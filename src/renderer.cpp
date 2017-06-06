#include <cairo/cairo-xcb.h> // for cairo_xcb_surface_set_size
#include <pango/pangocairo.h>
#include <cmath>
#include <vector>

#include "rwte/config.h"
#include "rwte/renderer.h"
#include "rwte/logging.h"
#include "rwte/term.h"
#include "rwte/utf8.h"
#include "rwte/rwte.h"
#include "rwte/luastate.h"
#include "rwte/selection.h"

#define LOGGER() (logging::get("renderer"))

#define LIMIT(x, a, b)  ((x) = (x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
#define IS_TRUECOL(x)  (1 << 24 & (x))
#define TRUECOL(r,g,b) (1 << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF))

#define REDBYTE(c) ((c & 0xFF0000) >> 16)
#define GREENBYTE(c) ((c & 0x00FF00) >> 8)
#define BLUEBYTE(c) (c & 0x0000FF)

typedef std::shared_ptr<cairo_font_options_t> shared_font_options;

struct font_desc_deleter {
    void operator()(PangoFontDescription* fontdesc) { if (fontdesc) pango_font_description_free(fontdesc); }
};
typedef std::unique_ptr<PangoFontDescription, font_desc_deleter> unique_font_desc;

struct layout_deleter {
    void operator()(PangoLayout* layout) { if (layout) g_object_unref(layout); }
};
typedef std::unique_ptr<PangoLayout, layout_deleter> unique_layout;

typedef std::shared_ptr<cairo_surface_t> shared_surface;
typedef std::shared_ptr<cairo_t> shared_cairo;

static shared_font_options create_font_options()
{
    shared_font_options fo(cairo_font_options_create(),
            cairo_font_options_destroy);

    cairo_font_options_set_hint_metrics(fo.get(), CAIRO_HINT_METRICS_ON);
    cairo_font_options_set_hint_style(fo.get(), CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_subpixel_order(fo.get(), CAIRO_SUBPIXEL_ORDER_RGB);
    cairo_font_options_set_antialias(fo.get(), CAIRO_ANTIALIAS_SUBPIXEL);

    return fo;
}

static unique_font_desc create_font_desc()
{
    std::string font = options.font;
    if (font.empty())
    {
        // get font from lua config
        auto L = rwte.lua();
        L->getglobal("config");
        L->getfield(-1, "font");

        const char * s = L->tostring(-1);
        if (!s)
            LOGGER()->fatal("config.font is invalid");
        else
            font = s;

        L->pop(2);
    }

    unique_font_desc fontdesc(pango_font_description_from_string(font.c_str()));

    pango_font_description_set_weight(fontdesc.get(), PANGO_WEIGHT_MEDIUM);
    return fontdesc;
}

static uint32_t lookup_color(uint32_t color)
{
    // only need to lookup color if the magic bit is set
    if (!IS_TRUECOL(color))
    {
        // get colors lut
        auto L = rwte.lua();
        L->getglobal("config");
        L->getfield(-1, "colors");
        if (!L->istable(-1))
            LOGGER()->fatal("config.colors is not a table");

        // look up color
        L->geti(-1, color);
        int isnum = 0;
        color = L->tointegerx(-1, &isnum);
        if (!isnum)
        {
            // no match...find out what the black index is
            L->getfield(-3, "black_idx");
            color = L->tointegerx(-1, &isnum);
            if (!isnum)
                LOGGER()->fatal("config.black_idx is not an integer");

            // now look up black
            L->geti(-3, color);
            color = L->tointegerx(-1, &isnum);
            if (!isnum)
                LOGGER()->fatal("config.black_idx is not an valid index");

            L->pop(2);
        }

        L->pop(3);
    }

    return color;
}

static void set_cairo_color(cairo_t *cr, uint32_t color)
{
    // make sure we have a real color
    color = lookup_color(color);

    double r = REDBYTE(color) / 255.0;
    double g = GREENBYTE(color) / 255.0;
    double b = BLUEBYTE(color) / 255.0;

    cairo_set_source_rgb(cr, r, g, b);
}

// todo: move somewhere common
static int get_border_px()
{
    // if invalid, default to 2
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "border_px");
    int border_px = L->tointegerdef(-1, 2);
    L->pop(2);

    return border_px;
}

static int get_cursor_thickness()
{
    // if invalid, default to 2
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "cursor_thickness");
    int cursor_thickness = L->tointegerdef(-1, 2);
    L->pop(2);

    return cursor_thickness;
}

class Surface
{
public:
    Surface(cairo_surface_t *surface, shared_font_options fo, int width, int height) :
        m_surface(surface, cairo_surface_destroy),
        m_surfcr(cairo_create(surface), cairo_destroy),
        m_fo(fo),
        m_drawsurf(nullptr),
        m_drawcr(nullptr)
    {
#if DOUBLE_BUFFER
        init_draw_surface(width, height);
#else
        m_drawsurf = m_surface;
        m_drawcr = m_surfcr;

        set_defaults(m_drawcr.get());

        m_layout = create_layout(m_drawcr.get());
#endif

        // initial fill
        set_cairo_color(m_drawcr.get(), g_term->defbg());
        cairo_paint(m_drawcr.get());
    }

    void resize(int width, int height)
    {
        cairo_xcb_surface_set_size(m_surface.get(), width, height);

#if DOUBLE_BUFFER
        init_draw_surface(width, height);
#endif
    }

    void flush()
    {
#if DOUBLE_BUFFER
        cairo_set_source_surface(m_surfcr.get(), m_drawsurf.get(), 0, 0);
        cairo_paint(m_surfcr.get());
#endif
        cairo_surface_flush(m_surface.get());
    }

    cairo_t *cr() const { return m_drawcr.get(); }
    PangoLayout *layout() const { return m_layout.get(); }

private:
    void init_draw_surface(int width, int height)
    {
        shared_surface newsurf(
                cairo_surface_create_similar_image(
                    m_surface.get(), CAIRO_FORMAT_RGB24, width, height),
                cairo_surface_destroy);
        shared_cairo newcr(cairo_create(newsurf.get()), cairo_destroy);
        set_defaults(newcr.get());

        if (m_drawsurf)
        {
            // paint old surface to new one if it exists
            cairo_set_source_surface(newcr.get(), m_drawsurf.get(), 0, 0);
            cairo_paint(newcr.get());
        }

        // replace old drawcr, layout, and surface
        m_drawcr = newcr;
        m_layout = create_layout(m_drawcr.get());
        m_drawsurf = newsurf;
    }

    unique_layout create_layout(cairo_t *cr)
    {
        PangoContext *context = pango_cairo_create_context(cr);
        pango_cairo_context_set_font_options(context, m_fo.get());

        unique_layout layout(pango_layout_new(context));
        g_object_unref(context);
        return layout;
    }

    void set_defaults(cairo_t *cr)
    {
        // all of our lines are 1 wide
        cairo_set_line_width(cr, 1);
        // and we like antialiasing
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_SUBPIXEL);
    }

    shared_surface m_surface;
    shared_cairo m_surfcr;

    shared_font_options m_fo;

    shared_surface m_drawsurf;
    shared_cairo m_drawcr;
    unique_layout m_layout;
};

class RendererImpl
{
public:
    RendererImpl();

    void load_font(cairo_surface_t *root_surface);
    void set_surface(cairo_surface_t *surface, int width, int height);

    void resize(int width, int height);

    int charwidth() const { return m_cw; }
    int charheight() const { return m_ch; }

    void drawregion(int row1, int col1, int row2, int col2);

    int x2col(int x) const;
    int y2row(int y) const;

private:
    void clear(cairo_t *cr, int x1, int y1, int x2, int y2);
    void drawglyph(cairo_t *cr, PangoLayout *layout, const Glyph& glyph,
            int row, int col);
    void drawglyphs(cairo_t *cr, PangoLayout *layout,
            const glyph_attribute& attr, uint32_t fg, uint32_t bg,
            const std::vector<Rune>& runes, int row, int col);
    void drawcursor(cairo_t *cr, PangoLayout *layout);
    void load_font(cairo_t *cr);
    cairo_font_options_t *get_font_options();

    shared_font_options m_fo;
    std::unique_ptr<Surface> m_surface;

    int m_cw, m_ch;
    int m_width, m_height;
    int m_lastcurrow, m_lastcurcol;

    unique_font_desc m_fontdesc;
    int m_border_px;
};

RendererImpl::RendererImpl() :
    m_fo(create_font_options()),
    m_cw(0), m_ch(0),
    m_width(0), m_height(0),
    m_lastcurrow(0), m_lastcurcol(0),
    m_fontdesc(create_font_desc()),
    // initial border_px value; we'll keep it semi-fresh as
    // calls are made to public funcs
    m_border_px(get_border_px())
{ }

void RendererImpl::load_font(cairo_surface_t *root_surface)
{
    auto cr = cairo_create(root_surface);
    load_font(cr);
    cairo_destroy(cr);
}

void RendererImpl::set_surface(cairo_surface_t *surface, int width, int height)
{
    m_width = width;
    m_height = height;

    m_surface = std::make_unique<Surface>(surface, m_fo, width, height);
}

void RendererImpl::resize(int width, int height)
{
    // update border_px from the lua world
    m_border_px = get_border_px();

    m_surface->resize(width, height);

    if (m_width < width)
    {
        // paint from old width to new width, top to old height
        cairo_t *cr = m_surface->cr();
        set_cairo_color(cr, g_term->defbg());
        cairo_rectangle(cr, m_width, 0, width, m_height);
        cairo_fill(cr);
    }

    if (m_height < height)
    {
        // paint from old height to new height, all the way across
        cairo_t *cr = m_surface->cr();
        set_cairo_color(cr, g_term->defbg());
        cairo_rectangle(cr, 0, m_height, width, height);
        cairo_fill(cr);
    }

    m_width = width;
    m_height = height;

    LOGGER()->info("resize to {}x{}", width, height);
}

void RendererImpl::drawregion(int row1, int col1, int row2, int col2)
{
    // freshen up border_px
    m_border_px = get_border_px();

    cairo_t *cr = m_surface->cr();
    auto layout = m_surface->layout();

    auto& sel = g_term->sel();
    bool ena_sel = !sel.empty() && sel.alt == g_term->mode()[MODE_ALTSCREEN];

    std::vector<Rune> runes;
    for (int row = row1; row < row2; row++)
    {
        if (!g_term->isdirty(row))
            continue;

        g_term->cleardirty(row);

        int col = col1;
        while (col < col2)
        {
            runes.clear();
            Glyph g = g_term->glyph(row, col);
            if (!g.attr[ATTR_WDUMMY])
            {
                if (ena_sel && sel.selected(col, row))
                    g.attr[ATTR_REVERSE] = g.attr[ATTR_REVERSE] ^ true;
            }

            runes.push_back(g.u);

            for (int lookahead = col + 1; lookahead < col2; lookahead++)
            {
                const Glyph& g2 = g_term->glyph(row, lookahead);
                glyph_attribute attr2 = g2.attr;
                if (!attr2[ATTR_WDUMMY])
                {
                    if (ena_sel && sel.selected(lookahead, row))
                        attr2[ATTR_REVERSE] = attr2[ATTR_REVERSE] ^ true;
                }

                if (g.attr != attr2 || g.fg != g2.fg || g.bg != g2.bg)
                    break;

                runes.push_back(g2.u);
            }

            drawglyphs(cr, layout, g.attr, g.fg, g.bg, runes, row, col);
            col += runes.size();
        }
    }

    drawcursor(cr, layout);

    m_surface->flush();
}

int RendererImpl::x2col(int x) const
{
	x -= m_border_px;
	x /= m_cw;

	return LIMIT(x, 0, (m_width/m_cw)-1);
}

int RendererImpl::y2row(int y) const
{
	y -= m_border_px;
	y /= m_ch;

	return LIMIT(y, 0, (m_height/m_ch)-1);
}

void RendererImpl::clear(cairo_t *cr, int x1, int y1, int x2, int y2)
{
    uint32_t color;
    if (!g_term->mode()[MODE_REVERSE])
        color = g_term->defbg();
    else
        color = g_term->deffg();

    set_cairo_color(cr, color);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
    cairo_fill(cr);
}

void RendererImpl::drawglyph(cairo_t *cr, PangoLayout *layout, const Glyph& glyph,
        int row, int col)
{
    const std::vector<Rune> rune {glyph.u};
    drawglyphs(cr, layout, glyph.attr, glyph.fg, glyph.bg, rune, row, col);
}

void RendererImpl::drawglyphs(cairo_t *cr, PangoLayout *layout,
        const glyph_attribute& attr, uint32_t fg, uint32_t bg,
        const std::vector<Rune>& runes, int row, int col)
{
    int charlen = runes.size() * (attr[ATTR_WIDE] ? 2 : 1);
    int winx = m_border_px + col * m_cw;
    int winy = m_border_px + row * m_ch;
    int width = charlen * m_cw;

    // change basic system colors [0-7] to bright system colors [8-15]
    if (attr[ATTR_BOLD] && fg <= 7)
        fg = lookup_color(fg + 8);

    if (g_term->mode()[MODE_REVERSE])
    {
        // if the fg or bg color is a default, use the other one,
        // otherwise invert them bitwise

        fg = lookup_color(fg);
        if (fg == lookup_color(g_term->deffg()))
            fg = g_term->defbg();
        else
            fg = TRUECOL(~REDBYTE(fg), ~GREENBYTE(fg), ~BLUEBYTE(fg));

        bg = lookup_color(bg);
        if (bg == lookup_color(g_term->defbg()))
            bg = g_term->deffg();
        else
            bg = TRUECOL(~REDBYTE(bg), ~GREENBYTE(bg), ~BLUEBYTE(bg));
    }

    if (attr[ATTR_REVERSE])
        std::swap(fg, bg);

    // todo: this assumes darker is fainter
    if (attr[ATTR_FAINT])
    {
        fg = lookup_color(fg);
        fg = TRUECOL(REDBYTE(fg) / 2, GREENBYTE(fg) / 2, BLUEBYTE(fg) / 2);
    }

    if (attr[ATTR_BLINK] && g_term->mode()[MODE_BLINK])
        fg = bg;

    if (attr[ATTR_INVISIBLE])
        fg = bg;

    // border cleanup
    if (col == 0)
    {
        clear(cr, 0, (row == 0)? 0 : winy, m_border_px,
            winy + m_ch + ((row >= g_term->rows()-1)? m_height : 0));
    }
    if (col + charlen >= g_term->cols())
    {
        clear(cr, winx + width, (row == 0)? 0 : winy, m_width,
            ((row >= g_term->rows()-1)? m_height : (winy + m_ch)));
    }
    if (row == 0)
        clear(cr, winx, 0, winx + width, m_border_px);
    if (row == g_term->rows()-1)
        clear(cr, winx, winy + m_ch, winx + width, m_height);

    // clean up the region we want to draw to.
    set_cairo_color(cr, bg);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_rectangle(cr, winx, winy, width, m_ch);
    cairo_fill(cr);

    // render the glyphs

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    set_cairo_color(cr, fg);

    // decode to a vector
    std::vector<char> buf;
    char encoded[utf_size];
    std::size_t glyphlen = 0;
    for (auto& rune : runes)
    {
        glyphlen = utf8encode(rune, encoded);
        std::copy(encoded, encoded + glyphlen, std::back_inserter(buf));
    }

    // zero-terminate
    buf.push_back(0);

    pango_layout_set_text(layout, &buf[0], -1);
    pango_layout_set_font_description(layout, m_fontdesc.get());

    PangoAttrList *attrlist = nullptr;

    if (attr[ATTR_ITALIC])
    {
        attrlist = pango_attr_list_new();
        auto attr = pango_attr_style_new(PANGO_STYLE_ITALIC);
        pango_attr_list_insert(attrlist, attr);
    }

    if (attr[ATTR_BOLD])
    {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        pango_attr_list_insert(attrlist, attr);
    }

    if (attr[ATTR_UNDERLINE])
    {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        pango_attr_list_insert(attrlist, attr);
    }

    if (attr[ATTR_STRUCK])
    {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_strikethrough_new(true);
        pango_attr_list_insert(attrlist, attr);
    }

    pango_layout_set_attributes(layout, attrlist);
    if (attrlist)
        pango_attr_list_unref(attrlist);

    // needed? pango_cairo_update_layout(cr, layout);
    cairo_move_to(cr, winx, winy);
    pango_cairo_show_layout(cr, layout);
}

void RendererImpl::drawcursor(cairo_t *cr, PangoLayout *layout)
{
    Glyph g;
    g.u = ' ';
    g.fg = g_term->defbg();
    g.bg = g_term->defcs();

    auto& cursor = g_term->cursor();

    LIMIT(m_lastcurcol, 0, g_term->cols()-1);
    LIMIT(m_lastcurrow, 0, g_term->rows()-1);

    int curcol = cursor.col;

    // adjust position if in dummy
    if (g_term->glyph(m_lastcurrow, m_lastcurcol).attr[ATTR_WDUMMY])
        m_lastcurcol--;
    if (g_term->glyph(cursor.row, curcol).attr[ATTR_WDUMMY])
        curcol--;

    auto& sel = g_term->sel();
    bool ena_sel = !sel.empty() && sel.alt == g_term->mode()[MODE_ALTSCREEN];

    // remove the old cursor
    Glyph og = g_term->glyph(m_lastcurrow, m_lastcurcol);
    if (ena_sel && sel.selected(m_lastcurcol, m_lastcurrow))
        og.attr[ATTR_REVERSE] = og.attr[ATTR_REVERSE] ^ true;
    drawglyph(cr, layout, og, m_lastcurrow, m_lastcurcol);

    auto& oldg = g_term->glyph(cursor.row, cursor.col);
    g.u = oldg.u;
    g.attr[ATTR_BOLD] = oldg.attr[ATTR_BOLD];
    g.attr[ATTR_ITALIC] = oldg.attr[ATTR_ITALIC];
    g.attr[ATTR_UNDERLINE] = oldg.attr[ATTR_UNDERLINE];
    g.attr[ATTR_STRUCK] = oldg.attr[ATTR_STRUCK];

    // select the right color for the right mode.
    uint32_t drawcol;
    if (g_term->mode()[MODE_REVERSE])
    {
        g.attr.set(ATTR_REVERSE);
        g.bg = g_term->deffg();
        if (ena_sel && sel.selected(cursor.col, cursor.row))
        {
            drawcol = g_term->defcs();
            g.fg = g_term->defrcs();
        }
        else
        {
            drawcol = g_term->defrcs();
            g.fg = g_term->defcs();
        }
    }
    else
    {
        if (ena_sel && sel.selected(cursor.col, cursor.row))
        {
            drawcol = g_term->defrcs();
            g.fg = g_term->deffg();
            g.bg = g_term->defrcs();
        }
        else
        {
            drawcol = g_term->defcs();
        }
    }

    if (g_term->mode()[MODE_HIDE])
        return;

    // draw the new one
    if (g_term->focused())
    {
        switch (g_term->cursortype())
        {
        case CURSOR_BLINK_BLOCK:
            if (g_term->mode()[MODE_BLINK])
                break;
            // fall through
        case CURSOR_STEADY_BLOCK:
            g.attr[ATTR_WIDE] = g_term->glyph(cursor.row, curcol).attr[ATTR_WIDE];
            drawglyph(cr, layout, g, cursor.row, cursor.col);
            break;
        case CURSOR_BLINK_UNDER:
            if (g_term->mode()[MODE_BLINK])
                break;
            // fall through
        case CURSOR_STEADY_UNDER:
            {
                int cursor_thickness = get_cursor_thickness();
                set_cairo_color(cr, drawcol);
                cairo_rectangle(cr,
                    m_border_px + curcol * m_cw,
                    m_border_px + (cursor.row + 1) * m_ch - cursor_thickness,
                    m_cw,
                    cursor_thickness
                );
                cairo_fill(cr);
            }
            break;
        case CURSOR_BLINK_BAR:
            if (g_term->mode()[MODE_BLINK])
                break;
            // fall through
        case CURSOR_STEADY_BAR:
            {
                int cursor_thickness = get_cursor_thickness();
                set_cairo_color(cr, drawcol);
                cairo_rectangle(cr,
                    m_border_px + curcol * m_cw,
                    m_border_px + cursor.row * m_ch,
                    cursor_thickness,
                    m_ch
                );
                cairo_fill(cr);
            }
            break;
        }
    }
    else
    {
        set_cairo_color(cr, drawcol);
        cairo_rectangle(cr,
            m_border_px + curcol * m_cw + 0.5,
            m_border_px + cursor.row * m_ch + 0.5,
            m_cw - 1,
            m_ch - 1
        );
        cairo_stroke(cr);
    }
    m_lastcurcol = curcol, m_lastcurrow = cursor.row;
}

void RendererImpl::load_font(cairo_t *cr)
{
    auto L = rwte.lua();
    L->getglobal("config");

    int isnum = 0;
    L->getfield(-1, "cw_scale");
    float cw_scale = L->tonumberx(-1, &isnum);
    if (!isnum)
        cw_scale = 1.0; // if value is bad, no scaling

    L->getfield(-2, "ch_scale");
    float ch_scale = L->tonumberx(-1, &isnum);
    if (!isnum)
        ch_scale = 1.0; // if value is bad, no scaling

    L->pop(3);

    // don't free defaults
    PangoFontMap *fontmap = pango_cairo_font_map_get_default();
    PangoLanguage *lang = pango_language_get_default();

    // measure font
    PangoContext *context = pango_cairo_create_context(cr);
    PangoFont *font = pango_font_map_load_font(fontmap, context, m_fontdesc.get());
    PangoFontMetrics *metrics = pango_font_get_metrics(font, lang);
    m_cw = std::ceil(
            (pango_font_metrics_get_approximate_char_width(metrics) / PANGO_SCALE) *
            cw_scale);
    m_ch = std::ceil(
            ((pango_font_metrics_get_ascent(metrics) +
                pango_font_metrics_get_descent(metrics)) / PANGO_SCALE) *
            ch_scale);

    if (LOGGER()->level() <= logging::debug)
    {
        char *font = pango_font_description_to_string(m_fontdesc.get());
        LOGGER()->debug("loaded {}, font size {}x{}", font, m_cw, m_ch);
        g_free(font);
    }

    pango_font_metrics_unref(metrics);
    g_object_unref(font);
    g_object_unref(context);
}

Renderer::Renderer() :
    impl(std::make_unique<RendererImpl>())
{ }

Renderer::~Renderer()
{ }

void Renderer::load_font(cairo_surface_t *root_surface)
{ impl->load_font(root_surface); }

void Renderer::set_surface(cairo_surface_t *surface, int width, int height)
{ impl->set_surface(surface, width, height); }

void Renderer::resize(int width, int height)
{ impl->resize(width, height); }

int Renderer::charwidth() const
{ return impl->charwidth(); }

int Renderer::charheight() const
{ return impl->charheight(); }

void Renderer::drawregion(int row1, int col1, int row2, int col2)
{ impl->drawregion(row1, col1, row2, col2); }

int Renderer::x2col(int x) const
{ return impl->x2col(x); }

int Renderer::y2row(int y) const
{ return impl->y2row(y); }
