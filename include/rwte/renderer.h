#ifndef RWTE_RENDERER_H
#define RWTE_RENDERER_H

#include <memory>

namespace term {
class Term;
}

struct _cairo_surface;
typedef _cairo_surface cairo_surface_t;
struct Cell;

namespace renderer {

class RendererImpl;

class Renderer
{
public:
    Renderer(term::Term* term);
    ~Renderer();

    void load_font(cairo_surface_t* root_surface);

    /// \brief Sets the surface the renderer should render to.
    ///
    /// To reset the surface, nullptr may be passed. Note that the renderer
    /// will assume ownership of the surface.
    void set_surface(cairo_surface_t* surface, int width, int height);

    void resize(int width, int height);

    int charwidth() const;
    int charheight() const;

    // note: excludes end
    void drawregion(const Cell& begin, const Cell& end);

    Cell pxtocell(int x, int y) const;

private:
    std::unique_ptr<RendererImpl> impl;
};

} // namespace renderer

#endif // RWTE_RENDERER_H
