#ifndef RWTE_RENDERER_H
#define RWTE_RENDERER_H

#include <memory>

// forwards
struct _cairo_surface;
typedef _cairo_surface cairo_surface_t;
struct Cell;

namespace renderer {

class RendererImpl;

class Renderer
{
public:
    Renderer();
    ~Renderer();

    void load_font(cairo_surface_t *root_surface);
    void set_surface(cairo_surface_t *surface, int width, int height);

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
