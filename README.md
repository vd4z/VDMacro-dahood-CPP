## insidingforfeds_macro (windows c++)

simple macro for dahood. fast, low delay, low cpu. does first person and third person. you can bind any key or mouse (x1/x2 too). no extra junk.

### why this one
- low latency, reacts quick
- no bloat, small console app
- bind anything (keys, x1/x2, mmb, lmb, rmb, F keys, alt keys)
- can save your config so next time is 1 step

### requirements
- windows 10/11 (x64)
- visual studio 2022

### setup / build
- option 1 (vs 2022):
  - open `insidingforfeds_macro.sln`
  - set `Release` | `x64`
  - press Ctrl+F5 to build and run
- option 2 (prebuilt):
  - run `x64/Release/insidingforfeds_macro.exe`

### how to use
1) choose activation:
   - `hold` (hold to run, let go to stop)
   - `toggle` (press to start, press again to stop)
2) choose mode:
   - `1st person`
   - `3rd person`
3) bind:
   - press any key or any mouse button (x1/x2/mmb/lmb/rmb). it binds the first thing you press
4) choose if you wanna save config for next time

### what it does
- 3rd person: I down → 4ms → O down → 4ms → I up → 4ms → O up → 4ms, loop. uses scancodes so roblox picks it up better
- 1st person: scroll up/down really tight timing with smaller wheel steps so it’s smooth and stable

### tips
- click roblox window first so it has focus or ur gonna fuck shit up
- if 3rd person speed changes after an update, tiny timing tweaks (like 1–2ms) can help (dm me on discord)

### config
- saves to `config.json` (same folder)
- stores: activation, mode, your bind
- next launch you can reuse it

### troubleshooting
- x1/x2 not working:
  - bind again inside the app by pressing the mouse button
  - check if sum fuckass overlay or mouse software is raping the button
  - try running as admin
- stutter / weird timing:
  - close overlays and heavy apps
  - use `Release | x64` build

### credits
- roblox: `valkrycx`
- discord: `insidingforfeds`




## tags
Da hood, macro, roblox, dahood, da hood macro
