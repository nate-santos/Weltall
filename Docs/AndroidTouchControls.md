# Android touch controls

The phone overlay is original project code drawn with Unreal Slate lines and text. It uses no external controller package, copied skin, game texture or game-derived fixture.

The default layout puts the D-pad at the lower left, four face buttons at the lower right, shoulders along the upper edges, and compact Start/Select controls at the bottom. L3/R3 are available as well. D-pad diagonals and multiple simultaneous action touches are supported. Physical keyboard/controller state and touch state are combined independently, so lifting a finger cannot release a button still held on hardware.

Circle cancels the existing hardware-key binding prompt when it is waiting for a physical key. Touch controls keep their logical game actions independently of those bindings.

Android Back closes the touch settings/edit panel first, cancels a hardware-key prompt next, or acts as the game's Circle button. It is consumed by the controller and does not invoke an immediate application exit. A short system gesture is retained for one game input sample even if down/up arrive together; lifecycle resets clear it.

Tap **Controls** to pause the game and open appearance settings. Idle and pressed visibility start at 25% and 55%. The minus/plus targets change visibility in 5% steps; zero opacity keeps touch targets active. The Controls button remains visible for recovery. Size adjusts from 70% to 140%.

**Side areas** reserves left/right areas for the controls and fits the game picture into the remaining center area. The default Start/Select placement moves into those areas too. In **Move controls**, drag any control and tap **Done** to save. Positions are stored as normalized offsets from the default layout, so they survive resolution changes. Reset restores both appearance and positions. Deliberately moving a control into the picture or over another control is allowed; the last-drawn overlapping control receives a new touch.

The overlay is installed only on Android and is wrapped in `SSafeZone`. Layout changes, application deactivation/backgrounding, orientation notifications and individual pointer capture loss release affected touch state. The Android platform configuration selects sensor landscape; both landscape sides share the same layout relative to the screen's safe area.

## Validation status

Engine-independent tests in `Tests/WeltallCore/Input/TouchPadTests.cpp` cover diagonal movement with two action fingers, independent releases, hardware/touch merging, all buttons, pointer reuse, dragging outside a button, rotation/layout resets, safe-area geometry, unobstructed default side-area placement, zero-opacity hit testing and settings persistence/clamping. They contain only synthetic coordinates and authored settings.

The complete core and test runner compile for Android ARM64 with NDK Clang and warnings treated as errors. On October 17, 2026, all 10 touch tests passed on the connected Android phone with no failures or skips, including Back gesture retention and locale-independent float parsing. This validates the input/layout model on ARM64; the Slate overlay itself still needs the Unreal build below.

Unreal compilation and real-device tests are still required to verify Slate composition over the custom renderer, Android pointer capture/cancellation, physical button size, cutout/navigation insets after a 180-degree rotation and persistence through app updates. Movie fading, automatic hiding when a physical controller is used, camera gestures, per-control scaling and landscape-side locking are follow-up features.

API references checked against Epic's UE 5.8 documentation:

- [Slate widget touch and capture events](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/SWidget)
- [Capture-loss pointer identity](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FCaptureLostEvent)
- [Safe area layout](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Slate/SSafeZone)
- [Application lifecycle and orientation delegates](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FCoreDelegates)
- [Slate line drawing](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FSlateDrawElement/MakeLines)
