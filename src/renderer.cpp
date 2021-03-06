#include "lua/config.h"
#include "lua/state.h"
#include "rw/logging.h"
#include "rw/utf8.h"
#include "rwte/color.h"
#include "rwte/config.h"
#include "rwte/renderer.h"
#include "rwte/rwte.h"
#include "rwte/screen.h"
#include "rwte/selection.h"
#include "rwte/term.h"

#include <cairo/cairo-xcb.h> // for cairo_xcb_surface_set_size
#include <cmath>
#include <pango/pangocairo.h>
#include <vector>

#define LOGGER() (rw::logging::get("renderer"))

namespace renderer {

/// \brief Converts 6-level color to 8-bit color
///
/// todo: fix comment
/// Six levels for every primary, with 6³ = 216 combinations. The index
/// can be addressed by (36×R)+(6×G)+B, with all R, G and B values in a
/// range from 0 to 5. Intended as homogeneous RGB cube, it gives six
/// true grays. Also there is room for another sorts of 40 colors, so
/// operating systems or programs can add extra colors.
constexpr uint16_t sixd_to_16bit(int x)
{
    return x == 0 ? 0 : 0x3737 + 0x2828 * x;
}

static uint32_t lookup_color(uint32_t color)
{
    // only need to lookup color if the magic bit is set
    if (!color::isTruecol(color)) {
        // get colors lut
        auto L = rwte->lua();
        L->getglobal("config");
        L->getfield(-1, "colors");
        if (!L->istable(-1))
            LOGGER()->fatal("config.colors is not a table");

        // look up color
        L->geti(-1, color);
        int isnum = 0;
        uint32_t t = L->tointegerx(-1, &isnum);
        if (isnum) {
            // it's valid, we can use it
            color = t;
        } else {
            if (16 <= color && color <= 255) {
                // 256 color
                if (color < 6 * 6 * 6 + 16) {
                    // same colors as xterm
                    color = color::truecol(
                            sixd_to_16bit(((color - 16) / 36) % 6),
                            sixd_to_16bit(((color - 16) / 6) % 6),
                            sixd_to_16bit(((color - 16) / 1) % 6));
                } else {
                    // greyscale
                    int val = 0x0808 + 0x0a0a * (color - (6 * 6 * 6 + 16));
                    color = color::truecol(val, val, val);
                }
            } else {
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
        }

        L->pop(3);
    }

    return color;
}

class Context
{
public:
    Context() {}

    Context(cairo_surface_t* surface) :
        m_ctx(cairo_create(surface))
    {}

    // copy ctor
    Context(const Context& other)
    {
        if (other.m_ctx)
            m_ctx = cairo_reference(other.m_ctx);
    }

    // move ctor
    Context(Context&& other) noexcept :
        m_ctx(std::exchange(other.m_ctx, nullptr))
    {}

    ~Context()
    {
        if (m_ctx) {
            cairo_destroy(m_ctx);
            m_ctx = nullptr;
        }
    }

    // copy assign
    Context& operator=(const Context& other)
    {
        return *this = Context(other);
    }

    // move assign
    Context& operator=(Context&& other) noexcept
    {
        std::swap(m_ctx, other.m_ctx);
        return *this;
    }

    void setOperator(cairo_operator_t op)
    {
        cairo_set_operator(m_ctx, op);
    }

    void rectangle(double x, double y, double width, double height)
    {
        cairo_rectangle(m_ctx, x, y, width, height);
    }

    void fill()
    {
        cairo_fill(m_ctx);
    }

    void stroke()
    {
        cairo_stroke(m_ctx);
    }

    void setLineWidth(double width)
    {
        cairo_set_line_width(m_ctx, width);
    }

    void setAntialias(cairo_antialias_t antialias)
    {
        cairo_set_antialias(m_ctx, antialias);
    }

    void moveTo(double x, double y)
    {
        cairo_move_to(m_ctx, x, y);
    }

    void setSourceRgb(double r, double g, double b)
    {
        cairo_set_source_rgb(m_ctx, r, g, b);
    }

    void showLayout(PangoLayout* layout)
    {
        pango_cairo_show_layout(m_ctx, layout);
    }

    void paint()
    {
        cairo_paint(m_ctx);
    }

    void setSourceColor(uint32_t color)
    {
        // make sure we have a real color
        color = lookup_color(color);

        double r = color::redByte(color) / 255.0;
        double g = color::greenByte(color) / 255.0;
        double b = color::blueByte(color) / 255.0;

        setSourceRgb(r, g, b);
    }

    cairo_t* get() { return m_ctx; }

private:
    cairo_t* m_ctx = nullptr;
};

static PangoFontDescription* create_font_desc()
{
    auto font = options.font;
    if (font.empty()) {
        // get font from lua config
        font = lua::config::get_string("font");
        if (font.empty())
            LOGGER()->fatal("config.font is invalid");
    }

    PangoFontDescription* fontdesc = pango_font_description_from_string(font.c_str());

    pango_font_description_set_weight(fontdesc, PANGO_WEIGHT_MEDIUM);
    return fontdesc;
}

// todo: move somewhere common
static int get_border_px()
{
    // if invalid, default to 2
    return lua::config::get_int("border_px", 2);
}

static int get_cursor_thickness()
{
    // if invalid, default to 2
    return lua::config::get_int("cursor_thickness", 2);
}

class Surface
{
public:
    Surface(cairo_surface_t* surface, cairo_font_options_t* fo,
            PangoFontDescription* fontdesc, int width,
            int height) :
        m_surface(surface),
        m_cr(surface)
    {
        set_defaults(m_cr);

        PangoContext* context = pango_cairo_create_context(m_cr.get());
        pango_cairo_context_set_font_options(context, fo);
        pango_context_set_font_description(context, fontdesc);
        m_layout = pango_layout_new(context);
        g_object_unref(context);
    }

    ~Surface()
    {
        if (m_surface)
            cairo_surface_destroy(m_surface);
        if (m_layout)
            g_object_unref(m_layout);
    }

    void resize(int width, int height)
    {
        if (cairo_surface_get_type(m_surface) == CAIRO_SURFACE_TYPE_XCB)
            cairo_xcb_surface_set_size(m_surface, width, height);
    }

    void flush()
    {
        cairo_surface_flush(m_surface);
    }

    cairo_surface_t* get() { return m_surface; }
    Context cr() const { return m_cr; }
    PangoLayout* layout() const { return m_layout; }

private:
    void set_defaults(Context& ctx)
    {
        // all of our lines are 1 wide
        ctx.setLineWidth(1);
        // and we like antialiasing
        ctx.setAntialias(CAIRO_ANTIALIAS_SUBPIXEL);
    }

    cairo_surface_t* m_surface;
    Context m_cr;
    PangoLayout* m_layout;
};

class RendererImpl
{
public:
    RendererImpl(term::Term* term);
    ~RendererImpl();

    void load_font(cairo_surface_t* root_surface);
    void set_surface(cairo_surface_t* surface, int width, int height);

    void resize(int width, int height);

    int charwidth() const { return m_cw; }
    int charheight() const { return m_ch; }

    void drawregion(const Cell& begin, const Cell& end);

    Cell pxtocell(int x, int y) const;

private:
    void clear(Context& cr, int x1, int y1, int x2, int y2);
    void drawglyph(Context& cr, PangoLayout* layout,
            const screen::Glyph& glyph, const Cell& cell);
    void drawglyphs(Context& cr, PangoLayout* layout,
            const screen::glyph_attribute& attr, uint32_t fg, uint32_t bg,
            const std::vector<char32_t>& runes, const Cell& cell);
    void drawcursor(Context& cr, PangoLayout* layout);
    void load_font(Context& cr);

    term::Term* m_term;

    cairo_font_options_t* m_fo;
    PangoFontDescription* m_fontdesc;

    std::unique_ptr<Surface> m_surface;

    int m_cw = 0, m_ch = 0;
    int m_width = 0, m_height = 0;
    Cell m_lastcur{0, 0};

    int m_border_px;
};

RendererImpl::RendererImpl(term::Term* term) :
    m_term(term),
    m_fo(cairo_font_options_create()),
    m_fontdesc(create_font_desc()),
    // initial border_px value; we'll keep it semi-fresh as
    // calls are made to public funcs
    m_border_px(get_border_px())
{
    cairo_font_options_set_hint_metrics(m_fo, CAIRO_HINT_METRICS_ON);
    cairo_font_options_set_hint_style(m_fo, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_subpixel_order(m_fo, CAIRO_SUBPIXEL_ORDER_RGB);
    cairo_font_options_set_antialias(m_fo, CAIRO_ANTIALIAS_SUBPIXEL);
}

RendererImpl::~RendererImpl()
{
    if (m_fontdesc)
        pango_font_description_free(m_fontdesc);
    if (m_fo)
        cairo_font_options_destroy(m_fo);
}

void RendererImpl::load_font(cairo_surface_t* root_surface)
{
    Context cr{root_surface};
    load_font(cr);
}

void RendererImpl::set_surface(cairo_surface_t* surface, int width, int height)
{
    m_width = width;
    m_height = height;

    if (surface) {
        m_surface = std::make_unique<Surface>(surface, m_fo, m_fontdesc,
                width, height);
    } else {
        m_surface.reset();
    }
}

void RendererImpl::resize(int width, int height)
{
    // update border_px from the lua world
    m_border_px = get_border_px();

    m_surface->resize(width, height);

    if (m_width < width) {
        // paint from old width to new width, top to old height
        auto cr = m_surface->cr();
        cr.setSourceColor(m_term->defbg());
        cr.rectangle(m_width, 0, width, m_height);
        cr.fill();
    }

    if (m_height < height) {
        // paint from old height to new height, all the way across
        auto cr = m_surface->cr();
        cr.setSourceColor(m_term->defbg());
        cr.rectangle(0, m_height, width, height);
        cr.fill();
    }

    m_width = width;
    m_height = height;

    LOGGER()->info("resize to {}x{}", width, height);
}

void RendererImpl::drawregion(const Cell& begin, const Cell& end)
{
    // freshen up border_px
    m_border_px = get_border_px();

    auto cr = m_surface->cr();
    auto layout = m_surface->layout();

    /*
    // test painting
    cr.setOperator(CAIRO_OPERATOR_SOURCE);
    if (m_term->focused())
        cr.setSourceRgb(1, 0, 0);
    else
        cr.setSourceRgb(0, 0, 1);
    cr.paint();
    m_surface->flush();
    return;
    */

    auto& sel = m_term->sel();
    bool ena_sel = !sel.empty() &&
                   sel.alt == m_term->mode()[term::MODE_ALTSCREEN];

    std::vector<char32_t> runes;
    Cell cell;
    for (cell.row = begin.row; cell.row < end.row; cell.row++) {
        if (!m_term->isdirty(cell.row))
            continue;

        m_term->cleardirty(cell.row);

        cell.col = begin.col;
        while (cell.col < end.col) {
            runes.clear();

            // making a copy, because we want to reverse it if it's
            // selected, without modifying the original
            screen::Glyph g = m_term->glyph(cell);
            if (!g.attr.wdummy) {
                if (ena_sel && sel.selected(cell)) {
                    g.attr.reverse ^= 1;
                }
            }

            runes.push_back(g.u);

            for (int lookahead = cell.col + 1; lookahead < end.col; lookahead++) {
                const screen::Glyph& g2 = m_term->glyph(
                        {cell.row, lookahead});
                screen::glyph_attribute attr2 = g2.attr;
                if (!attr2.wdummy) {
                    if (ena_sel && sel.selected({cell.row, lookahead})) {
                        attr2.reverse ^= 1;
                    }
                }

                if (g.attr != attr2 || g.fg != g2.fg || g.bg != g2.bg)
                    break;

                runes.push_back(g2.u);
            }

            drawglyphs(cr, layout, g.attr, g.fg, g.bg, runes, cell);
            cell.col += runes.size();
        }
    }

    drawcursor(cr, layout);

    m_surface->flush();
}

Cell RendererImpl::pxtocell(int x, int y) const
{
    int col = (x - m_border_px) / m_cw;
    int row = (y - m_border_px) / m_ch;

    return {
            std::clamp(row, 0, (m_height / m_ch) - 1),
            std::clamp(col, 0, (m_width / m_cw) - 1)};
}

void RendererImpl::clear(Context& cr, int x1, int y1, int x2, int y2)
{
    uint32_t color;
    if (!m_term->mode()[term::MODE_REVERSE])
        color = m_term->defbg();
    else
        color = m_term->deffg();

    cr.setSourceColor(color);
    cr.setOperator(CAIRO_OPERATOR_SOURCE);
    cr.rectangle(x1, y1, x2 - x1, y2 - y1);
    cr.fill();
}

void RendererImpl::drawglyph(Context& cr, PangoLayout* layout,
        const screen::Glyph& glyph, const Cell& cell)
{
    const std::vector<char32_t> rune{glyph.u};
    drawglyphs(cr, layout, glyph.attr, glyph.fg, glyph.bg, rune, cell);
}

void RendererImpl::drawglyphs(Context& cr, PangoLayout* layout,
        const screen::glyph_attribute& attr, uint32_t fg, uint32_t bg,
        const std::vector<char32_t>& runes, const Cell& cell)
{
    int charlen = runes.size() * (attr.wide ? 2 : 1);
    int winx = m_border_px + cell.col * m_cw;
    int winy = m_border_px + cell.row * m_ch;
    int width = charlen * m_cw;

    // change basic system colors [0-7] to bright system colors [8-15]
    if (attr.bold && fg <= 7)
        fg = lookup_color(fg + 8);

    if (m_term->mode()[term::MODE_REVERSE]) {
        // if the fg or bg color is a default, use the other one,
        // otherwise invert them bitwise

        fg = lookup_color(fg);
        if (fg == lookup_color(m_term->deffg()))
            fg = m_term->defbg();
        else
            fg = color::truecol(
                    ~color::redByte(fg),
                    ~color::greenByte(fg),
                    ~color::blueByte(fg));

        bg = lookup_color(bg);
        if (bg == lookup_color(m_term->defbg()))
            bg = m_term->deffg();
        else
            bg = color::truecol(
                    ~color::redByte(bg),
                    ~color::greenByte(bg),
                    ~color::blueByte(bg));
    }

    // todo: this assumes darker is fainter
    if (attr.faint) {
        fg = lookup_color(fg);
        fg = color::truecol(
                color::redByte(fg) / 2,
                color::greenByte(fg) / 2,
                color::blueByte(fg) / 2);
    }

    if (attr.reverse) {
        std::swap(fg, bg);
    }

    if (attr.blink && m_term->mode()[term::MODE_BLINK])
        fg = bg;

    if (attr.invisible)
        fg = bg;

    // border cleanup
    if (cell.col == 0) {
        clear(cr, 0, (cell.row == 0) ? 0 : winy, m_border_px,
                winy + m_ch + ((cell.row >= m_term->rows() - 1) ? m_height : 0));
    }
    if (cell.col + charlen >= m_term->cols()) {
        clear(cr, winx + width, (cell.row == 0) ? 0 : winy, m_width,
                ((cell.row >= m_term->rows() - 1) ? m_height : (winy + m_ch)));
    }
    if (cell.row == 0)
        clear(cr, winx, 0, winx + width, m_border_px);
    if (cell.row == m_term->rows() - 1)
        clear(cr, winx, winy + m_ch, winx + width, m_height);

    // clean up the region we want to draw to.
    cr.setSourceColor(bg);
    cr.setOperator(CAIRO_OPERATOR_SOURCE);
    cr.rectangle(winx, winy, width, m_ch);
    cr.fill();

    // render the glyphs

    cr.setOperator(CAIRO_OPERATOR_OVER);
    cr.setSourceColor(fg);

    // decode to a vector
    std::vector<char> buf;
    for (const auto& rune : runes) {
        utf8encode(rune, std::back_inserter(buf));
    }

    // zero-terminate
    buf.push_back(0);

    pango_layout_set_text(layout, &buf[0], -1);

    PangoAttrList* attrlist = nullptr;

    if (attr.italic) {
        attrlist = pango_attr_list_new();
        auto attr = pango_attr_style_new(PANGO_STYLE_ITALIC);
        pango_attr_list_insert(attrlist, attr);
    }

    if (attr.bold) {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        pango_attr_list_insert(attrlist, attr);
    }

    if (attr.underline) {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        pango_attr_list_insert(attrlist, attr);
    }

    if (attr.struck) {
        if (!attrlist)
            attrlist = pango_attr_list_new();
        auto attr = pango_attr_strikethrough_new(true);
        pango_attr_list_insert(attrlist, attr);
    }

    // todo: look at pango_layout_set_justify + pango_layout_set_width

    pango_layout_set_attributes(layout, attrlist);
    if (attrlist)
        pango_attr_list_unref(attrlist);

    // needed? pango_cairo_update_layout(cr, layout);
    cr.moveTo(winx, winy);
    cr.showLayout(layout);
}

void RendererImpl::drawcursor(Context& cr, PangoLayout* layout)
{
    screen::Glyph g{
            .u = ' ',
            .fg = m_term->defbg(),
            .bg = m_term->defcs()};

    auto& cursor = m_term->cursor();

    m_lastcur.col = std::clamp(m_lastcur.col, 0, m_term->cols() - 1);
    m_lastcur.row = std::clamp(m_lastcur.row, 0, m_term->rows() - 1);

    int curcol = cursor.col;

    // adjust position if in dummy
    if (m_term->glyph(m_lastcur).attr.wdummy)
        m_lastcur.col--;
    if (m_term->glyph({cursor.row, curcol}).attr.wdummy)
        curcol--;

    auto& sel = m_term->sel();
    bool ena_sel = !sel.empty() &&
                   sel.alt == m_term->mode()[term::MODE_ALTSCREEN];

    // remove the old cursor
    // making a copy, because we want to reverse it if it's
    // selected, without modifying the original
    screen::Glyph og = m_term->glyph(m_lastcur);
    if (ena_sel && sel.selected(m_lastcur))
        og.attr.reverse ^= 1;
    drawglyph(cr, layout, og, m_lastcur);

    auto& oldg = m_term->glyph(cursor);
    g.u = oldg.u;
    g.attr.bold = oldg.attr.bold;
    g.attr.italic = oldg.attr.italic;
    g.attr.underline = oldg.attr.underline;
    g.attr.struck = oldg.attr.struck;

    // select the right color for the right mode.
    uint32_t drawcol;
    if (m_term->mode()[term::MODE_REVERSE]) {
        g.attr.reverse = 1;
        g.bg = m_term->deffg();
        if (ena_sel && sel.selected(cursor)) {
            drawcol = m_term->defcs();
            g.fg = m_term->defrcs();
        } else {
            drawcol = m_term->defrcs();
            g.fg = m_term->defcs();
        }
    } else {
        if (ena_sel && sel.selected(cursor)) {
            drawcol = m_term->defrcs();
            g.fg = m_term->deffg();
            g.bg = m_term->defrcs();
        } else {
            drawcol = m_term->defcs();
        }
    }

    if (m_term->mode()[term::MODE_HIDE])
        return;

    // draw the new one
    if (m_term->focused()) {
        switch (m_term->cursortype()) {
            case screen::cursor_type::CURSOR_BLINK_BLOCK:
                if (m_term->mode()[term::MODE_BLINK])
                    break;
                [[fallthrough]];
            case screen::cursor_type::CURSOR_STEADY_BLOCK:
                g.attr.wide = m_term->glyph({cursor.row, curcol})
                                      .attr.wide;
                drawglyph(cr, layout, g, cursor);
                break;
            case screen::cursor_type::CURSOR_BLINK_UNDER:
                if (m_term->mode()[term::MODE_BLINK])
                    break;
                [[fallthrough]];
            case screen::cursor_type::CURSOR_STEADY_UNDER: {
                int cursor_thickness = get_cursor_thickness();
                cr.setSourceColor(drawcol);
                cr.rectangle(
                        m_border_px + curcol * m_cw,
                        m_border_px + (cursor.row + 1) * m_ch - cursor_thickness,
                        m_cw,
                        cursor_thickness);
                cr.fill();
            } break;
            case screen::cursor_type::CURSOR_BLINK_BAR:
                if (m_term->mode()[term::MODE_BLINK])
                    break;
                [[fallthrough]];
            case screen::cursor_type::CURSOR_STEADY_BAR: {
                int cursor_thickness = get_cursor_thickness();
                cr.setSourceColor(drawcol);
                cr.rectangle(
                        m_border_px + curcol * m_cw,
                        m_border_px + cursor.row * m_ch,
                        cursor_thickness,
                        m_ch);
                cr.fill();
            } break;
        }
    } else {
        cr.setSourceColor(drawcol);
        cr.rectangle(
                m_border_px + curcol * m_cw + 0.5,
                m_border_px + cursor.row * m_ch + 0.5,
                m_cw - 1,
                m_ch - 1);
        cr.stroke();
    }

    m_lastcur = {cursor.row, curcol};
}

void RendererImpl::load_font(Context& cr)
{
    auto L = rwte->lua();
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
    PangoFontMap* fontmap = pango_cairo_font_map_get_default();
    PangoLanguage* lang = pango_language_get_default();

    // measure font
    PangoContext* context = pango_cairo_create_context(cr.get());
    PangoFont* font = pango_font_map_load_font(fontmap, context, m_fontdesc);
    PangoFontMetrics* metrics = pango_font_get_metrics(font, lang);
    m_cw = std::ceil(
            pango_font_metrics_get_approximate_char_width(metrics) /
            (float) PANGO_SCALE * cw_scale);
    // height should equal ascent + descend, but the pango docs say height
    // can be zero, implying that the values may not be directly connected.
    // let's just take the max and not worry about it
    m_ch = std::ceil(
            std::max(pango_font_metrics_get_height(metrics),
                    (pango_font_metrics_get_ascent(metrics) +
                            pango_font_metrics_get_descent(metrics))) /
            (float) PANGO_SCALE * ch_scale);

    if (LOGGER()->level() <= rw::logging::log_level::debug) {
        char* font = pango_font_description_to_string(m_fontdesc);
        LOGGER()->debug("loaded {}, font size {}x{}", font, m_cw, m_ch);
        g_free(font);
    }

    pango_font_metrics_unref(metrics);
    g_object_unref(font);
    g_object_unref(context);
}

Renderer::Renderer(term::Term* term) :
    impl(std::make_unique<RendererImpl>(term))
{}

Renderer::~Renderer() = default;

void Renderer::load_font(cairo_surface_t* root_surface)
{
    impl->load_font(root_surface);
}

void Renderer::set_surface(cairo_surface_t* surface, int width, int height)
{
    impl->set_surface(surface, width, height);
}

void Renderer::resize(int width, int height)
{
    impl->resize(width, height);
}

int Renderer::charwidth() const
{
    return impl->charwidth();
}

int Renderer::charheight() const
{
    return impl->charheight();
}

void Renderer::drawregion(const Cell& begin, const Cell& end)
{
    impl->drawregion(begin, end);
}

Cell Renderer::pxtocell(int x, int y) const
{
    return impl->pxtocell(x, y);
}

} // namespace renderer
