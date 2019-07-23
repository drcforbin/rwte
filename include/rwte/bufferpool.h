#ifndef RWTE_BUFFERPOOL_H
#define RWTE_BUFFERPOOL_H

#include "rwte/wayland.h"

#include <memory>
#include <vector>

namespace wlwin {

class BufferPool;

class Buffer : public wayland::Buffer<Buffer> {
    using Base = wayland::Buffer<Buffer>;
public:
    Buffer(BufferPool *pool, wl_buffer *buffer, unsigned char *data,
            int width, int height, int stride);
    ~Buffer();

    // todo: implement in wayland::* classes
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    unsigned char *data() { return m_data; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int stride() const { return m_stride; }

    bool busy() const { return m_busy; }
    void setBusy(bool val) { m_busy = val; }

protected:
    friend class wayland::Buffer<Buffer>;

    void handle_release();

private:
    BufferPool *m_pool = nullptr;
    unsigned char *m_data = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
    bool m_busy = false;
};

class BufferPool
{
public:
    BufferPool(wl_shm *shm) :
        m_shm(shm)
    { }

    bool create_buffers(int width, int height);
    bool resize(int width, int height);

    Buffer* get_buffer();

protected:
    friend class Buffer;

    void release_buffer(Buffer *buffer);

private:
    struct wl_shm *m_shm;

    std::vector<std::unique_ptr<Buffer>> buffers;
};

} // namespace wlwin

#endif // RWTE_BUFFERPOOL_H
