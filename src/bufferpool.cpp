#include "rwte/bufferpool.h"
#include "rwte/logging.h"
#include "rwte/wayland.h"

#include <cairo/cairo.h>

#define LOGGER() (logging::get("bufferpool"))

namespace wlwin {

// we should really only need one buffer, right?
constexpr int NumBuffers = 2;

static int create_shm_file(off_t size)
{
    // nobody cares what it's called, we're going to
    // close it after we make the exchange
    int fd = memfd_create("", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, size) >= 0)
            return fd;

        close(fd);
    }

    return -1;
}

Buffer::Buffer(BufferPool* pool, wl_buffer* buffer, unsigned char* data,
        int width, int height, int stride) :
    Base(buffer),
    m_pool(pool),
    m_data(data),
    m_width(width),
    m_height(height),
    m_stride(stride)
{}

Buffer::~Buffer()
{
    if (m_data)
        munmap(m_data, m_stride * m_height);
}

void Buffer::handle_release()
{
    m_pool->release_buffer(this);
}

bool BufferPool::create_buffers(int width, int height)
{
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    int size = stride * height;

    for (int i = 0; i < NumBuffers; i++) {
        int fd = create_shm_file(size);
        if (fd < 0) {
            LOGGER()->fatal("creating a buffer fd failed for {}: {}\n",
                    size, strerror(errno));
            return false; // won't return
        }

        auto data = (unsigned char*) mmap(nullptr, size,
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
            LOGGER()->fatal("mmap failed for {}, {}", size, strerror(errno));
            close(fd); // won't get here, given fatal
            return false;
        }

        // todo: keep pool, for resizing smaller?
        wl_shm_pool* pool = wl_shm_create_pool(m_shm, fd, size);
        auto buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);

        if (buffer) {
            buffers.emplace_back(std::make_unique<Buffer>(
                    this, buffer, data, width, height, stride));
        } else {
            LOGGER()->fatal("unable to create buffer {}", i);
            return false;
        }
    }
    return true;
}

bool BufferPool::resize(int width, int height)
{
    buffers.clear();
    return create_buffers(width, height);
}

Buffer* BufferPool::get_buffer()
{
    for (auto& buffer : buffers) {
        if (!buffer->busy()) {
            buffer->setBusy(true);
            return buffer.get();
        }
    }

    LOGGER()->warn("all buffers busy!");
    return nullptr;
}

void BufferPool::release_buffer(Buffer* buffer)
{
    buffer->setBusy(false);
}

} // namespace wlwin
