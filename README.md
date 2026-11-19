# Snake — Console Edition (C++17)

Classic Snake on a terminal grid. Steer with WASD, eat the food, grow
longer, don't hit a wall or yourself. Score is the number of food items
eaten.

A deliberately minimal C++17 project — standard library only, no external
dependencies — built to teach the game-loop pattern, container choice,
and input buffering.

## Controls

| Key | Action |
|-----|--------|
| W / A / S / D | Move up / left / down / right |
| SPACE | Restart (on game over) |
| Q | Quit |

## Build & Run

### Quick build (g++)

```bash
g++ -std=c++17 -O2 -pthread main.cpp -o snake
./snake
```

### With CMake

```bash
cmake -B build
cmake --build build
./snake
```

## How it works

- **Fixed 8 Hz tick.** Snake *feels* turn-based; 60 Hz is frantic. The
  timestep is a design knob, not a technical constant.
- **The snake is a `std::deque<Point>`.** Head is pushed to the front,
  tail popped from the back. This is the O(1)-at-both-ends reason `deque`
  beats `vector` here.
- **Turns are buffered.** One pending turn is stored, applied on the next
  tick, and reversals (right → left) are rejected at apply time. Pressing
  two turns between ticks does not send the snake into its own neck.
- **Food spawns on a randomly chosen free cell.** If the board is full,
  the game ends — a win condition most Snake tutorials ignore.

## What this project teaches

- `std::deque` vs `std::vector` — container choice matters.
- Input buffering, and *why* immediate turns cause self-collision.
- Self-collision detection that correctly excludes the tail cell (it
  vacates this tick, so hitting it is not a collision).
- Graceful handling of the "board is full" terminal state.
- Cross-platform non-blocking keyboard input (raw mode on POSIX,
  `_getch()` on Windows).

## Requirements

- **Compiler:** any C++17 compiler (`g++`, `clang++`, MSVC)
- **Platform:** Linux, macOS, or Windows
- **Dependencies:** standard library only
- **Threads:** linked via `-pthread` (GCC/Clang) or `Threads::Threads` (CMake)

## Roadmap

- [ ] ANSI color and Unicode block glyphs
- [ ] Arrow-key input
- [ ] Wrap-around mode toggle
- [ ] Speed ramp with score
- [ ] Persisted high score
- [ ] Double-buffered renderer to kill flicker
- [ ] Pause / resume

## License

MIT.