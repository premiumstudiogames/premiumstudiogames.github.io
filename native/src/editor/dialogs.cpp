// Project Browser and the editor's dialogs (preferences, settings, help, packaging, ...).
#include "editor.h"
#include "platform.h"
#include "log.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include "arabic.h"
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;
namespace pge {

static const char* API_TEXT = R"(SCRIPTS
Scripts are JavaScript. Add one to an actor in Details > Script. These functions are called by the engine:

  function onStart() { }          once, when the game starts
  function onUpdate(dt) { }       every frame (dt = seconds since the last frame)
  function onOverlap(other) { }   the player walked into this actor (Trigger Box or mesh)
  function onHit(other) { }       a physics body hit this actor (needs collision)
  function onKey(code) { }        a key was pressed, e.g. 'KeyE', 'Space', 'Digit1'
  function onAnimationEnd(name) { }   a model's animation that doesn't loop reached its end
  function onMoveFinished(ok) { }     this actor's walk (moveTo) ended: ok = it got there
  function onDamage(amount, info) { }       this actor was hurt: info = { type, from (actor / player), point, direction }
  function onPlayerDamage(amount, info) { } the player was hurt (every script hears it)
  function onPlayerDeath() { }              the player's health reached 0 (none of these: the player starts again at the Player Start)
  function onJointBreak(joint) { }          a Physics Constraint of this actor broke (pulled harder than its Break Force)
  function onVehicleEnter(car) { }  function onVehicleExit(car) { }   the player got in / out of a car

self  - the actor the script is on
  self.name, self.tags, self.id
  self.position.x / .y / .z      metres (also .set(x, y, z), .add(v), .copy(v))
  self.rotation.x / .y / .z      radians
  self.scale.x / .y / .z
  self.visible = false           hide / show
  self.worldPosition             position in the world (read only)
  self.setColor('#ff8800')       change the colour
  self.setText('Hello')          Text Render actors
  self.addForce(x, y, z)         push a physics actor (Simulate Physics on)
  self.setVelocity(x, y, z)      self.velocity
  self.lookAt(x, y, z)           turn towards a point
  self.distanceTo(other)         metres to another actor or the player
  self.destroy()                 remove from the game
  models (.glb / .gltf):
  self.playAnimation('Run', { loop: true, speed: 1, blend: 0.2 })   play one of its animations (blend: seconds from the one before); false if it has none of that name
  self.stopAnimation()           back to how it is in the file
  self.animation                 { name, time, duration, playing, loop, speed }
  self.animations                ['Idle', 'Walk', 'Run', ...]
  self.animationSpeed = 2        twice as fast (-1 = backwards)
  self.bone('Hand_R')            { position, rotation } of a bone (joint) in the world: hold things, shoot from a muzzle
  a character that moves (moveTo, or moved by a script) is idle / walks / runs by itself, its feet on the ground; self.autoAnimate = false / self.footIK = false to stop
  self.setBlendSpace([['Idle', 0], ['Walk', 1.5], ['Run', 5]], speed)   self.blendValue = speed   (mixed by the value, in step)
  self.playLayer('Shoot', { bone: 'Spine', loop: false, weight: 1 })   self.stopLayer()   an animation on part of the body
  self.setIK('Hand_L', place | actor | null, { weight })   self.lookAt(actor | place | null, { maxAngle: 70 })   self.clearIK()   (world.player.setIK / lookAt too)
  self.setBoneScale('Head', 0)   a bone and what hangs on it bigger / smaller; 0 = gone (gore: a limb shot off)
  walking (AI) on the level's floors, round walls, up ramps and stairs:
  self.moveTo([x, y, z] | actor | world.player, { speed: 3.5, acceptance: 0.5, turn: true })   false if there's no way; an actor is followed
  self.stopMoving()   self.isMoving   self.moveResult ('success', 'fail', 'stopped')
  attaching (a weapon in a hand, a flashlight on the camera, a hat on the player):
  self.attachTo(other | world.player | 'camera', { bone: 'Hand_R', keepWorld: false, offset: [x, y, z], rotation: [deg x, y, z] })
  self.detach({ keepWorld: true })   self.parent      a collision mesh attached to a moving body becomes part of it (a car built from blocks)
  self.applyDamage(25, { type: 'bullet', from: world.player, point, impulse: 300 })   its onDamage runs; impulse pushes it where it was hit
  self.addImpulse([x, y, z], point)  push a physics actor (or a ragdoll) at a point
  self.ragdoll({ impulse: [0, 50, -300], point })   a character model goes limp and falls (its bones become physics bodies)   self.isRagdoll
  cars:  self.vehicle  .speed (km/h) .rpm .gear .grounded .driver .setInput(throttle, steer, brake, handbrake) .enter() .exit()
  self.makeVehicle({ torque: 650, maxSteer: 35, wheelRadius: 0.4, wheelWidth: 0.3, travel: 0.3, stiffness: 1.5, drive: 'all' | 'rear' | 'front', mass: 1300, wheelMesh: 'Cylinder' })
  self.joint                     a Physics Constraint actor's joint (see world.addJoint)

world
  world.time                     seconds since the game started
  world.spawn('Models/Robot.glb', { position: [0, 0, 5], animation: 'Idle' })   a model of the project (or 'Cube', 'Sphere', ...)
  world.findPath(from, to)       the way between two places (or actors / the player): [Vec3, ...] or null
  world.navigation.rebuild()     after the level changed (walls moved, built, broken); world.navigation.info
  world.setPostProcess({ vignette: 0.5, grain: 0.3, vhs: 0.6, chromatic: 0.2, scanlines: 0.3, saturation: 0.8, contrast: 1.1, temperature: -0.3, bloom: 2, tint: '#ffeedd' })
                                 camera effects (0..1; saturation / contrast / bloom 1 = as is); only what you give changes; null = all off. world.postProcess = now
  world.vars                     an object to keep your own values (score, ...)
  online (net.host / net.join): physics actors, cars, ragdolls and actors with Replicates are the same for every player by themselves;
    whoever spawns one with physics (or replicate: true) moves it; a player who gets in a car or grabs a box moves it; each player sees
    the others as characters (named 'Player 1', 'Player 2', tag 'player'); applyDamage to one reaches that player; decals show for all
  world.raycast(from, to, { ignore: [actors], ignorePlayer: true })   the first thing on the line (Line Trace): { position, normal, distance, actor, player, body, bone } or null
  world.applyDamage(actor | world.player, 20, { type, from, point, direction, impulse })
  world.applyRadialDamage(center, radius, 80, { impulse: 1500, type: 'explosion', from })   an explosion: less further away, walls shield; gives the actors it hurt
  world.spawnDecal(hit.position, hit.normal, { type: 'blood' | 'bullet' | 'scorch', texture: 'Textures/Crack.png', size: 0.8, color: '#550000', angle, life: 10, actor, body })
                                 a picture laid on a surface (it moves with the actor / body it's on; at most 512, the oldest go);  world.clearDecals()
  world.addJoint({ kind: 'Hinge' | 'Fixed' | 'BallSocket' | 'Slider' | 'Distance', a: actor, b: actor (none = the world), pivot, axis, min, max, motorSpeed, motorForce, breakForce })
                                 → joint: .value (angle / position)  .setMotor(speed, force)  .setTarget(angle or position, force)  .remove()  .alive
  world.grab(hit | actor, point) → handle: .moveTo(place) every frame to carry it, .release(velocity) to drop or throw it (boxes, a ragdoll's arm)
  world.player.health  .maxHealth  .dead  .damage(n)  .heal(n)   world.player.set({ health: 100, maxHealth: 100, vehicleKey: 'KeyE' ('' = no getting in by key) })
  world.player.vehicle           the car the player drives (or null)   world.player.enterVehicle(car)   world.player.exitVehicle()
                                 in a car: W / S (RT / LT) drive and brake, A / D (left stick) steer, Space (A) hand brake, V the view, E (Y) out
  world.player                   .position, .teleport(x, y, z), .launch(x, y, z)
  world.player.fly = true        flying: no falling, Space goes up, the sneak key (or Shift) down
  world.player.set({ walkSpeed: 4.3, runSpeed: 5.6, sneakSpeed: 1.3, jumpSpeed: 8.4, gravity: -32, accel: 46, runKey: 'Control', sneakKey: 'Shift',
                     doubleTapRun: true, flySpeed: 8, flyRun: 1.6, flyClimb: 6, yaw: 0, pitch: 0, mouseSensitivity: 1, locked: false })   how the player moves (any of them; locked: no walking, the scripts move the player)
  world.player.settings          all of them, and .grounded .running .sneaking .velocity .yaw .pitch
  storage.set('key', 'text')  storage.get('key')  storage.setJSON('key', obj)  storage.getJSON('key', default)  storage.remove('key')  storage.keys('prefix')
                                 the game's own saved values (settings, world list, inventories), in Documents\PremiumEngine\Saves\<game>\store
  function onStop() { ... }      runs when the game stops or restarts (save here)
  world.find('Name')             an actor by name (or null)
  world.findAll('tag')           actors with a tag (or that name)
  world.spawn('Cube', { position: [0, 5, 0], scale: [1, 1, 1], color: '#ff0000', physics: true, name: 'Box', script: 'SC_Name', vehicle: { ... }, parent: actor, bone: 'Hand_R' })
  world.destroy(actor)
  net.host({ port: 46600, name: 'My world', max: 8, info: '' })   play together: host a game (others on the LAN see it; over the internet they need
                                 your address and the port open)     net.join('192.168.1.5', 46600)   join one
  net.searchLan()  net.lan       games on this network: [{ name, address, port, players, max, info }]
  net.send(obj, to)  net.receive()   send (the host: to = a player's id or everyone; a player: to the host) / what arrived:
                                 [{ type: 'join' | 'leave' | 'msg' | 'connected' | 'closed' | 'error', id, data }]
  net.role ('' | 'host' | 'client')  net.id (0 = the host)  net.peers  net.connected  net.error  net.kick(id)  net.stop()
  world.message('Text', seconds) big message in the middle of the screen
  world.hud('id', 'Text')        a line in the corner (empty text removes it)
  premium.trophy('id')           a trophy on the player's Premium Games account (id from the store's list)
  premium.console / .store       true when started from Premium Games' console mode / from Premium Games
  world.restart()                restart the level
  world.setGravity(-9.81)
  world.setSky({ sky: '#9ccbe6', fogColor: '#c4e4f1', fog: true, fogDensity: 0.006, skyLight: '#e6f2ff', groundLight: '#4a4636', ambient: 0.8, exposure: 1, fov: 75,
                 sun: { dir: [0.3, -0.8, 0.2], color: '#fff4e0', intensity: 2.4 } })   time of day and weather (any of them; dir points from the sun; colours also as [r, g, b])
  world.setQuality({ msaa: 4, ssao: true, bloom: true, shadowDistance: 160 })   picture quality (a settings menu); shadowDistance 0 = no sun shadows
  world.setSky({ skyTop: '#3d8fd6', sunDisc: '#fff4c9', sunSize: 0.035, moon: 1, stars: 0.8 })   a sky drawn behind everything: top colour (sky = the horizon),
                                 the sun's disc and glow where the sun is, the moon opposite it, stars (0 .. 1)
  voxels.solidAt(x, y, z)  voxels.boxSolid([x0, y0, z0], [x1, y1, z1])   is something solid there (a box: any of it)
  files.read('data.json')  files.readJSON('data.json', default)   a text file shipped next to the game file (null if missing)
  world.setCamera({ target: [x, y, z], distance: 6, height: 1 })   the camera behind a point, turned by the mouse (vehicles); world.setCamera(null): the player's eyes

input
  input.key('w')                 held down?  ('space', 'shift', 'up', 'a'..'z', '0'..'9')
  input.pressed('space')         pressed this frame?
  input.mouse                    .dx, .dy, .left, .right
  input.pressed('MouseLeft')     mouse buttons work like keys: 'MouseLeft', 'MouseRight', 'MouseMiddle'
  input.pressed('WheelUp')       the mouse wheel turned this frame ('WheelUp' / 'WheelDown'; onKey gets them too)

world.camera                     .position, .forward (the direction the player looks)

MODELS (models) - creatures, vehicles, held items: parts made of boxes (or your own vertices) that turn about their pivots
  models.define('pig', { parts: {
    body: { pivot: [0, 0.7, 0], boxes: [{ size: [0.6, 0.5, 1], color: '#f2a5a5' }] },
    head: { parent: 'body', pivot: [0, 0.15, -0.5], boxes: [{ size: [0.5, 0.5, 0.45], pos: [0, 0, -0.22], tile: { top: 4, side: 3 } }] },
    leg1: { parent: 'body', pivot: [-0.18, -0.25, -0.35], boxes: [{ size: [0.2, 0.45, 0.2], pos: [0, -0.225, 0], block: 5 }] } } })
  a part: pivot (from its parent's), parent, rotation (rest), boxes, mesh;  { boxes: [...] } alone = one part named 'body'
  a box: size, pos (centre, from the pivot), rotation, color / colors: { top, side, bottom } / tile: n or { top, side, bottom }
         / block: id (that block's faces), glow (0 .. 2), repeat: true (the tile repeats every metre)
  mesh: { positions: [x, y, z, ...], indices, normals, colors: [r, g, b, ...], uvs: [u, v, ...], color, tile, glow }
  const m = models.spawn('pig', { position: [x, y, z], rotation: [0, yaw, 0], scale: 1 })    null + models.lastError if not
  m.set({ position, rotation, scale, visible, tint: '#ff8080', shadow, parts: { leg1: { rotation: [0.5, 0, 0] } } })
  m.pose({ leg1: { rotation: [a, 0, 0], position, scale, visible } })      every frame, all the parts in one call
  m.part('head') -> { position, forward, up, matrix }     m.position  m.rotation  m.visible  m.remove()
  rotations in radians, order X Y Z (like three.js); hiding a part hides its children. Lit and shadowed like the blocks.

PARTICLES - small blocks that fly, fall, bounce on blocks and shrink (pieces of a broken block, smoke, sparks)
  particles.emit({ position: [x, y, z], count: 12, block: id | tile: n | color: '#hex' | colors: [...],
    speed: [1, 3], direction: [0, 1, 0], spread: 1 (0 = straight, 1 = everywhere), velocity: [x, y, z], area: [x, y, z],
    gravity: -9.81, drag: 0.4, life: [0.6, 1.2], size: [0.08, 0.14], spin: [0, 6], bounce: 0.25, collide: true, shrink: true, glow: 0 .. 2 })
  particles.clear()   particles.count
)"   // (MSVC: one string literal piece is at most 16 KB)
R"(
SOUND (sound) - files (wav, ogg, mp3, flac) next to the project, or sounds made by code; 3D: the ear is the camera
  const h = sound.play('sounds/step.ogg', { volume: 0.8, pitch: 1.1 })      returns a handle (0 + sound.lastError if not)
  sound.play('music/menu.ogg', { bus: 'music', loop: true, stream: true, fadeIn: 2 })
  sound.play('fire.wav', { position: [x, y, z], minDistance: 2, maxDistance: 80, rolloff: 1 })   heard from there
  sound.play('engine.ogg', { follow: self, loop: true })     moves with an actor (stops when it is destroyed)
  options: volume, pitch, pan (-1 .. 1), loop, bus ('sfx' default, 'music', 'ui' or any name), delay (s), fadeIn (s), stream
  sound.set(h, { volume, pitch, pan, position, loop })    sound.stop(h, fadeSeconds)    sound.playing(h)    sound.stopAll('music')
  sound.volume = 0.8   (the whole game)       sound.setVolume('music', 0.5)   sound.getVolume('music')
  sound.make('jump', description)   keep a sound made by code under a name, then sound.play('jump')
  sound.play(description, options)  make and play at once;   sound.play(float32Array, { sampleRate: 44100 })  your own samples
  sound.samples('name', float32Array, { sampleRate: 44100, channels: 1 })
  a description (like Web Audio: oscillator -> filter -> gain):
    { type: 'square', freq: [300, 900], duration: 0.2, gain: { attack: 0.005, decay: 0.05, sustain: 0.6, release: 0.1 },
      filter: { type: 'lowpass', freq: 2000, q: 1 }, detune: 0, vibrato: { rate: 6, depth: 20 }, volume: 0.5 }
    type: sine, square, sawtooth, triangle, noise (white), pink, brown     duty: 0.5 (square)
    freq / gain / filter freq: a number, [from, to] (exponential), or points [[0, 440], [0.1, 880, 'exp'], [0.3, 220]] (seconds)
    gain envelope: attack, decay, sustain (level), hold, release, peak, curve: 'exp'
    filter: lowpass, highpass, bandpass, notch (Q like Web Audio's BiquadFilterNode); a list of filters works too
    several voices: { voices: [ { ..., start: 0 }, { ..., start: 0.1, length: 0.2 } ], echo: { time: 0.25, feedback: 0.35, mix: 0.35 }, volume: 0.8 }
  The game's sounds pause with the pause menu. Packaging copies the project's sounds and pictures with the game.

GAME UI (ui) - menus, HUD, inventory, settings, made from scripts (like Unreal's UMG)
The screen is 1080 units tall (1920 wide at 16:9) at any resolution: sizes and positions are in these units.
  const w = ui.create('window', { title: 'Inventory', w: 700, h: 500, modal: true, pause: true, visible: false })
  w.add('button', { text: 'Craft', on: { click: e => ... } })      node.add(type, props) = ui.create with parent
  ui.get('id')  node.set({ ... })  node.on('click', fn)  node.off('click')  node.remove()  node.clear()
  node.show({ anim: 'slide', from: 'bottom', time: 0.25 })   node.hide(...)   node.toggle()   node.visible
  node.value  node.text  node.data  node.rect (x, y, w, h)  node.children  node.parent  node.focus()
  ui.rtl = true          rows and grids from the right (Arabic menus);  ui.scale = 1.2 (bigger UI)
  ui.escape = true       Escape goes to your script (onKey('Escape')), not the engine's pause menu
  ui.open / ui.paused / ui.typing     a menu that frees the mouse is open / the game is paused / a text field has the keys
  ui.mouse               { x, y, w, h }: the mouse and the screen in UI units from the top-left (an item that follows the mouse)
types: panel, text, image, button, grid, list (scrolling column), input, slider, tabs, window
props (all optional):
  id, parent, x, y, w, h ('fill', 'fill:2', '50%', 'auto' or units), minW, maxW, minH, maxH
  anchor: 'center' | 'top' | 'bottom' | 'left' | 'right' | 'top-left' | 'bottom-right' ...  (x, y move from there)
  layout: 'free' | 'row' | 'column' | 'grid',  gap, pad (n, [v, h] or [t, r, b, l]), align: 'start' | 'center' | 'end' | 'stretch',
  justify: 'start' | 'center' | 'end' | 'between',  cols, cell (n or [w, h]),  dir: 'rtl' | 'ltr',  scroll: true,  clip: true
  bg ('#rrggbbaa', 'rgba(...)', or ['#top', '#bottom'] gradient), radius, border (width or { width, color }), opacity, shadow
  hover: { bg, color, border }, press: { bg },  z (drawing order),  visible
  text, size, color, align ('left' | 'center' | 'right' | 'auto'), valign, wrap, bold, lineHeight   (Arabic is joined, right to left)
  image: 'icons/sword.png', fit: 'contain' | 'cover' | 'stretch', tint;  block: 5 or { id: 5, face: 'top', cube: true } (from the atlas)
  input: value, placeholder, maxLength, password    slider: min, max, step, value    tabs: labels, value (children are the pages)
  window: title, closable, movable
  modal: true (frees the mouse, the game doesn't look around)   pause: true (the game stands still while it is shown)
  draggable / droppable / data   drag and drop between nodes (inventory)
  disabled, pass (the mouse goes through it),  anim: { anim, from, time } (default for show / hide)
  animations: fade, slide (from left / right / top / bottom), pop, garage (comes down like a garage door), reveal
events (node.on): click, dblclick, rightclick, down, up, enter, leave, change, input, submit, focus, blur, close,
  dragstart, dragend, drop.  e.target (where it happened), e.current (the node whose handler runs), e.value,
  e.from / e.to / e.data (drag and drop), e.shift / e.ctrl / e.alt, e.stop()  - clicks and drops bubble up to the parents
)"
R"(
BLUEPRINTS - scripts made of nodes and wires, like Unreal Engine's (+ Add > Blueprint, or Details > Script > + New Blueprint)
  A Blueprint is put on an actor like a script, and runs the same way: in Play, in packaged games, on the console.
  Events start the flow: Event BeginPlay, Event Tick (Delta Seconds), Event ActorBeginOverlap, Event Hit, Key Pressed,
  Any Key Pressed, Event EndPlay, Custom Event, On Widget Clicked (a game UI button), On Animation End (a model's animation finished).
  Animation: Play Animation (name, loop, play rate, blend time), Stop Animation, Set Play Rate, Get Current Animation, Is Animation Playing,
  Get Bone Location (where a bone of the model is: Hand_R ...).
  The white wire (exec) is the order things happen in; coloured wires carry values: red Boolean, green Float, pink String,
  yellow Vector, blue Actor, teal Array. Inputs with no wire use the value typed on the node; Target = self.
  Flow Control: Branch, Sequence, For Loop, For Each Loop, While Loop, Do Once, Flip Flop, Gate, Delay, Set Timer by Event,
  Clear Timer, Call Custom Event.  Variables: + Variable in My Blueprint, drag one into the graph (Alt: Set).
  Right-click (or drag a wire into empty space): add a node - search in English or Arabic. Alt+click a pin: break its
  wires. Right-drag: pan. Wheel: zoom. F: frame all. C: a comment box (around the selected nodes). Delete, Ctrl+C/V/D.
  The names are English (the main ones, like Unreal); the language box shows them in Arabic or both, and hovering a node
  explains it in both. Compile results (errors / warnings) are on their nodes; Show Code shows the script it runs as.
  On the console version: R3 (right stick click) gives a mouse pointer: left stick moves it, A clicks, X right-clicks,
  right stick zooms / scrolls.

CONTROLLER (Xbox, PlayStation, Switch Pro ... plugged in or Bluetooth, found when it connects)
  The player walks with the left stick and looks with the right stick out of the box; clicking the left stick runs.
  Buttons are keys: 'PadA' 'PadB' 'PadX' 'PadY' 'PadLB' 'PadRB' 'PadLT' 'PadRT' 'PadLS' 'PadRS' 'PadStart' 'PadBack'
  'PadUp' 'PadDown' 'PadLeft' 'PadRight'   ->  input.key('PadA'), input.pressed('PadRT'), onKey('PadY') all work.
  They also press the keys a PC game already uses (aliases): A = Space, RT = MouseLeft, LT = MouseRight, Start = Escape,
  LB / RB = WheelUp / WheelDown, B = Escape while a menu is open, B = sneak.  So a mouse and keyboard game plays at once.
  input.pad                      a controller is connected
  input.axis('LeftX')            -1 .. 1 ('LeftX' 'LeftY' 'RightX' 'RightY'), triggers 0 .. 1 ('LT' 'RT'); dead zones applied
  input.rumble(low, high, seconds)   vibration (0 .. 1 each motor)
  input.padSettings({ aliases: true, invertY: false, lookSpeed: 3 })
  In menus (ui): the D-pad / left stick move between buttons (a ring shows which), A presses, B goes back (the scope's
  'back' event, and Escape), LB / RB change tabs, left / right move a slider, A picks a draggable item up and A puts it
  down.  On the console a text field opens the on-screen keyboard (Arabic / English, X deletes, Y done).

CONSOLE VERSION - every game ships twice: the PC version and the console version
  Platforms > Console (controller, up to 2 GB): Builds/<name>/Console with console.json. Full controller play,
  console menus (focus ring, on-screen keyboard, no mouse cursor) and lighter graphics (MSAA 2, shadows 120 m).
  The package must stay under 2 GB, or packaging stops and says how big it is.
  Play options (the arrow next to Play) > Play as the Console Version: test it in the editor.   premium.console is true there (make bigger, simpler menus).

BLOCK WORLD (voxels)
The ground is split into 16 x 16 m chunks. Near the ground (the near layer) blocks can be 20, 40, 60, 80 or 100 cm
wide and 20 - 100 cm tall; above and below it everything is 1 m blocks. Chunks are generated and built on background
threads, hidden faces are removed, corners get ambient occlusion, and the player and physics bodies collide with the
blocks directly. Positions are metres. A block sits on the 20 cm grid; (x, y, z) is its lowest corner.
BLOCKFALL's world fits: voxels.create({ bottom: -98, top: 302, nearLayer: [-2, 20] }).

  voxels.create({ bottom: -32, top: 64, nearLayer: [-6, 14], viewDistance: 6, seed: 2026, threads: 0 })
                                 start a block world (removes the old one). false + voxels.lastError on a mistake
  voxels.enabled                 is there a block world?
  voxels.defineBlock(1, { name: 'Grass', color: '#5a9c3c', tile: 0, ... })    options:
      color: '#rrggbb', 'rgb(r,g,b)', [r, g, b] (0..1) or { top, side, bottom }     tile: number or { top, side, bottom } (atlas)
      solid: false            no collision (plants)          transparent: true    see-through: does not hide faces;
                              textured see-through blocks are cut out (clear pixels are holes: leaves, flowers), cutout: false turns it off
      liquid: true            water: moving see-through surface, no collision, the player swims; opacity: 0.75
      biome: 'top' | 'side' | 'bottom' | ['top', 'side'] | true    those faces take the chunk's biome colour (chunk.tint)
      vary: 0.08              every block its own shade (0 .. 0.5), like BLOCKFALL
      glow: 1                 light of its own (0 .. 2, or true)
      shape: 'stairs'         the block's own shape: cube, slope, half, hex, arch, wedge, cylinder, pyramid, roof, stairs, cross
                              (cross = two crossed planes for flowers and grass; BLOCKFALL's sh numbers 0-8 work too)
      model: [ { box: [x0, y0, z0, x1, y1, z1], color, tile }, { cylinder: [x, y, z, radius, length], axis: 'x', sides: 16, color } ]
                              a model block (wheels, chairs, engines); coordinates inside the block 0..1
  voxels.setAtlas('textures/blocks.png', 16)   block textures: a PNG of square tiles, 16 per row
  voxels.setAtlas({ tileSize: 16, tiles: [Uint8Array, ...] })   textures made by your code: RGBA bytes of each tile
  voxels.setAtlas({ width, height, pixels: Uint8Array, tilesPerRow })   or the whole atlas at once
  voxels.setGenerator('flat' | 'hills', { ground: 0, top: 1, dirt: 2, stone: 3, amplitude: 9, scale: 0.012, octaves: 4, smooth: true })
                                 built-in ground made of block ids top / dirt / stone
  voxels.setGenerator(function (chunk, noise, params) { ... }, { ... })  your own ground (runs on background threads,
                                 cannot use world / self; only chunk, noise and params)
      chunk.cx, chunk.cz         chunk number;  chunk.x, chunk.z = its corner in metres
      chunk.column(x, z, yBottom, yTop, id)   1 m blocks from yBottom up to yTop (x, z = 0..15 inside the chunk)
      chunk.fill(x0, y0, z0, x1, y1, z1, id)  1 m blocks in a box (max not included)
      chunk.set(x, y, z, id, size, opts)      one block (size 0.2 .. 1); opts like voxels.setBlock (height, rotation, shape, carve)
      chunk.place(x, y, z, id, size, opts)    the same, only where there is air or water (trees don't cut into the ground)
          Writes may reach up to 16 m outside the chunk (trees, buildings, dungeons): that part is added to the chunk
          next to it when it is made, every time it is made again, and never over the player's own changes there.
      chunk.cells(x, y, z, w, h, d, ids)      many 20 cm blocks at once from a Uint16Array: ids[i + w * (k + d * j)] is the
                                              block at (x + i / 5, y + j / 5, z + k / 5); 0 leaves the cell as it is (fast)
      chunk.stack(x, z, y, [ids])             20 cm blocks stacked up one 20 cm column from y
      chunk.get(x, y, z)                      block id there (0 = air), inside this chunk
      chunk.tint(x, z, [r, g, b])             biome colour of a 1 m column (multiplies the colour of biome faces; 1 = none)
      noise.simplex2(x, z)  noise.simplex3(x, y, z)  noise.fbm(x, z, octaves, lacunarity, gain)  noise.hash(x, y, z)
      noise.bf.h2(x, y, s)  noise.bf.vn(x, y, s)  noise.bf.fbm(x, y, s)  noise.bf.h3(x, y, z, s)  noise.bf.vn3(x, y, z, s)
      noise.bf.hashf(x, y, z)                 BLOCKFALL's own noise in C++: exactly the game's numbers, much faster
      noise.bf.fbmGrid(out, nx, nz, x0, z0, step, scale, seed)   a whole grid in one call (out = Float64Array(nx * nz)):
      noise.bf.vnGrid(...)                    out[i + nx * j] = fbm((x0 + i * step) * scale, (z0 + j * step) * scale, seed)
      (all noise uses the seed from voxels.create, so every thread and every run makes the same world)
  voxels.getBlock(x, y, z)       { id, x, y, z, size, height, rotation, shape, carve } of the block at a point, or null for air
  voxels.setBlock(x, y, z, id, size, opts)   place (replaces blocks it overlaps). false + voxels.lastError if not allowed
      opts: { height: 0.4 }          a block lower than it is wide (20 cm steps)
            { rotation: 0..11 }      0-3 quarter turns, + 4 upside down, + 8 on its side (BLOCKFALL's r)
            { shape: 'stairs' }      this block's shape (see defineBlock)
            { carve: [125 x 0 / 1] } a carved 5 x 5 x 5 form: index i + 5 * (j + 5 * k), 1 = solid
            { onlyEmpty: true }      only if the space is free (air or water)
  voxels.carve(x, y, z, i, j, k, solid)   cut out (false) or fill (true) one 1/5 piece of the block at x, y, z
  voxels.canPlace(x, y, z, size, height)
  voxels.removeBlock(x, y, z)    removes the block at a point, returns its id (0 = nothing)
  voxels.liquidAt(x, y, z)       id of the liquid there (0 = none)
  voxels.raycast(from, dir, max, { liquids: true })   the first block along a ray, or null (water is looked through
      unless liquids: true):  { id, x, y, z, size, height, normal, point, distance, placePosition(size, height) }
  voxels.ready(x, y, z)          the chunks around a point are built and collide
  voxels.topY(x, z)              top of the highest block (NaN while not loaded)
  voxels.save(name)              the player's changes (only changed chunks) -> Documents\PremiumEngine\Saves\<game>\<name>.pgworld
  voxels.load(name)              puts them back over the generated world (after voxels.create and defineBlock). false + lastError
  voxels.deleteSave(name)        deletes that saved world file (true if there was one)
  voxels.saveData() / voxels.loadData(text)   the world's changes as text and back (send a world to a player who joins)
  voxels.setViewDistance(8)       how many chunks around the player are drawn (1 .. 32; a settings menu)
  voxels.items([id, x, y, z, sx, sy, sz, yaw, pitch, tile, r, g, b, ...])   small blocks drawn with the world until the next call (items on the ground,
                                 pieces flying off, a crack: tile = an atlas tile on every face, -1 = the block's own; 13 numbers each, Float32Array is fastest)
  voxels.stats                   { chunks, meshed, generating, meshing, triangles, bricks, generateMs, meshMs, memoryMB, changedChunks }
  world.voxels                   the same object
  world.player.swimming          true while the player is in a liquid (Space swims up)

  Example - break and build:
    const hit = voxels.raycast(world.camera.position, world.camera.forward, 8);
    if (hit && input.pressed('MouseLeft')) voxels.removeBlock(hit.point.clone().addScaledVector(hit.normal, -0.05));
    if (hit && input.pressed('MouseRight')) voxels.setBlock(hit.placePosition(0.2), 2, 0.2, { shape: 'stairs', rotation: 1 });

ARABIC TEXT
  world.hud, world.message and TextRender actors join Arabic letters and show them right to left, also mixed with English
  and numbers. HUD lines with Arabic sit on the right of the screen.

log('text', value)               writes to the Output Log
math.clamp(v, a, b)  math.lerp(a, b, t)  math.random(a, b)  math.vec3(x, y, z)
)";

static const char* DOCS_TEXT = R"(PREMIUM ENGINE - QUICK START

1. Create a project from a template in the Project Browser (Blank, First Person, Third Person).
2. Place actors: toolbar + (Quick Add), Actor > Place Actor, Window > Place Actors, or drag shapes from the Content Browser into the viewport.
3. Move them with the gizmo: W move, E rotate, R scale. Hold the right mouse button in the viewport and use W A S D Q E to fly.
4. Change an actor in the Details panel: mesh, material, physics, light, text, script.
5. Materials: Content Browser > Materials. Double-click to edit. Drag a material onto an actor in the viewport to apply it.
   Textures: the Material Editor's slots (Base Color, Normal, Roughness, Metallic, Ambient Occlusion, Emissive) take pictures of the project.
   Tiling repeats them; World Aligned lays them on the world so scaled walls and floors don't stretch; Masked cuts out leaves and fences.
   Roughness reads the green channel, Metallic the blue, Ambient Occlusion the red: a packed ORM picture goes in all three.
5b. Models: drop .glb / .gltf files (Blender, Mixamo, Sketchfab...) on the editor window, or Content Browser > Import. They go into
   the project's Models folder with their materials, textures, skeleton and animations. Drop one on the viewport to place it, or drag
   it from Content Browser > Models. Details > Animation picks the one that plays from the start; scripts and Blueprints play others.
   A model changed on disk (exported again) is loaded again by itself. Pictures go into Textures the same way.
   Export from Blender: File > Export > glTF 2.0 (.glb), +Y Up, with Animation; no Draco compression.
5h. Landscape: Place Actors > Landscape. Details > Landscape: Raise (Shift: lower), Lower, Smooth, Flatten, Noise, Paint (4 layers, each a
   material of the project; tiling in metres), Hills > Make, Size and detail. Hold the left mouse button on it in the viewport.
5f. Third Person: the player is the engine's mannequin (Idle, Walk, Run, Jump, Fall, picked by how it moves); World Settings > Player
   Character puts your own model there (its animations are found by name). The camera comes in front of walls. Scripts:
   world.player.set({ model: 'Models/Hero.glb' }).
5g. Blueprint functions: My Blueprint > Functions > + Function: its own graph, inputs and outputs, Pure or with exec pins; call it from
   the right-click menu (Functions).
5e. Reflections: place a Sphere Reflection Capture (Place Actors > Lights) in a room or a street: shiny floors, metal, glass and water
   within its radius reflect what it sees from there (the editor makes it again when the level changes; Details > Recapture).
5d. AI: characters walk on the level's floors with self.moveTo (scripts) or AI Move To / AI Move To Actor (Blueprints): round walls,
   up ramps and stairs, following the player. The floors come from the still meshes with collision; World Settings > Navigation sets
   the characters' size, step and slope. Show > Navigation (P) shows where they can walk.
5c. Camera effects: World Settings > Post Process - vignette, film grain, chromatic aberration, VHS tape, scanlines, saturation, contrast,
   temperature, bloom, tint (Found Footage and Cinematic presets). Scripts: world.setPostProcess; Blueprints: Set Camera Effect.
6. Scripts: Tools > New Script, or Details > Script > New Script. Press F7 to compile. See Help > Script API Reference.
7. Press Play (Alt+P) to play in the viewport. Esc stops. Shift+F1 frees the mouse.
8. Platforms > Windows packages the game as a Windows program (.exe) in Documents\PremiumEngine\Builds.

Projects are saved in Documents\PremiumEngine\Projects as .pge.json files.
They also open in Premium Engine 0.1 (web) and the other way round.
)";

static const char* SHORTCUTS[][2] = {
  {"Ctrl+S", "Save"}, {"Ctrl+Z / Ctrl+Y", "Undo / Redo"}, {"Ctrl+C / X / V", "Copy / Cut / Paste actors"}, {"Ctrl+D", "Duplicate"}, {"Delete", "Delete"}, {"F2", "Rename"},
  {"W / E / R", "Move / Rotate / Scale"}, {"F", "Focus selected"}, {"Home", "Frame all"}, {"End", "Snap to floor"}, {"H / Ctrl+H", "Hide selected / Show all"}, {"G", "Game view (hide icons)"},
  {"Esc", "Unselect / stop playing"}, {"Right mouse + W A S D Q E", "Fly the camera (wheel = speed, Shift = fast)"}, {"Middle mouse drag", "Pan"}, {"Alt + left mouse", "Orbit"},
  {"Mouse wheel", "Move forward / back"}, {"Alt+P", "Play / Stop"}, {"Shift+F1", "Free the mouse while playing"}, {"F7", "Compile scripts"}, {"Ctrl+Space", "Content Browser"},
  {"Ctrl+N / Ctrl+O", "New level / Open level"}, {"F1", "Documentation"}};

static bool beginDialog(const char* title, bool* open, ImVec2 size) {
  if (!*open) return false;
  ImGuiViewport* v = ImGui::GetMainViewport(); ImGui::SetNextWindowPos({v->Pos.x + v->Size.x / 2, v->Pos.y + v->Size.y / 2}, ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
  if (!ImGui::Begin(title, open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return false; }
  return true;
}

void Editor::drawDialogs() {
  float s = win.dpi;
  if (beginDialog("New Level", &wNewLevel, {380 * s, 0})) {
    ImGui::Text("Level name"); ImGui::SetNextItemWidth(-1); if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::InputText("##nl", &newName, ImGuiInputTextFlags_EnterReturnsTrue); static bool templ = false; ImGui::Checkbox("Start with a floor, sky and light", &templ);
    if (ImGui::Button("Create", {120, 0}) || go) { newLevel(newName, templ); wNewLevel = false; } ImGui::SameLine(); if (ImGui::Button("Cancel")) wNewLevel = false;
    ImGui::End(); }
  if (beginDialog("Open Level", &wOpenLevel, {360 * s, 300 * s})) {
    for (auto& [ln, lv] : P.levels) { std::string label = ln + "  (" + std::to_string(lv.actors.size()) + " actors)" + (ln == P.startLevel ? "  - start level" : "");
      if (ImGui::Selectable(label.c_str(), ln == level, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) { openLevel(ln); wOpenLevel = false; } }
    ImGui::TextDisabled("Double-click to open"); ImGui::End(); }
  if (beginDialog("New Blueprint", &wNewBlueprint, {420 * s, 0})) {
    ImGui::TextWrapped("A Blueprint is a script made of nodes and wires (like Unreal's). Put it on an actor in Details > Script.");
    ImGui::TextWrapped("%s", visualText("البلوبرنت سكربت تسويه بالعقد والأسلاك (مثل Unreal). تحطه على ممثل من Details > Script.").c_str());
    if (wNewBlueprint && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::InputText("Name", &newName, ImGuiInputTextFlags_EnterReturnsTrue); Actor* a = actor(sel); static bool assignB = true;
    if (a) ImGui::Checkbox(("Put it on " + a->name).c_str(), &assignB);
    if (ImGui::Button("Create", {120, 0}) || go) { std::string id = newBlueprint(newName); if (a && assignB) { a->script = id; touch(); } openBlueprint(id); wNewBlueprint = false; }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) wNewBlueprint = false; ImGui::End(); }
  if (beginDialog("New Script", &wNewScript, {380 * s, 0})) {
    ImGui::Text("Script name"); ImGui::SetNextItemWidth(-1); if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::InputText("##ns", &newName, ImGuiInputTextFlags_EnterReturnsTrue); Actor* a = actor(sel); static bool assign = true;
    if (a) ImGui::Checkbox(("Add it to " + a->name).c_str(), &assign);
    if (ImGui::Button("Create", {120, 0}) || go) { std::string id = newScript(newName); if (a && assign) { a->script = id; touch(); } openScript(id); wNewScript = false; }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) wNewScript = false; ImGui::End(); }
  if (beginDialog("New Material", &wNewMaterial, {380 * s, 0})) {
    ImGui::Text("Material name"); ImGui::SetNextItemWidth(-1); if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::InputText("##nm", &newName, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Create", {120, 0}) || go) { materialOpen = newMaterial(newName); wMaterial = true; Actor* a = actor(sel); if (a && a->type == "StaticMesh") { a->material = materialOpen; touch(); } wNewMaterial = false; }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) wNewMaterial = false; ImGui::End(); }
  if (beginDialog("Rename Actor", &wRename, {340 * s, 0})) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(); ImGui::SetNextItemWidth(-1); bool go = ImGui::InputText("##rn", &rename, ImGuiInputTextFlags_EnterReturnsTrue);
    if ((ImGui::Button("Rename", {120, 0}) || go) && !rename.empty()) { if (Actor* a = actor(sel)) { a->name = rename; touch(); } wRename = false; } ImGui::SameLine(); if (ImGui::Button("Cancel")) wRename = false; ImGui::End(); }
  if (beginDialog("Import Into Level", &wImport, {520 * s, 0})) {
    ImGui::TextWrapped("Adds every actor of a level from another Premium Engine project (.pge.json, also from 0.1 web) to the current level, with its materials and scripts.");
    static std::string path; ImGui::SetNextItemWidth(-90); ImGui::InputTextWithHint("##ip", "C:\\path\\to\\Project.pge.json", &path); ImGui::SameLine();
    if (ImGui::Button("Browse...")) { std::string f = openFileDialog("Import level", "Premium Engine project", "*.pge.json;*.json"); if (!f.empty()) path = f; }
    if (ImGui::Button("Import", {120, 0})) { Project q; std::string err;
      if (!loadProjectFile(path, q, &err)) notify("Could not read: " + err);
      else { const Level* lv = q.level(q.startLevel); int n = 0; std::map<std::string, std::string> ids; for (auto& a : lv->actors) ids[a.id] = newId("a");
        for (auto a : lv->actors) { a.id = ids[a.id]; if (!a.parent.empty()) a.parent = ids.count(a.parent) ? ids[a.parent] : ""; a.name = uniqueName(a.name); L().actors.push_back(a); n++; }
        for (auto& [id, m] : q.materials) if (!P.materials.count(id)) P.materials[id] = m; for (auto& [id, sc] : q.scripts) if (!P.scripts.count(id)) P.scripts[id] = sc;
        touch(); logMsg("Imported " + std::to_string(n) + " actors from " + q.name, LogKind::Ok); wImport = false; } }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) wImport = false; ImGui::End(); }
  if (beginDialog("Editor Preferences", &wPrefs, {440 * s, 0})) {
    bool ch = false;
    ch |= ImGui::Checkbox("Autosave", &prefs.autosave); ImGui::SameLine(); ImGui::SetNextItemWidth(120); ch |= ImGui::SliderInt("minutes", &prefs.autosaveMinutes, 1, 30);
    ch |= ImGui::Checkbox("Invert mouse Y when flying", &prefs.invertY);
    ImGui::SetNextItemWidth(160); ch |= ImGui::SliderFloat("Camera speed", &prefs.camSpeed, 1, 8, "%.0f");
    ImGui::SetNextItemWidth(160); if (ImGui::SliderFloat("UI scale", &prefs.uiScale, 0.75f, 2.0f, "%.2f")) { ImGui::GetIO().FontGlobalScale = prefs.uiScale; ch = true; }
    if (ImGui::Checkbox("Show FPS in the viewport", &prefs.showFps)) { showStats = prefs.showFps; ch = true; }
    if (ch) savePrefs();
    ImGui::Separator(); ImGui::TextDisabled("Projects: %s", projectsDir().c_str()); ImGui::End(); }
  if (beginDialog("Project Settings", &wProjectSettings, {480 * s, 0})) {
    bool ch = false; ImGui::SeparatorText("Description");
    ImGui::Text("Project Name"); ImGui::SetNextItemWidth(-1); ch |= ImGui::InputText("##pn", &P.name);
    ImGui::Text("Description"); ImGui::SetNextItemWidth(-1); ch |= ImGui::InputTextMultiline("##pd", &P.description, {-1, 70});
    ImGui::SeparatorText("Maps & Modes");
    ImGui::Text("Game Default Map"); ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sl", P.startLevel.c_str())) { for (auto& [ln, lv] : P.levels) if (ImGui::Selectable(ln.c_str(), ln == P.startLevel)) { P.startLevel = ln; ch = true; } ImGui::EndCombo(); }
    ImGui::SeparatorText("Packaging"); ImGui::TextWrapped("Windows: %s\\%s\\Windows\\%s.exe", buildsDir().c_str(), sanitizeName(P.name).c_str(), sanitizeName(P.name).c_str());
    ImGui::TextDisabled("Template: %s   Engine: %s   File: %s", P.templ.c_str(), PGE_VERSION, P.path.c_str());
    if (ch) { touch(); SDL_SetWindowTitle(win.win, (P.name + " - Premium Engine").c_str()); }
    ImGui::End(); }
  if (beginDialog("Statistics", &wStats, {420 * s, 0})) {
    std::map<std::string, int> types; int tris = 0; for (auto& a : L().actors) { types[actorTypeLabel(a.type)]++; if (a.type == "StaticMesh") tris += (int)meshShape(a.mesh).i.size() / 3; }
    if (ImGui::BeginTable("st", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
      auto row = [](const std::string& k, const std::string& v) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(k.c_str()); ImGui::TableNextColumn(); ImGui::TextUnformatted(v.c_str()); };
      row("Level", level); row("Actors", std::to_string(L().actors.size())); for (auto& [t, n] : types) row("  " + t, std::to_string(n));
      row("Triangles (meshes)", std::to_string(tris)); row("Draw calls (last frame)", std::to_string(R.drawCalls)); row("Levels", std::to_string(P.levels.size()));
      row("Materials", std::to_string(P.materials.size())); row("Scripts", std::to_string(P.scripts.size()));
      if (playing && game) { row("Game: actors", std::to_string(game->actorCount())); row("Game: physics bodies", std::to_string(game->physicsBodies())); }
      ImGui::EndTable(); }
    ImGui::End(); }
  if (beginDialog("Map Check", &wMapCheck, {620 * s, 320 * s})) {
    for (auto& m : mapCheck) { ImVec4 c = m.rfind("error", 0) == 0 ? ImVec4(1, 0.42f, 0.42f, 1) : m.rfind("warning", 0) == 0 ? ImVec4(1, 0.8f, 0.35f, 1) : ImVec4(0.6f, 0.9f, 0.6f, 1); ImGui::TextColored(c, "%s", m.c_str()); }
    ImGui::End(); }
  if (beginDialog("Package Project", &wPackage, {560 * s, 0})) {
    ImGui::TextWrapped("%s", packageResult.c_str()); ImGui::Spacing();
    if (packageOk) { if (!console) { if (ImGui::Button("Open Folder", {130, 0})) openInExplorer(packageDir); ImGui::SameLine(); }
      if (ImGui::Button("Run Game", {130, 0})) { std::string err; std::string exe = (fs::u8path(packageDir) / fs::u8path(sanitizeName(P.name) + exeSuffix())).u8string(); if (!launchProgram(exe, console ? std::vector<std::string>{} : std::vector<std::string>{"--windowed"}, &err)) notify(err); } ImGui::SameLine(); }
    if (ImGui::Button("Close", {100, 0})) wPackage = false; ImGui::End(); }
  if (beginDialog("Script API Reference", &wApi, {720 * s, 620 * s})) { ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 2 ? ImGui::GetIO().Fonts->Fonts[2] : ImGui::GetFont()); ImGui::TextUnformatted(API_TEXT); ImGui::PopFont(); ImGui::End(); }
  if (beginDialog("Documentation", &wDocs, {720 * s, 420 * s})) { ImGui::TextWrapped("%s", DOCS_TEXT); ImGui::End(); }
  if (beginDialog("Keyboard Shortcuts", &wShortcuts, {560 * s, 560 * s})) {
    if (ImGui::BeginTable("sc", 2, ImGuiTableFlags_RowBg)) { for (auto& r : SHORTCUTS) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.5f, 0.75f, 1, 1), "%s", r[0]); ImGui::TableNextColumn(); ImGui::TextUnformatted(r[1]); } ImGui::EndTable(); }
    ImGui::End(); }
  if (beginDialog("About Premium Engine", &wAbout, {460 * s, 0})) {
    ImGui::TextColored(ImVec4(0.0f, 0.44f, 0.88f, 1), "PREMIUM ENGINE"); ImGui::Text("Version %s (experimental)", PGE_VERSION); ImGui::Text("Made for Premium Games");
    ImGui::Separator(); ImGui::TextWrapped("C++17 - OpenGL 3.3 - SDL2 - Dear ImGui + ImGuizmo - Jolt Physics - QuickJS - nlohmann/json - glm - ImGuiColorTextEdit - Boost.Regex. All open source (MIT / zlib / Boost licences).");
    ImGui::End(); }
}

/* ------------------------------------------------------------------ Project Browser (shown when no project is open) */
void Editor::drawProjectBrowser() {
  ImGuiViewport* v = ImGui::GetMainViewport(); ImGui::SetNextWindowPos(v->WorkPos); ImGui::SetNextWindowSize(v->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {28, 22});
  ImGui::Begin("Project Browser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  ImFont* big = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont();
  ImGui::PushFont(big); ImGui::SetWindowFontScale(30.f / big->FontSize); ImGui::TextColored(ImVec4(0.0f, 0.5f, 1, 1), "PREMIUM ENGINE"); ImGui::SetWindowFontScale(1); ImGui::PopFont();
  ImGui::SameLine(); ImGui::TextDisabled("  %s  -  Unreal-style editor, C++ engine, games run as Windows programs", PGE_VERSION);
  ImGui::Separator(); ImGui::Spacing();
  float leftW = ImGui::GetContentRegionAvail().x * 0.5f, s = win.dpi;
  ImGui::BeginChild("recent", {leftW, 0}, ImGuiChildFlags_Border);
  ImGui::SeparatorText("RECENT PROJECTS");
  // projects in Documents/PremiumEngine/Projects + recent list
  struct Item { std::string path, name; fs::file_time_type t; };
  static std::vector<Item> items; static double scanned = -10; double now = SDL_GetTicks() / 1000.0;
  if (now - scanned > 3) { scanned = now; items.clear(); std::error_code ec; std::set<std::string> seen;
    auto add = [&](const fs::path& p) { std::string k = fs::weakly_canonical(p, ec).u8string(); if (seen.count(k) || !fs::exists(p, ec)) return; seen.insert(k); items.push_back({p.u8string(), p.stem().stem().u8string(), fs::last_write_time(p, ec)}); };
    for (auto& r : recent) add(fs::u8path(r));
    for (auto& d : fs::directory_iterator(fs::u8path(projectsDir()), ec)) if (d.is_directory()) for (auto& f : fs::directory_iterator(d.path(), ec)) { std::string n = f.path().filename().u8string(); if (n.size() > 9 && n.substr(n.size() - 9) == ".pge.json") add(f.path()); }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.t > b.t; }); }
  if (items.empty()) ImGui::TextDisabled("No projects yet. Create one on the right.");
  float tile = 150 * s; int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (tile + 12))), n = 0;
  for (auto& it : items) { if (n % cols) ImGui::SameLine(); n++; ImGui::PushID(it.path.c_str()); ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##p", false, ImGuiSelectableFlags_AllowDoubleClick, {tile, tile * 0.85f}) && (console || ImGui::IsMouseDoubleClicked(0))) pendingOpen_ = it.path;   // console: A opens
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDouble-click to open", it.path.c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled({p.x + 4, p.y + 4}, {p.x + tile - 4, p.y + tile * 0.6f}, IM_COL32(16, 40, 70, 255), 4);
    dl->AddText(big, 34, {p.x + 14, p.y + 14}, IM_COL32(0, 140, 255, 255), it.name.substr(0, 2).c_str());
    ImGui::PushClipRect(p, {p.x + tile, p.y + tile}, true); dl->AddText({p.x + 6, p.y + tile * 0.63f}, IM_COL32_WHITE, it.name.c_str()); ImGui::PopClipRect();
    ImGui::PopID(); }
  if (!console) {
  ImGui::Spacing(); ImGui::SeparatorText("OPEN");
  if (hasFileDialogs()) { if (ImGui::Button("Browse...", {140, 0})) { std::string f = openFileDialog("Open project", "Premium Engine project", "*.pge.json;*.json"); if (!f.empty()) pendingOpen_ = f; } }
  else { ImGui::SetNextItemWidth(-90); ImGui::InputTextWithHint("##op", "/path/to/Project.pge.json", pathBuf_, sizeof pathBuf_); ImGui::SameLine(); if (ImGui::Button("Open")) pendingOpen_ = pathBuf_; }
  ImGui::TextDisabled("Projects from Premium Engine 0.1 (web) open here too."); ImGui::TextDisabled("You can also drop a .pge.json file on this window.");
  }
  ImGui::EndChild(); ImGui::SameLine();
  ImGui::BeginChild("new", {0, 0}, ImGuiChildFlags_Border);
  ImGui::SeparatorText("NEW PROJECT - GAMES");
  const char* names[] = {"Blank", "First Person", "Third Person", "Block World"}; const char* ids[] = {"Blank", "FirstPerson", "ThirdPerson", "Blocks"};
  const char* desc[] = {"An empty level with a floor, a sun and a Player Start. Spectator camera.",
    "Walk and jump in first person: ramp, stairs, physics boxes, coins to collect with a score, a jump pad and a goal. Scripts included.",
    "The same playground with a third person camera behind the player character.",
    "A BLOCKFALL-style world made of blocks: 20 cm blocks near the surface, 1 m blocks below and in the mountains, built in chunks on background threads. Break and place blocks."};
  for (int i = 0; i < 4; i++) { ImGui::PushID(i); ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x, h = 64 * s;
    if (ImGui::Selectable("##t", browserTemplate_ == i, ImGuiSelectableFlags_AllowDoubleClick, {w, h})) browserTemplate_ = i;
    ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled({p.x + 4, p.y + 4}, {p.x + h - 4, p.y + h - 4}, IM_COL32(20, 50, 90, 255), 4);
    dl->AddText(big, 26, {p.x + 14, p.y + h / 2 - 14}, IM_COL32(120, 190, 255, 255), i == 0 ? "B" : i == 1 ? "1P" : i == 2 ? "3P" : "BW");
    dl->AddText({p.x + h + 6, p.y + 8}, IM_COL32_WHITE, names[i]);
    ImGui::PushClipRect({p.x + h, p.y}, {p.x + w, p.y + h}, true); dl->AddText(nullptr, 0, {p.x + h + 6, p.y + 28}, ImGui::GetColorU32(ImGuiCol_TextDisabled), desc[i], nullptr, w - h - 12); ImGui::PopClipRect();
    ImGui::PopID(); }
  ImGui::Spacing(); ImGui::SeparatorText("PROJECT");
  ImGui::Text("Name"); ImGui::SetNextItemWidth(-1); if (focusNameField) { ImGui::SetKeyboardFocusHere(); focusNameField = false; } ImGui::InputText("##bn", &browserName_);
  ImGui::TextDisabled("Folder: %s", (fs::u8path(projectsDir()) / fs::u8path(sanitizeName(browserName_))).u8string().c_str());
  ImGui::Spacing();
  if (ImGui::Button("Create", {160 * s, 34 * s})) { browserErr_.clear(); newProject(ids[browserTemplate_], browserName_, &browserErr_); }
  if (!browserErr_.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", browserErr_.c_str());
  ImGui::EndChild();
  ImGui::End();
  if (!pendingOpen_.empty()) { std::string f = pendingOpen_; pendingOpen_.clear(); if (!openProjectFile(f)) browserErr_ = "Could not open " + f; }
}
}
