# Android touch controls

The phone overlay is original project code drawn with Unreal Slate lines and text. It uses no external controller package, copied skin, game texture or game-derived fixture.

The default layout puts the D-pad at the lower left, four face buttons at the lower right, shoulders along the upper edges, and compact Start/Select controls at the bottom. L3/R3 are available as well. D-pad diagonals and multiple simultaneous action touches are supported. Physical keyboard/controller state and touch state are combined independently, so lifting a finger cannot release a button still held on hardware.

Circle cancels the existing hardware-key binding prompt when it is waiting for a physical key. Touch controls keep their logical game actions independently of those bindings.

A fresh Android install uses the original button symbols so game prompts match the overlay. The on-screen pad remains connected to the game when a physical controller is unplugged.

Android Back closes the touch settings/edit panel first, cancels a hardware-key prompt next, or acts as the game's Circle button. It is consumed by the controller and does not invoke an immediate application exit. A short system gesture is retained for one game input sample even if down/up arrive together; lifecycle resets clear it.

Tap **Controls** to pause the game and open appearance settings. Idle and pressed visibility start at 25% and 55%. The minus/plus targets change visibility in 5% steps; zero opacity keeps touch targets active. The Controls button remains visible for recovery. Size adjusts from 70% to 140%.

**Side areas** reserves left/right areas for the controls and fits the game picture into the remaining center area. The default Start/Select placement moves into those areas too. In **Move controls**, drag any control and tap **Done** to save. Positions are stored as normalized offsets from the default layout, so they survive resolution changes. Reset restores both appearance and positions. Deliberately moving a control into the picture or over another control is allowed; the last-drawn overlapping control receives a new touch.

The overlay is installed only on Android and is wrapped in `SSafeZone`. Layout changes, application deactivation/backgrounding, orientation notifications and individual pointer capture loss release affected touch state. The Android platform configuration selects sensor landscape; both landscape sides share the same layout relative to the screen's safe area.

## Validation status

A touch which begins and ends between game updates could previously be missed. The correction retains per-pointer presses until one game sample while keeping visual held state immediate; capture cancellation removes only that active pointer's pending press. Reset/layout/lifecycle changes discard all pending touches. The native regression covers every button, ordinary release-capture notifications, cancellation with other fingers held, directional sweeps and reset. Windows and Moto ARM64 each pass 11 touch cases and 415 assertions with zero failures/skips (`out/testing/touch-quick-tap-windows.xml`, `out/testing/android/20261019T000032Z-2c24e6b8/summary.json`). The installed `56affc5` APK also accepts zero-duration device touches through the overlay to navigate Options and Saves.

Device checks already observed: appearance settings open, idle/pressed transparency and size adjust, reserved side areas move the controls beside the picture, Android Back closes the panel, and settings remain byte-identical through a signed APK update. Invisible targets still respond, a dragged Cross target works at its new position after restart, and the visible Controls button recovers the settings panel at zero displayed opacity. Reset restores the original defaults. A three-pointer touchscreen sequence keeps D-pad/Square/Triangle held; releasing Square preserves the other two, and cancellation clears all. Captures are under `out/testing/android-app/multitouch-final-*`. A short background/resume preserves the process and restores the game's own keep-screen-on flag. These checks do not replace field/battle combinations, rotation, controller or extended-gameplay testing.

Engine-independent tests in `Tests/WeltallCore/Input/TouchPadTests.cpp` cover diagonal movement with two action fingers, independent releases, hardware/touch merging, all buttons, pointer reuse, dragging outside a button, rotation/layout resets, safe-area geometry, unobstructed default side-area placement, zero-opacity hit testing and settings persistence/clamping. They contain only synthetic coordinates and authored settings.

The complete core and test runner compile for Android ARM64 with NDK Clang and warnings treated as errors. The 11-case device regression includes Back gesture retention and locale-independent float parsing. Native tests validate the input/layout model; the installed Unreal build adds direct evidence for ordinary single-touch navigation and appearance settings.

Both forced landscape rotations render and place the Controls panel within the corresponding safe area; Android Back closes it in both. The process remains alive and the test restores the original rotation settings (`out/testing/android-app/rotation-probe/summary.json`). The user also confirms that physically turning the Moto 180 degrees rotates the picture and controls together. A connected Stadia controller's D-pad, B and A work by user confirmation; after it is turned off, touch settings and Back still work without restarting the app. Combined hardware/touch holds, disconnection during a held action, field/battle combinations and physical button comfort still need testing. Movie fading, automatic hiding when a physical controller is used, camera gestures, per-control scaling and landscape-side locking are follow-up features.

API references checked against Epic's UE 5.8 documentation:

- [Slate widget touch and capture events](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/SWidget)
- [Capture-loss pointer identity](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FCaptureLostEvent)
- [Safe area layout](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Slate/SSafeZone)
- [Application lifecycle and orientation delegates](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FCoreDelegates)
- [Slate line drawing](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FSlateDrawElement/MakeLines)
