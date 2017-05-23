#ifndef RENDER_H
#define RENDER_H

#include <memory>

// forwards
struct _cairo_surface;
typedef _cairo_surface cairo_surface_t;

class RendererImpl;

class Renderer
{
public:
    Renderer(cairo_surface_t *surface, int width, int height);
    ~Renderer();

    void resize(int width, int height);

    int charwidth() const;
    int charheight() const;

    void drawregion(int row1, int col1, int row2, int col2);

    int x2col(int x) const;
    int y2row(int y) const;

private:
    std::unique_ptr<RendererImpl> impl;
};

#endif // RENDER_H
