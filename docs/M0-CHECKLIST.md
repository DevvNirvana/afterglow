# M0: does the stack run on your 13.60 console?

No coding. About an hour. Tell me the result of each step.

1. Boot the console, run Relapse, and load your usual homebrew setup (etaHEN or equivalent, plus whatever you use to launch native apps).
2. Install **PS5 OpenGL Showcase** (homebrew.page, or the latest release at github.com/blackbearreloaded/ps5-opengl/releases). Launch it.
   - Does it start? Does it show 3D particles? Does it feel smooth?
   - Note the display mode you used (1080p, 1440p or 4K).
3. Install **Homebrew UI Lab** (homebrew.page). Launch it and press L1 and R1 to switch designs.
   - Does it start? Does the controller work? Does sound play?
4. If you can, note how you launch native apps (title folder in `/data/homebrew`, a launcher, or ShadowMount), because our app will launch the same way.

## Reading the results

- **Both run:** the stack works on your firmware. Go to M1.
- **One runs, one does not:** send me the exact behaviour (black screen, crash, returns to the menu, error text). We can still proceed carefully.
- **Neither runs:** do not start building. We need to find out why first (loader setup, firmware, launch method).
