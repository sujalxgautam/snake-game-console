// =============================================================================
//  main.cpp — Snake (Console Edition) — polished
// =============================================================================
//
//  PROJECT BRIEF
//  -------------
//  Classic Snake on a terminal grid with the polish a real game has:
//  a start menu, difficulty selection (Speed Ramp / Classic / Zen),
//  colored block graphics, a proper bordered board, persistent high score,
//  and a clean game-over menu.
//
//  Steer with WASD, eat the food, grow longer, don't hit a wall or yourself.
//  Score = food eaten.
//
//  DIFFICULTY : Easy baseline + Medium polish
//  STANDARD    : C++17
//  DEPENDENCIES: Standard library only.
//  BUILD       : g++ -std=c++17 -O2 -pthread main.cpp -o snake
//  RUN         : ./snake
//
//  FEATURES
//  --------
//    * Start menu with mode selection:
//        - Speed Ramp: tick interval shrinks as you grow
//        - Classic: fixed 8 Hz pace
//        - Zen: wrap-around walls, no wall death
//    * High score persisted to $HOME/.snake_highscore
//    * Colored solid-block snake: bright head (█), darker body (▓)
//    * Red diamond food (◆)
//    * Bordered board with Unicode box drawing
//    * Countdown 3-2-1-GO! before each round
//    * Grace period after GO so you're not instantly dead
//    * Game-over menu (Restart / Menu / Quit)
//    * Full terminal clear at startup (clean canvas on launch)
//
//  A NOTE ON SNAKE APPEARANCE
//  --------------------------
//  Terminal cells are ~2:1 tall:wide, so a solid block (█) is taller than
//  it is wide. A vertical run of blocks looks like a slender column; a
//  horizontal run looks like a thicker stripe. This is INHERENT to the
//  medium and every classic terminal Snake has the same look. Do not
//  "fix" it by switching to thin line glyphs (─ │) — that makes the snake
//  look like a wire, not a snake. Keep the solid blocks.
//
//  COMMON PITFALLS
//  ---------------
//    * vector for the snake -> O(n) front-pop; use deque.
//    * Applying turns immediately -> reverse into your own neck.
//    * Forgetting to reject reversals.
//    * Spawning food on the snake -> infinite retry.
//    * Growing: on eat, DON'T pop the tail that tick.
//    * Persisting a file relative to CWD -> use HOME.
//    * Forgetting ANSI RESET -> terminal stays colored after exit.
//    * Replacing blocks with thin lines -> snake becomes a dot. Don't.
//    * Using render::clearScreen() at startup -> only moves cursor home,
//      does NOT wipe prior terminal content. Use color::clearScreen().
//
//  LICENSE: MIT.
// =============================================================================

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include <conio.h>
    #define SNAKE_WINDOWS 1
#else
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define SNAKE_WINDOWS 0
#endif

// =============================================================================
//  ANSI color helpers
// =============================================================================
namespace color {
    constexpr const char* kReset      = "\033[0m";
    constexpr const char* kBold       = "\033[1m";
    constexpr const char* kDim        = "\033[2m";
    constexpr const char* kRed        = "\033[31m";
    constexpr const char* kGreen      = "\033[32m";
    constexpr const char* kYellow     = "\033[33m";
    constexpr const char* kBlue       = "\033[34m";
    constexpr const char* kMagenta    = "\033[35m";
    constexpr const char* kCyan       = "\033[36m";
    constexpr const char* kWhite      = "\033[37m";
    constexpr const char* kBoldRed    = "\033[1;31m";
    constexpr const char* kBoldGreen  = "\033[1;32m";
    constexpr const char* kBoldYellow = "\033[1;33m";
    constexpr const char* kBoldCyan   = "\033[1;36m";
    constexpr const char* kBoldWhite  = "\033[1;37m";

    inline void clearScreen() { std::cout << "\033[H\033[2J"; }
}

// =============================================================================
//  Config
// =============================================================================
namespace cfg {
    constexpr int    kWidth            = 32;
    constexpr int    kHeight           = 18;
    constexpr int    kStartLength      = 3;

    constexpr double kBaseTickSeconds  = 1.0 / 8.0;
    constexpr double kFastestTick      = 1.0 / 18.0;
    constexpr int    kGraceTicks       = 8;
    constexpr int    kCountdownTicks   = 60;
}

// =============================================================================
//  Types
// =============================================================================
enum class Direction { Up, Down, Left, Right };
enum class GameState { Menu, Countdown, Playing, GameOver };
enum class Mode      { SpeedRamp, Classic, Zen };

struct Point {
    int x = 0;
    int y = 0;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

struct Game {
    GameState state = GameState::Menu;
    Mode      mode  = Mode::Classic;

    std::deque<Point>    snake;
    Point                food;
    Direction            dir         = Direction::Right;
    std::optional<Direction> pendingTurn;

    std::mt19937 rng{std::random_device{}()};

    int    score              = 0;
    int    best               = 0;

    int    graceTicksLeft     = 0;
    int    countdownTicksLeft = 0;
    double currentTick        = cfg::kBaseTickSeconds;

    int width  = cfg::kWidth;
    int height = cfg::kHeight;
};

// =============================================================================
//  High score persistence
// =============================================================================
namespace highscore {
    inline std::string path() {
        const char* home = std::getenv("HOME");
        if (home && *home) return std::string(home) + "/.snake_highscore";
#if defined(_WIN32)
        const char* up = std::getenv("USERPROFILE");
        if (up && *up) return std::string(up) + "\\.snake_highscore";
#endif
        return "snake_highscore.txt";
    }

    inline int load() {
        std::ifstream in(path());
        int v = 0;
        if (in >> v) return v;
        return 0;
    }

    inline void save(int v) {
        std::ofstream out(path(), std::ios::trunc);
        if (out) out << v << "\n";
    }
}

// =============================================================================
//  Input — non-blocking poll
// =============================================================================
namespace input {

#if !SNAKE_WINDOWS
struct RawMode {
    termios saved{};
    bool    active = false;
    void enable() {
        if (active) return;
        if (tcgetattr(STDIN_FILENO, &saved) != 0) return;
        termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        active = true;
    }
    void disable() {
        if (!active) return;
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        active = false;
    }
    ~RawMode() { disable(); }
};

inline std::optional<char> pollKey() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) == 1) return c;
    return std::nullopt;
}
#else
inline std::optional<char> pollKey() {
    if (_kbhit()) return static_cast<char>(_getch());
    return std::nullopt;
}
#endif

} // namespace input

// =============================================================================
//  Logic
// =============================================================================
namespace logic {

inline bool isOpposite(Direction a, Direction b) {
    return (a == Direction::Up    && b == Direction::Down)  ||
           (a == Direction::Down  && b == Direction::Up)    ||
           (a == Direction::Left  && b == Direction::Right) ||
           (a == Direction::Right && b == Direction::Left);
}

inline bool onSnake(const Game& g, Point p, bool ignoreTail = false) {
    const std::size_t end = ignoreTail && !g.snake.empty()
                          ? g.snake.size() - 1
                          : g.snake.size();
    for (std::size_t i = 0; i < end; ++i)
        if (g.snake[i] == p) return true;
    return false;
}

inline void placeFood(Game& g) {
    std::vector<Point> freeCells;
    freeCells.reserve(static_cast<std::size_t>(g.width * g.height));
    for (int y = 0; y < g.height; ++y)
        for (int x = 0; x < g.width; ++x) {
            Point p{x, y};
            if (!onSnake(g, p)) freeCells.push_back(p);
        }
    if (freeCells.empty()) {
        g.state = GameState::GameOver;
        return;
    }
    std::uniform_int_distribution<std::size_t> pick(0, freeCells.size() - 1);
    g.food = freeCells[pick(g.rng)];
}

inline void resetBoard(Game& g) {
    g.score          = 0;
    g.dir            = Direction::Right;
    g.pendingTurn.reset();
    g.snake.clear();
    g.currentTick    = cfg::kBaseTickSeconds;

    const int cy = g.height / 2;
    const int cx = g.width  / 2;
    for (int i = 0; i < cfg::kStartLength; ++i)
        g.snake.push_back(Point{cx - i, cy});

    placeFood(g);
}

inline void startCountdown(Game& g, Mode m) {
    g.state              = GameState::Countdown;
    g.mode               = m;
    g.countdownTicksLeft = cfg::kCountdownTicks * 3;
    g.graceTicksLeft     = 0;
    resetBoard(g);
}

inline void beginPlaying(Game& g) {
    g.state          = GameState::Playing;
    g.graceTicksLeft = cfg::kGraceTicks;
}

inline void queueTurn(Game& g, Direction d) {
    g.pendingTurn = d;
}

inline void recomputeTick(Game& g) {
    if (g.mode != Mode::SpeedRamp) {
        g.currentTick = cfg::kBaseTickSeconds;
        return;
    }
    const int steps = g.score / 3;
    double t = cfg::kBaseTickSeconds - steps * (1.0 / 24.0);
    if (t < cfg::kFastestTick) t = cfg::kFastestTick;
    g.currentTick = t;
}

inline void step(Game& g) {
    if (g.state == GameState::Countdown) {
        g.countdownTicksLeft -= 1;
        if (g.countdownTicksLeft <= 0) beginPlaying(g);
        return;
    }
    if (g.state != GameState::Playing) return;

    if (g.graceTicksLeft > 0) {
        g.graceTicksLeft -= 1;
        return;
    }

    if (g.pendingTurn) {
        if (!isOpposite(*g.pendingTurn, g.dir)) g.dir = *g.pendingTurn;
        g.pendingTurn.reset();
    }

    Point head = g.snake.front();
    switch (g.dir) {
        case Direction::Up:    --head.y; break;
        case Direction::Down:  ++head.y; break;
        case Direction::Left:  --head.x; break;
        case Direction::Right: ++head.x; break;
    }

    if (g.mode == Mode::Zen) {
        if (head.x < 0)         head.x = g.width  - 1;
        if (head.x >= g.width)  head.x = 0;
        if (head.y < 0)         head.y = g.height - 1;
        if (head.y >= g.height) head.y = 0;
    } else {
        if (head.x < 0 || head.x >= g.width ||
            head.y < 0 || head.y >= g.height) {
            g.state = GameState::GameOver;
            return;
        }
    }

    if (onSnake(g, head, /*ignoreTail=*/true)) {
        g.state = GameState::GameOver;
        return;
    }

    const bool ate = (head == g.food);

    g.snake.push_front(head);
    if (!ate) {
        g.snake.pop_back();
    } else {
        g.score += 1;
        if (g.score > g.best) {
            g.best = g.score;
            highscore::save(g.best);
        }
        placeFood(g);
        recomputeTick(g);
    }
}

} // namespace logic

// =============================================================================
//  Rendering — solid block snake. The horizontal-vs-vertical thickness
//  difference is inherent to the terminal cell aspect ratio; every classic
//  console Snake uses solid blocks, and so do we. Do not replace with thin
//  line glyphs (─ │) — the snake stops looking like a snake.
// =============================================================================
namespace render {

using namespace color;

inline void clearScreen() { std::cout << "\033[H"; }
inline void hideCursor(bool hide) { std::cout << (hide ? "\033[?25l" : "\033[?25h"); }

inline const char* modeName(Mode m) {
    switch (m) {
        case Mode::SpeedRamp: return "Speed Ramp";
        case Mode::Classic:   return "Classic";
        case Mode::Zen:       return "Zen";
    }
    return "?";
}

inline std::string menuScreen(const Game& g) {
    std::ostringstream out;
    out << "\n\n"
        << kBoldGreen
        << "        ███████╗███╗   ██╗ █████╗ ██╗  ██╗███████╗\n"
        << "        ██╔════╝████╗  ██║██╔══██╗██║ ██╔╝██╔════╝\n"
        << "        ███████╗██╔██╗ ██║███████║█████╔╝ █████╗  \n"
        << "        ╚════██║██║╚██╗██║██╔══██║██╔═██╗ ██╔══╝  \n"
        << "        ███████║██║ ╚████║██║  ██║██║  ██╗███████╗\n"
        << "        ╚══════╝╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝\n"
        << kReset << "\n"
        << kDim << "                   · · ·  console edition  · · ·\n" << kReset << "\n\n";

    out << "   " << kBold << "[1]" << kReset << "  Speed Ramp  "
        << kDim << "(speeds up as you eat)" << kReset << "\n"
        << "   " << kBold << "[2]" << kReset << "  Classic     "
        << kDim << "(fixed 8 Hz pace)" << kReset << "\n"
        << "   " << kBold << "[3]" << kReset << "  Zen         "
        << kDim << "(wrap-around walls, relaxing)" << kReset << "\n"
        << "   " << kBold << "[Q]" << kReset << "  Quit\n\n";

    if (g.best > 0)
        out << "   " << kDim << "Best: " << kBoldYellow << g.best
            << kReset << "\n";

    out << "\n   " << kCyan << "Choice: " << kReset;
    return out.str();
}

inline std::string countdownText(const Game& g) {
    const int perDigit = cfg::kCountdownTicks;
    const int t = g.countdownTicksLeft;
    if (t > perDigit * 2)  return "3";
    if (t > perDigit)      return "2";
    if (t > 1)             return "1";
    return "GO!";
}

inline std::string frame(const Game& g) {
    std::vector<std::string> grid(g.height, std::string(g.width, ' '));

    if (g.food.x >= 0 && g.food.x < g.width &&
        g.food.y >= 0 && g.food.y < g.height)
        grid[g.food.y][g.food.x] = '*';

    for (std::size_t i = 1; i < g.snake.size(); ++i) {
        const auto& p = g.snake[i];
        if (p.x >= 0 && p.x < g.width && p.y >= 0 && p.y < g.height)
            grid[p.y][p.x] = 'o';
    }
    if (!g.snake.empty()) {
        const auto& h = g.snake.front();
        if (h.x >= 0 && h.x < g.width && h.y >= 0 && h.y < g.height)
            grid[h.y][h.x] = 'H';
    }

    std::ostringstream out;
    out << "\n";

    out << kDim << "┌" << std::string(g.width, '─') << "┐" << kReset << "\n";

    for (int y = 0; y < g.height; ++y) {
        out << kDim << "│" << kReset;
        for (int x = 0; x < g.width; ++x) {
            const char c = grid[y][x];
            if (c == 'H')      out << kBoldGreen << "█" << kReset;
            else if (c == 'o') out << kGreen     << "▓" << kReset;
            else if (c == '*') out << kBoldRed   << "◆" << kReset;
            else               out << ' ';
        }
        out << kDim << "│" << kReset << "\n";
    }

    out << kDim << "└" << std::string(g.width, '─') << "┘" << kReset << "\n";

    out << "  " << kDim << "Score: " << kReset << kBoldWhite << g.score << kReset
        << kDim << "    Best: " << kReset << kBoldYellow << g.best << kReset
        << kDim << "    Mode: " << kReset << kCyan << modeName(g.mode) << kReset << "\n";

    if (g.state == GameState::Countdown) {
        out << "\n  " << kBoldYellow << "      " << countdownText(g) << kReset << "\n";
        out << "  " << kDim << "(Get ready — snake moves after GO!)" << kReset << "\n";
    } else if (g.state == GameState::Playing) {
        if (g.graceTicksLeft > 0)
            out << "  " << kDim << "(starting...)" << kReset << "\n";
        else
            out << "  " << kDim << "[WASD] turn   [Q] menu" << kReset << "\n";
    } else if (g.state == GameState::GameOver) {
        out << "\n  " << kBoldRed << "══════ GAME OVER ══════" << kReset << "\n"
            << "\n  " << kBold << "[R]" << kReset << " Restart   "
            << kBold << "[M]" << kReset << " Menu   "
            << kBold << "[Q]" << kReset << " Quit\n"
            << "  " << kCyan << "Choice: " << kReset;
    }
    return out.str();
}

} // namespace render

// =============================================================================
//  main
// =============================================================================
int main() {
    using clock = std::chrono::steady_clock;

#if !SNAKE_WINDOWS
    input::RawMode raw;
    raw.enable();
#endif
    render::hideCursor(true);

    // Full terminal clear at startup — matches Tic-Tac-Toe's behavior.
    // NOTE: render::clearScreen() only moves the cursor home; it does NOT
    // wipe prior terminal content. color::clearScreen() does a full clear.
    // Use the full clear here, once, at startup.
    color::clearScreen();

    Game game;
    game.best = highscore::load();

    auto   last        = clock::now();
    double accumulator = 0.0;
    bool   running     = true;

    while (running) {
        // --- Input --------------------------------------------------------
        while (auto c = input::pollKey()) {
            const char ch = *c;

            if ((ch == 'q' || ch == 'Q') &&
                (game.state == GameState::Menu || game.state == GameState::GameOver)) {
                running = false;
                break;
            }

            switch (game.state) {
                case GameState::Menu:
                    if (ch == '1') logic::startCountdown(game, Mode::SpeedRamp);
                    if (ch == '2') logic::startCountdown(game, Mode::Classic);
                    if (ch == '3') logic::startCountdown(game, Mode::Zen);
                    if (ch == 'q' || ch == 'Q') running = false;
                    break;

                case GameState::Countdown:
                    break;

                case GameState::Playing:
                    switch (ch) {
                        case 'w': case 'W': logic::queueTurn(game, Direction::Up);    break;
                        case 's': case 'S': logic::queueTurn(game, Direction::Down);  break;
                        case 'a': case 'A': logic::queueTurn(game, Direction::Left);  break;
                        case 'd': case 'D': logic::queueTurn(game, Direction::Right); break;
                        case 'q': case 'Q': game.state = GameState::Menu;             break;
                        default: break;
                    }
                    break;

                case GameState::GameOver:
                    if (ch == 'r' || ch == 'R') logic::startCountdown(game, game.mode);
                    if (ch == 'm' || ch == 'M') game.state = GameState::Menu;
                    if (ch == 'q' || ch == 'Q') running = false;
                    break;
            }
        }
        if (!running) break;

        // --- Fixed timestep ------------------------------------------------
        const auto now = clock::now();
        accumulator += std::chrono::duration<double>(now - last).count();
        last = now;
        accumulator = std::min(accumulator, 0.25);

        if (game.state == GameState::Countdown) {
            while (accumulator >= 1.0 / 60.0) {
                logic::step(game);
                accumulator -= 1.0 / 60.0;
            }
        } else if (game.state == GameState::Playing) {
            while (accumulator >= game.currentTick) {
                logic::step(game);
                accumulator -= game.currentTick;
            }
        }

        // --- Render --------------------------------------------------------
        render::clearScreen();
        if (game.state == GameState::Menu) {
            std::cout << render::menuScreen(game) << std::flush;
        } else {
            std::cout << render::frame(game) << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    render::hideCursor(false);
    std::cout << "\n" << color::kBoldGreen
              << "Thanks for playing!\n" << color::kReset;
    return 0;
}