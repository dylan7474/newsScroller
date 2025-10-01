# Repository Guidelines

## Project Structure & Module Organization
`main.c` owns initialization, rendering, and headline scrolling, while `cJSON.c`/`cJSON.h` ship the embedded JSON parser. Runtime settings live in `config.ini`, including API credentials and font preferences. Bundled fonts (`font.ttf`, `DejaVuSans.ttf`) stay at the repo root alongside the SDL binaries. Compilers use `Makefile` for Linux, with `Makefile.win` documenting MinGW flags. No dedicated tests directory exists yet; artifacts such as `news_ticker` should never be committed.

## Build, Run, and Development Commands
- `make` — compile the ticker on Linux; install `libsdl2-dev`, `libsdl2-ttf-dev`, `libcurl4-openssl-dev`, and their headers first.
- `./news_ticker` — run the fullscreen ticker using values from `config.ini`.
- `make clean` — remove the compiled binary before rebuilding or distributing.
- `mingw32-make -f Makefile.win` — reproduce the build on Windows via MinGW; keep compiler flags in sync with the Linux target when updating.

## Coding Style & Naming Conventions
Use 4-space indentation and keep braces on the same line as control statements. Name functions, variables, and struct members with `snake_case`. Place struct definitions near the top, static helpers above `main`, and favor `const` qualifiers for read-only data. Comments should explain intent or external constraints, not obvious mechanics.

## Configuration & Secrets
Work from an ignored copy like `config.local.ini` and never commit real API keys. Confirm `font_path` references an available `.ttf` (e.g., `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`). Document any new config fields in both the sample file and project notes.

## Testing Guidelines
Automated coverage is not yet in place. After each change, run `./news_ticker`, verify live headlines fetch correctly, unplug or block the network to ensure fallback messages display, and check SPACE pauses/resumes while ESC exits. Record platform-specific findings in your PR.

## Commit & Pull Request Guidelines
Keep commits small and imperative (e.g., `Improve fallback color rotation`). Update documentation whenever build steps or config fields change. Pull requests must include: a concise summary, manual test notes, dependency impacts, visuals for UI updates, and links to related issues or NewsAPI changes.
