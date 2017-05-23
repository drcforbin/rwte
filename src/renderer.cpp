#include <cairo/cairo-xcb.h> // for cairo_xcb_surface_set_size
#include <pango/pangocairo.h>
#include <cmath>

#include "rwte/config.h"
#include "rwte/renderer.h"
#include "rwte/logging.h"
#include "rwte/term.h"
#include "rwte/utf8.h"
#include "rwte/rwte.h"
#include "rwte/luastate.h"

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
    // get colors lut
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "font");

    const char * font = L->tostring(-1);
    if (!font)
        LOGGER()->fatal("config.font is invalid");

    unique_font_desc fontdesc(pango_font_description_from_string(font));

    L->pop(2);

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

static int get_border_px()
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "border_px");
    int isnum = 0;
    int border_px = L->tointegerx(-1, &isnum);
    if (!isnum)
        border_px = 2; // if value is bad, use 2
    L->pop(2);

    return border_px;
}

static int get_cursor_thickness()
{
    auto L = rwte.lua();
    L->getglobal("config");
    L->getfield(-1, "cursor_thickness");
    int isnum = 0;
    int cursor_thickness = L->tointegerx(-1, &isnum);
    if (!isnum)
        cursor_thickness = 2; // if value is bad, use 2
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
    RendererImpl(cairo_surface_t *surface, int width, int height);

    void resize(int width, int height);

    int charwidth() const { return m_cw; }
    int charheight() const { return m_ch; }

    void drawregion(int row1, int col1, int row2, int col2);

    int x2col(int x) const;
    int y2row(int y) const;

private:
    void clear(cairo_t *cr, int x1, int y1, int x2, int y2);
    void clearrow(cairo_t *cr, int row, int col1, int col2);
    void drawglyph(cairo_t *cr, PangoLayout *layout, Glyph glyph, int row, int col);
    void drawcursor(cairo_t *cr, PangoLayout *layout);
    void load_font();
    cairo_font_options_t *get_font_options();
    int selected(const Selection& sel, int col, int row);

    shared_font_options m_fo;
    Surface m_surface;

    int m_cw, m_ch;
    int m_width, m_height;
    int m_lastcurrow, m_lastcurcol;

    unique_font_desc m_fontdesc;
    int m_border_px;
};

RendererImpl::RendererImpl(cairo_surface_t *surface, int width, int height) :
    m_fo(create_font_options()),
    m_surface(surface, m_fo, width, height),
    m_cw(0), m_ch(0),
    m_width(width), m_height(height),
    m_lastcurrow(0), m_lastcurcol(0),
    m_fontdesc(create_font_desc()),
    // initial border_px value; we'll keep it semi-fresh as
    // calls are made to public funcs
    m_border_px(get_border_px())
{
    load_font();
}

void RendererImpl::resize(int width, int height)
{
    // update border_px from the lua world
    m_border_px = get_border_px();

    m_surface.resize(width, height);

    if (m_width < width)
    {
        // paint from old width to new width, top to old height
        cairo_t *cr = m_surface.cr();
        set_cairo_color(cr, g_term->defbg());
        cairo_rectangle(cr, m_width, 0, width, m_height);
        cairo_fill(cr);
    }

    if (m_height < height)
    {
        // paint from old height to new height, all the way across
        cairo_t *cr = m_surface.cr();
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

    cairo_t *cr = m_surface.cr();
    auto layout = m_surface.layout();

    auto& sel = g_term->sel();
    bool ena_sel = sel.ob.col != -1 && sel.alt == g_term->mode()[MODE_ALTSCREEN];

    for (int row = row1; row < row2; row++)
    {
        if (!g_term->isdirty(row))
            continue;

        g_term->cleardirty(row);

        // todo: get smarter about this clearing and drawing;
        // make runs of same attr, then draw at once
        clearrow(cr, row, col1, col2);
        for (int col = col1; col < col2; col++)
        {
            Glyph g = g_term->glyph(row, col);
            if (!g.attr[ATTR_WDUMMY])
            {
                if (ena_sel && selected(sel, col, row))
                    g.attr[ATTR_REVERSE] = g.attr[ATTR_REVERSE] ^ true;
                drawglyph(cr, layout, g, row, col);
            }
        }
    }

    drawcursor(cr, layout);

    m_surface.flush();
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

void RendererImpl::clearrow(cairo_t *cr, int row, int col1, int col2)
{
    int y = row * m_ch;
    clear(cr, m_border_px + col1 * m_cw, m_border_px + y, col2 * m_cw, y + m_ch);
}

void RendererImpl::drawglyph(cairo_t *cr, PangoLayout *layout, Glyph glyph, int row, int col)
{
    const int len = 1; // todo: handle runs of chars with same attr
    int charlen = len * ((glyph.attr[ATTR_WIDE]) ? 2 : 1);
    int winx = m_border_px + col * m_cw;
    int winy = m_border_px + row * m_ch;
    int width = charlen * m_cw;

    uint32_t fg = glyph.fg;
    uint32_t bg = glyph.bg;

    // change basic system colors [0-7] to bright system colors [8-15]
    if (glyph.attr[ATTR_BOLD] && fg <= 7)
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

    if (glyph.attr[ATTR_REVERSE])
        std::swap(fg, bg);

    // todo: this assumes darker is fainter
    if (glyph.attr[ATTR_FAINT])
    {
        fg = lookup_color(fg);
        fg = TRUECOL(REDBYTE(fg) / 2, GREENBYTE(fg) / 2, BLUEBYTE(fg) / 2);
    }

    if (glyph.attr[ATTR_BLINK] && glyph.attr[MODE_BLINK])
        fg = bg;

    if (glyph.attr[ATTR_INVISIBLE])
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

    // render the glyph

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    set_cairo_color(cr, fg);

    char encoded[utf_size + 1];
    auto glyphlen = utf8encode(glyph.u, encoded);
    encoded[glyphlen] = 0;

    pango_layout_set_text(layout, encoded, -1);
    pango_layout_set_font_description(layout, m_fontdesc.get());

    PangoAttrList *attrlist = nullptr;

    if (glyph.attr[ATTR_ITALIC])
    {
        attrlist = pango_attr_list_new();
        auto attr = pango_attr_style_new(PANGO_STYLE_ITALIC);
        pango_attr_list_insert(attrlist, attr);
    }

    if (glyph.attr[ATTR_BOLD])
    {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        pango_attr_list_insert(attrlist, attr);
    }

    if (glyph.attr[ATTR_UNDERLINE])
    {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        pango_attr_list_insert(attrlist, attr);
    }

    if (glyph.attr[ATTR_STRUCK])
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
    bool ena_sel = sel.ob.col != -1 && sel.alt == g_term->mode()[MODE_ALTSCREEN];

    // remove the old cursor
    Glyph og = g_term->glyph(m_lastcurrow, m_lastcurcol);
    if (ena_sel && selected(sel, m_lastcurcol, m_lastcurrow))
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
        if (ena_sel && selected(sel, cursor.col, cursor.row))
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
        if (ena_sel && selected(sel, cursor.col, cursor.row))
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
        case CURSOR_STEADY_BLOCK:
            g.attr[ATTR_WIDE] = g_term->glyph(cursor.row, curcol).attr[ATTR_WIDE];
            drawglyph(cr, layout, g, cursor.row, cursor.col);
            break;
        case CURSOR_BLINK_UNDER:
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

void RendererImpl::load_font()
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
    cairo_t *cr = m_surface.cr();
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

// todo: move to Selection
int RendererImpl::selected(const Selection& sel, int col, int row)
{
    if (sel.mode == SEL_EMPTY)
        return 0;

    if (sel.type == SEL_RECTANGULAR)
        return (sel.nb.row <= row && row <= sel.ne.row) &&
            (sel.nb.col <= col && col <= sel.ne.col);

    return (sel.nb.row <= row && row <= sel.ne.row) &&
        (row != sel.nb.row || col >= sel.nb.col) &&
        (row != sel.ne.row || col <= sel.ne.col);
}

Renderer::Renderer(cairo_surface_t *surface, int width, int height) :
    impl(std::make_unique<RendererImpl>(surface, width, height))
{ }

Renderer::~Renderer()
{ }

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
