Premium Engine for Windows (experimental). Unzip and run PremiumEngine.exe.

- Unreal-style editor: File / Edit / Window / Tools / Build / Select / Actor / Help, Viewport, Outliner, Details, World Settings, Content Browser, Output Log
- Templates: Blank, First Person, Third Person
- Physically based renderer (OpenGL 3.3), Jolt Physics, JavaScript scripts
- Play in the editor, package games as Windows .exe
- Models: glTF 2.0 (.glb / .gltf) with their materials, textures, skeletons and animations - drop them on the window
- Materials with textures: base colour, normal, roughness, metallic, ambient occlusion, emissive; tiling, world aligned, masked
- Camera effects (post process): vignette, film grain, chromatic aberration, VHS tape, scanlines, saturation, contrast, temperature, bloom, tint
- Reflections: Sphere Reflection Capture actors (shiny floors, metal, glass reflect the room)
- AI navigation: characters walk round walls, up ramps and stairs, follow the player (scripts: moveTo, Blueprints: AI Move To); Show > Navigation
- Landscape: big outdoor ground - sculpt (raise, lower, smooth, flatten, noise, hills) and paint 4 layers with the mouse; it collides and AI walks on it
- Third Person: the engine's mannequin with Idle, Walk, Run, Jump, Fall (or your own character); a camera that comes in front of walls
- Blueprint functions with inputs and outputs (pure or with exec pins)
- Cars: Place Actors > Vehicle (or Vehicle on any physics mesh, or makeVehicle from a script): engine, gears, suspension, 4 wheels; the player gets in with E and drives (W / S, A / D, Space, controller); AI cars from scripts
- Physics Constraints: hinge (doors), fixed (breakable), ball socket (chains), slider (drawers, lifts, powered), distance (ropes); welded parts (a car built from blocks)
- Ragdolls: character models fall limp (from damage or Simulate Physics) and can be picked up, dragged and thrown
- Gameplay: line traces, damage and explosions (radial damage), player health, attaching to bones / the player / the camera (weapons, flashlights), decals (blood, bullet holes, scorch marks), carrying and throwing bodies
- Blueprints (visual scripting) with English and Arabic names and explanations

Every build is tested on Windows: engine tests, the editor makes a project, plays and packages it, and the packaged game starts and walks.
