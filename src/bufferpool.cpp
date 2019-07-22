#include "rwte/bufferpool.h"
#include "rwte/logging.h"
#include "rwte/wayland.h"

#include <cairo/cairo.h>
#include <fcntl.h>

#define LOGGER() (logging::get("bufferpool"))

namespace wlwin {

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A'+(r&15)+(r&16)*2;
        r >>= 5;
    }
}

static int create_shm_file(off_t size) {
    char name[] = "/rwte-XXXXXX";
    int retries = 100;

    do {
        randname(name + strlen(name) - 6);

        --retries;
        // shm_open guarantees that O_CLOEXEC is set. if it works,
        // we can unlink it immediately; nobody cares about its name.
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            if (ftruncate(fd, size) < 0) {
                close(fd);
                return -1;
            }
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

// we should really only need one buffer, right?
const int NumBuffers = 2;

Buffer::Buffer(BufferPool *pool, wl_buffer *buffer, unsigned char *data,
        int width, int height, int stride) :
    Base(buffer),
    m_pool(pool),
    m_data(data),
    m_width(width), m_height(height),
    m_stride(stride)
{ }

Buffer::~Buffer()
{
    if (m_data)
        munmap(m_data, m_stride * m_height);
}

void Buffer::handle_release()
{
    m_pool->release_buffer(this);
}

bool BufferPool::create_buffers(int width, int height) {
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    int size = stride * height;

    for (int i = 0; i < NumBuffers; i++ ) {
        int fd = create_shm_file(size);
        if (fd < 0) {
            LOGGER()->fatal("creating a buffer file failed for {}: {}\n",
                    size, strerror(errno));
            return false; // won't return
        }

        auto data = (unsigned char *) mmap(nullptr, size,
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
            LOGGER()->fatal("mmap failed for {}, {}", size, strerror(errno));
            close(fd); // won't get here, given fatal
            return false;
        }

        // todo: keep pool, for resizing smaller?
        wl_shm_pool *pool = wl_shm_create_pool(m_shm, fd, size);
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

bool BufferPool::resize(int width, int height) {
    buffers.clear();
    return create_buffers(width, height);
}

Buffer* BufferPool::get_buffer() {
    for (auto& buffer : buffers) {
        if (!buffer->busy()) {
            buffer->setBusy(true);
            return buffer.get();
        }
    }

    LOGGER()->warn("all buffers busy!");
    return nullptr;
}

void BufferPool::release_buffer(Buffer *buffer) {
    buffer->setBusy(false);
}

} // namespace wlwin
