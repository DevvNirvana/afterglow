# Start here (M1: build the baseline on your own machine)

This folder is the unmodified PS5 native app template, plus `docs/PLAN.md`, `docs/CLAUDE.md` and `docs/M0-CHECKLIST.md`. Nothing has been compiled yet: the workspace that prepared it blocked one of the template's downloads (zlib), so the first real build happens on your machine.

## Do this, in order

1. Open this folder in Antigravity with Claude Code. Move `docs/CLAUDE.md` to the repo root so Claude Code picks it up.
2. Use Linux or WSL. Install prerequisites:
   `sudo apt install curl git make ninja-build ccache pkg-config python3 python3-pip python3-venv tar wget unzip clang-18 clang-format-18 clang-tidy-18 lld-18`
3. Check the machine: `make doctor`
4. Give the app its own identity (title ID must be unique before you distribute):
   `make init TITLE_ID=PPSA12345 APP_NAME="My App Name"`
   (For purely local testing you can keep the default PPSA99999.)
5. Build: `make`
   The first build downloads the public PS5 payload SDK and zlib and generates the runtime. The result is `dist/<TITLE_ID>/` and `dist/<TITLE_ID>.zip`.
6. Copy `dist/<TITLE_ID>/` to `/data/homebrew` on the console (FTP: `make deploy PS5_HOST=<console IP>`), then launch it the same way you launch other native apps.
7. Tell Claude Code (and me) exactly what happened: builds, launches, shows the hello screen, or fails with an error.

## If it works

The template's hello screen on your 13.60 console proves the app layer works there. The next step is M2 in `docs/PLAN.md`: replace the demo screen with one OpenGL shader scene using `blackbearreloaded/ps5-opengl` (start from its `examples/core33-triangle`).

## If the build fails

Run `make doctor` and paste the output plus the first error. `docs/TROUBLESHOOTING.md` in this folder covers common cases.

## If it builds but does not launch on 13.60

That is useful information about the platform, not a failure of the project. Report what you see (black screen, returns to home, error text) and how you launched it.
