#include "doctest.h"
#include "fmt/format.h"
#include "rwte/screen.h"

class ScreenFixture
{
public:
    ScreenFixture() :
        bus(std::make_shared<event::Bus>()),
        screen{bus}
    {
        // fill a screen with known data
        screen.resize(initial_cols, initial_rows);
        // todo: seems like this should be part of resize
        screen.setscroll(0, initial_rows - 1);

        for (auto& line : screen.lines()) {
            for (auto& g : line) {
                g = initial_fill;
            }
        }

        screen::Cursor c{};
        c.attr = second_fill;
        screen.setCursor(c);
    }

protected:
    std::shared_ptr<event::Bus> bus;
    screen::Screen screen;
    static const int initial_rows;
    static const int initial_cols;
    static const screen::Glyph initial_fill;
    static const screen::Glyph second_fill;
};

const int ScreenFixture::initial_rows = 5;
const int ScreenFixture::initial_cols = 6;

const screen::Glyph ScreenFixture::initial_fill{
        32655,
        {.bold = 1},
        982374,
        8758};

const screen::Glyph ScreenFixture::second_fill{
        3423,
        {.faint = 1},
        8474,
        2897};

class ScreenFixtureVarying : public ScreenFixture
{
public:
    ScreenFixtureVarying()
    {
        Cell cell;
        for (cell.row = 0; cell.row < initial_rows; cell.row++) {
            for (cell.col = 0; cell.col < initial_cols; cell.col++) {
                screen.glyph(cell) = glyphForCell(cell);
            }
        }
    }

protected:
    screen::Glyph glyphForCell(const Cell& cell)
    {
        // todo: review bitset constructor usage...
        // this should be calling the ulonglong ctor,
        // but we might be misusing it elsewhere

        // return a glyph encoding cell and field number
        uint32_t magic = (cell.row << 10) | (cell.col << 2);
        return {
                magic | 0,
                {},
                magic | 2,
                magic | 3};
    }

    void checkMotion(const std::vector<Cell> sources)
    {
        // iterate over each location in screen. values dir will
        // contain offsets to where the current value of a cell
        // came from. if {0,0}, the target value should be
        // unchanged; example, if the contents of a cell were
        // copied from the cell two right and one up, dir will
        // contain {-1, 2}. offsets outside the screen indicate
        // cleared cells

        auto source = sources.cbegin();
        Cell cell;
        /*
        for (cell.row = 0; cell.row < screen.rows(); cell.row++) {
            for (cell.col = 0; cell.col < screen.cols(); cell.col++) {
                const auto actual = screen.glyph(cell);
                std::cout << "{"
                    << ((actual.u >> 10) & 0xFF) << ","
                    << ((actual.u >> 2) & 0xFF) << "}";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
        */
        for (cell.row = 0; cell.row < screen.rows(); cell.row++) {
            for (cell.col = 0; cell.col < screen.cols(); cell.col++) {
                const auto actual = screen.glyph(cell);

                screen::Glyph expected{};
                Cell from{cell.row + source->row, cell.col + source->col};
                if (0 <= from.row && from.row < screen.rows() &&
                        0 <= from.col && from.col < screen.cols()) {
                    // source is in screen; get it.
                    expected = glyphForCell(from);
                } else {
                    const auto& c = screen.cursor();
                    expected.u = screen::empty_char;
                    expected.fg = c.attr.fg;
                    expected.bg = c.attr.bg;
                }

                INFO("cell {"
                        << cell.row << "," << cell.col
                        << "} should be copied from {"
                        << from.row << "," << from.col
                        << "}; got value from {"
                        << ((actual.u >> 10) & 0xFF) << ","
                        << ((actual.u >> 2) & 0xFF) << "}");
                REQUIRE(actual.u == expected.u);
                REQUIRE(actual.attr == expected.attr);
                REQUIRE(actual.fg == expected.fg);
                REQUIRE(actual.bg == expected.bg);

                source++;
            }
        }

        REQUIRE(source == sources.cend());
    }
};

TEST_SUITE_BEGIN("screen");

TEST_CASE_FIXTURE(ScreenFixture, "resize resizes")
{
    SUBCASE("initial resize works")
    {
        REQUIRE(screen.cols() == initial_cols);
        REQUIRE(screen.rows() == initial_rows);

        Cell cell;
        for (cell.row = 0; cell.row < initial_rows; cell.row++) {
            for (cell.col = 0; cell.col < initial_cols; cell.col++) {
                const auto g = screen.glyph(cell);

                REQUIRE(g.u == this->initial_fill.u);
                REQUIRE(g.attr == this->initial_fill.attr);
                REQUIRE(g.fg == this->initial_fill.fg);
                REQUIRE(g.bg == this->initial_fill.bg);
            }
        }

        // explicitly count cells
        int count = 0;
        for (auto& line : screen.lines()) {
            count += line.size();
        }
        REQUIRE(count == 30);
    }

    SUBCASE("resize smaller works")
    {
        screen.resize(3, 2);
        REQUIRE(screen.cols() == 3);
        REQUIRE(screen.rows() == 2);

        Cell cell;
        for (cell.row = 0; cell.row < 2; cell.row++) {
            for (cell.col = 0; cell.col < 3; cell.col++) {
                const auto g = screen.glyph(cell);

                REQUIRE(g.u == this->initial_fill.u);
                REQUIRE(g.attr == this->initial_fill.attr);
                REQUIRE(g.fg == this->initial_fill.fg);
                REQUIRE(g.bg == this->initial_fill.bg);
            }
        }

        // explicitly count cells
        int count = 0;
        for (auto& line : screen.lines()) {
            count += line.size();
        }
        REQUIRE(count == 6);
    }

    SUBCASE("resize larger works")
    {
        screen.resize(9, 10);
        REQUIRE(screen.cols() == 9);
        REQUIRE(screen.rows() == 10);

        const screen::glyph_attribute empty_attr{};

        Cell cell;
        for (cell.row = 0; cell.row < 10; cell.row++) {
            for (cell.col = 0; cell.col < 9; cell.col++) {
                const auto g = screen.glyph(cell);

                if (cell.row < initial_rows && cell.col < initial_cols) {
                    REQUIRE(g.u == this->initial_fill.u);
                    REQUIRE(g.attr == this->initial_fill.attr);
                    REQUIRE(g.fg == this->initial_fill.fg);
                    REQUIRE(g.bg == this->initial_fill.bg);
                } else {
                    REQUIRE(g.u == screen::empty_char);
                    REQUIRE(g.attr == empty_attr);
                    REQUIRE(g.fg == 0);
                    REQUIRE(g.bg == 0);
                }
            }
        }

        // explicitly count cells
        int count = 0;
        for (auto& line : screen.lines()) {
            count += line.size();
        }
        REQUIRE(count == 90);
    }
}

TEST_CASE_FIXTURE(ScreenFixture, "clear clears")
{
    auto checkRange = [this](
                              int row1, int col1, int row2, int col2) {
        const screen::glyph_attribute empty_attr{};
        Cell cell;
        for (cell.row = 0; cell.row < this->screen.rows(); cell.row++) {
            for (cell.col = 0; cell.col < this->screen.cols(); cell.col++) {
                const auto g = this->screen.glyph(cell);

                // clear includes the end row/col, we do too
                if (row1 <= cell.row && cell.row <= row2 &&
                        col1 <= cell.col && cell.col <= col2) {
                    REQUIRE(g.u == screen::empty_char);
                    REQUIRE(g.attr == empty_attr);
                    REQUIRE(g.fg == this->second_fill.fg);
                    REQUIRE(g.bg == this->second_fill.bg);
                } else {
                    REQUIRE(g.u == this->initial_fill.u);
                    REQUIRE(g.attr == this->initial_fill.attr);
                    REQUIRE(g.fg == this->initial_fill.fg);
                    REQUIRE(g.bg == this->initial_fill.bg);
                }
            }
        }
    };

    SUBCASE("clear all sets to cursor")
    {
        screen.clear();
        checkRange(0, 0, initial_rows - 1, initial_cols - 1);
    }

    SUBCASE("clear sets top left to cursor")
    {
        screen.clear({0, 0}, {2, 2});
        checkRange(0, 0, 2, 2);
    }

    SUBCASE("clear sets middle to cursor")
    {
        screen.clear({1, 1}, {3, 3});
        checkRange(1, 1, 3, 3);
    }

    SUBCASE("clear sets bottom right to cursor")
    {
        screen.clear({2, 3}, {initial_rows - 1, initial_cols - 1});
        checkRange(2, 3, initial_rows - 1, initial_cols - 1);
    }

    SUBCASE("clear handles overflow top left")
    {
        screen.clear({-6, -4}, {2, 2});
        checkRange(0, 0, 2, 2);
    }

    SUBCASE("clear handles overflow bottom right")
    {
        screen.clear({2, 3}, {9, 10});
        checkRange(2, 3, initial_rows - 1, initial_cols - 1);
    }

    SUBCASE("clear handles complete overflow")
    {
        screen.clear({-6, -4}, {9, 10});
        checkRange(0, 0, initial_rows - 1, initial_cols - 1);
    }

    SUBCASE("clear handles missorted cols")
    {
        screen.clear({1, 3}, {3, 1});
        checkRange(1, 1, 3, 3);
    }

    SUBCASE("clear handles missorted rows")
    {
        screen.clear({3, 1}, {1, 3});
        checkRange(1, 1, 3, 3);
    }

    SUBCASE("clear handles missorted coords")
    {
        screen.clear({3, 3}, {1, 1});
        checkRange(1, 1, 3, 3);
    }
}

TEST_CASE_FIXTURE(ScreenFixtureVarying, "newline moves cursor down")
{
    SUBCASE("moves down when not on last line")
    {
        auto c = screen.cursor();
        c.row = 2, c.col = 3;
        screen.setCursor(c);

        // should move down, keeping same column, and not
        // modify the display
        screen.newline(false);
        auto c2 = screen.cursor();
        REQUIRE(c2.row == c.row + 1);
        REQUIRE(c2.col == c.col);

        // verify display unchanged
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}});

        // should move down again, reset to col 0, and not
        // modify the display
        screen.newline(true);
        c2 = screen.cursor();
        REQUIRE(c2.row == c.row + 2);
        REQUIRE(c2.col == 0);

        // verify display unchanged
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}});
    }

    SUBCASE("will scroll when on last line")
    {
        auto c = screen.cursor();
        c.row = screen.bot(), c.col = 3;
        screen.setCursor(c);

        // should keep the same position, but scroll display
        screen.newline(false);
        auto c2 = screen.cursor();
        REQUIRE(c2.row == c.row);
        REQUIRE(c2.col == c.col);

        // verify display scrolled 1
        checkMotion(
                {{1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
                        {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
                        {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
                        {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
                        {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}});

        // should keep the same row, reset to col 0, and scroll display
        screen.newline(true);
        c2 = screen.cursor();
        REQUIRE(c2.row == c.row);
        REQUIRE(c2.col == 0);

        // verify display scrolled another 1
        checkMotion(
                {{2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
                        {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
                        {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
                        {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
                        {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}});
    }
}

TEST_CASE_FIXTURE(ScreenFixtureVarying, "deleteline removes lines")
{
    SUBCASE("deletes one line")
    {
        auto c = screen.cursor();

        // delete one at the end
        c.row = initial_rows - 1, c.col = 3;
        screen.setCursor(c);
        screen.deleteline(1);

        // verify last line is missing (10 to guarantee off-screen)
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});

        // delete 3rd line
        c.row = 2, c.col = 0;
        screen.setCursor(c);
        screen.deleteline(1);

        // verify line 2 missing, and 3 is scrolled to its place
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});

        // delete 1st line
        c.row = 0, c.col = 2;
        screen.setCursor(c);
        screen.deleteline(1);

        // verify line 0 is now missing too, and rest scrolled up
        checkMotion(
                {{1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
                        {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});
    }

    SUBCASE("deletes multiple lines")
    {
        auto c = screen.cursor();

        // delete two in middle
        c.row = 2, c.col = 3;
        screen.setCursor(c);
        screen.deleteline(2);

        // verify line 2 and 3 missing, and 4 is scrolled to its place
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});

        // delete first two lines
        c.row = 0, c.col = 0;
        screen.setCursor(c);
        screen.deleteline(2);

        // verify line 2 and 3 missing, and 4 is scrolled to its place
        checkMotion(
                {{4, 0}, {4, 0}, {4, 0}, {4, 0}, {4, 0}, {4, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});
    }

    SUBCASE("deletes one at end on overflow")
    {
        auto c = screen.cursor();

        c.row = initial_rows - 1, c.col = 3;
        screen.setCursor(c);
        screen.deleteline(6);

        // verify last line is missing (10 to guarantee off-screen)
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});
    }
}

TEST_CASE_FIXTURE(ScreenFixtureVarying, "insertblankline adds lines")
{
    SUBCASE("inserts one line")
    {
        auto c = screen.cursor();

        // insert one at the beginning
        c.row = 0, c.col = 3;
        screen.setCursor(c);
        screen.insertblankline(1);

        // insert one at line 2
        c.row = 2, c.col = 3;
        screen.setCursor(c);
        screen.insertblankline(1);

        // insert one at line 4
        c.row = 4, c.col = 3;
        screen.setCursor(c);
        screen.insertblankline(1);

        // verify last line is missing (10 to guarantee off-screen)
        checkMotion(
                {{10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {-2, 0}, {-2, 0}, {-2, 0}, {-2, 0}, {-2, 0}, {-2, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});
    }

    SUBCASE("inserts multiple lines")
    {
        auto c = screen.cursor();

        // insert a couple in the middle
        c.row = 2, c.col = 3;
        screen.setCursor(c);
        screen.insertblankline(2);

        // verify last line is missing (10 to guarantee off-screen)
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0},
                        {-2, 0}, {-2, 0}, {-2, 0}, {-2, 0}, {-2, 0}, {-2, 0}});
    }

    SUBCASE("inserts one at end on overflow")
    {
        auto c = screen.cursor();

        c.row = initial_rows - 1, c.col = 3;
        screen.setCursor(c);
        screen.insertblankline(6);

        // verify last line is missing (10 to guarantee off-screen)
        checkMotion(
                {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                        {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}, {10, 0}});
    }
}

TEST_CASE_FIXTURE(ScreenFixtureVarying, "deletechar removes chars at cursor")
{
    // todo:
    // deletes zero
    // deletes one at first line
    // deletes one in middle
    // deletes one at end
    // deletes two at first line
    // deletes two in middle
    // deletes two at end
}

TEST_CASE_FIXTURE(ScreenFixtureVarying, "insertblank adds chars at cursor")
{
    // todo:
    // inserts zero
    // inserts one at first line
    // inserts one in middle
    // inserts one at end
    // inserts two at first line
    // inserts two in middle
    // inserts two at end
}

TEST_SUITE_END();
