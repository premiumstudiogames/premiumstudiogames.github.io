# Premium Engine 00.02.00 (experimental)

A game engine and editor for Windows, written in C++. The editor is laid out like Unreal Engine 5:
menu bar (File, Edit, Window, Tools, Build, Select, Actor, Help), toolbar, Viewport, Outliner,
Details, World Settings, Content Browser and Output Log. Games are packaged as Windows programs (.exe).

## Start
1. Unzip anywhere and run **PremiumEngine.exe**.
2. In the Project Browser pick a template (Blank, First Person, Third Person), type a name, press **Create**.
3. Press the green **Play** button (Alt+P). Esc stops.
4. **Platforms > Windows (.exe)** packages the game to `Documents\PremiumEngine\Builds\<Name>\Windows`.
   That folder is the whole game: zip it and share it.

Projects are saved in `Documents\PremiumEngine\Projects` as `.pge.json` files
(the same format as Premium Engine 0.1 on the web, so projects move between the two).

## What is inside
- Renderer: OpenGL 3.3, physically based lighting (like Unreal / three.js), sun shadows,
  point and spot lights, sky light, fog, ACES tone mapping.
- Physics: Jolt Physics (the engine used by Horizon Forbidden West), simulated bodies, collisions, character.
- Scripts: JavaScript (QuickJS) with `onStart`, `onUpdate`, `onOverlap`, `onHit`, `onKey`.
  Help > Script API Reference lists everything. Endless loops are stopped after 2 seconds instead of freezing.
- Editor: move / rotate / scale gizmo with snapping, undo / redo, copy / paste, attach (drag in the Outliner),
  drag and drop from the Content Browser, material editor with preview, code editor with colours and error lines,
  map check, statistics, console commands.

## Needs
Windows 10 or 11 (64-bit) and a graphics card with OpenGL 3.3 (any PC from the last 12 years).

## Build from source
```
cmake -S native -B build -A x64
cmake --build build --config Release
build\Release\PremiumEngineTests.exe
```
