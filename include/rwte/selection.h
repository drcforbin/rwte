#ifndef RWTE_SELECTION_H
#define RWTE_SELECTION_H

#include "rwte/coords.h"

#include <chrono>
#include <memory>
#include <time.h>

class Selection
{
public:
    enum class Mode
    {
        Idle = 0,
        Empty = 1,
        Ready = 2
    };

    enum class Snap
    {
        None = 0,
        Word = 1,
        Line = 2
    };

    void clear();
    void begin(const Cell& cell);
    bool empty() const;
    bool anyselected(const Cell& begin, const Cell& end) const;
    bool selected(const Cell& cell) const;

    void setmode(Mode val) { m_mode = val; }
    Mode mode() const { return m_mode; }

    void setrectangular(bool val) { m_rectangular = val; }
    bool rectangular() const { return m_rectangular; }

    Snap snap = Snap::None;

    // Selection variables:
    // nb – normalized coordinates of the beginning of the selection
    // ne – normalized coordinates of the end of the selection
    // ob – original coordinates of the beginning of the selection
    // oe – original coordinates of the end of the selection
    Cell nb{0, 0};
    Cell ne{0, 0};
    Cell ob{0, -1};
    Cell oe{0, 0};

    std::shared_ptr<char> primary;
    std::shared_ptr<char> clipboard;

    bool alt = false;
    std::chrono::time_point<std::chrono::steady_clock> tclick1 = {};
    std::chrono::time_point<std::chrono::steady_clock> tclick2 = {};

private:
    Mode m_mode = Mode::Idle;
    bool m_rectangular = false;
};

#endif // RWTE_SELECTION_H
