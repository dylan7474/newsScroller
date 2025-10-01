News Ticker
===========

Build
-----
- Install SDL2, SDL2_ttf, libcurl, and their headers (e.g., `sudo apt install libsdl2-dev libsdl2-ttf-dev libcurl4-openssl-dev`).
- Run `make` to produce the `news_ticker` binary on Linux; use `make clean` before rebuilding.
- On Windows, `mingw32-make -f Makefile.win` mirrors the Linux build flags for MinGW.

Configuration
-------------
- Copy `config.ini` to a private `config.local.ini` and adjust the following keys:
  - `api_key`: required NewsAPI key. Placeholder values trigger on-screen warnings and fallback headlines.
  - `font_path`: path to the `.ttf` font used for rendering. The ticker falls back to DejaVu Sans if the file is missing.
  - `font_size`: positive integer controlling line height.
  - `country_code`: two-letter ISO country code used in the NewsAPI request.
- The app reports configuration issues in stderr and in the ticker itself when it has to fall back.

Runtime
-------
- Launch with `./news_ticker`; the window stretches to your desktop resolution.
- SPACE pauses or resumes scrolling; ESC exits.
- Live headlines scroll independently with delta-time based speeds; when changes are paused the delta clock is reset to avoid jumps.
- If NewsAPI is unreachable, the ticker retries with exponential backoff, then displays a clearly labeled fallback playlist with the failure reason.

Verification
------------
- Run `./news_ticker` with a live network to confirm headlines render and cycle without stutter.
- Disconnect networking to ensure fallback messaging appears, including the highlighted error banner.
- Restore networking, update `config.ini` with deliberate mistakes (empty key, bad font), and confirm warnings print to stderr and the ticker.
- Use `make clean` before distributing builds; avoid committing built binaries.
