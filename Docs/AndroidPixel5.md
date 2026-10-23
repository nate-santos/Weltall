# Pixel 5 compatibility and battle validation

## Touch layout default (alpha.2.2 / code5)

`06d6b42` enables reserved side areas by default and stacks Select above Start in the right control area. Fresh settings and Reset layout use this arrangement at every device size; saved side-area preferences and custom offsets still load normally. The touch suite passes all 12 cases / 441 assertions, including default placement outside the picture at 1280x720, 1920x1080 and 2400x1080 and retention of an explicitly disabled side-area preference.

The signed Shipping update is installed on the Pixel, with its exact APK SHA-256 verified as `ed1691dcc4aebcf63f9c3d7e01d2de9b49a46ea7a37eba3c879437a95f5c76de`. Signature/same-signer, 16KB alignment and actual embedded-pak integrity/both-disc listing checks pass. Existing Pixel settings had side areas disabled; this one preference was enabled through the Controls panel before the update. After installation, the panel confirms side areas on, existing idle 65% / pressed 55% / size 100% retained, and the new source revision. Card1 No03 loads through Continue and the Disc2 confirmation; both buttons remain outside the picture in the field. The phone is left playable there without writing a new save. Local evidence: `out/testing/pixel5/right-controls-code5/`, `right-controls-apk/` and `right-controls-final.png`. The Moto was not updated.

## Compatibility and performance (alpha.2.1 / code4)

October 23, 2026. The compatibility fix is installed and tested on the Pixel 5. Default-quality Gear-battle presentation is stable in the bounded measurements below.

The newly connected Pixel 5 (Android 14, Adreno 620) installs alpha.2/code3 but selects Unreal's `Android_Adreno6xx` profile. Its OpenGL driver reports V@0490.0 (November 2020), below the engine rule's V@500 threshold for choosing `Android_Adreno6xx_Vulkan`. The earlier selector-plugin fix `736f9e9` is present; it alone does not handle this fallback. The generic ES 3.1 packaging error does not mean Vulkan is absent: the device reports Vulkan 1.1.128.

`a851d57` sets the project's base Android profile to prefer Vulkan, matching the package's Vulkan-only shader formats. Actual Vulkan driver/RHI availability checks remain enabled. Code4 is the compatibility candidate, signed with the existing key; code3 remains the previously qualified alpha artifact.

With Vulkan selected, startup movies render, but the original field pipeline fails to link on this driver. A diagnostic build with shader names identifies `FFieldSceneVS` / `FFieldScenePS`. The driver logs `Assertion failed: (*mSymbolObjectMap)[gv] == NULL`; Unreal reports graphics-pipeline failure and exits. Disabling global shader precompilation does not fix this failure. Do not describe that setting as a proven workaround.

A private diagnostic variant removing the field pixel shader's sample-mask input reaches the title and remains alive. This variant is diagnostic only: replacing multisample coverage with a constant is not the committed fix.

`5339d3d` instead specializes the field pixel shader by the render target's actual sample count. Single-sample rendering omits both sample-mask input and output; multisample rendering retains the existing shader behavior. Android defaults to no MSAA. This targets the unused shader interface without changing default resolution, filtering, blending or simulation. Optional MSAA on this old driver remains unqualified.

The editor build succeeds and all 69 existing rendering regression cases pass: 68 clean successes plus the existing texture-replacement fixture's two expected warnings, no failures/unrun. Local evidence is `out/testing/pixel5/pc-rendering/`.

The committed Development APK is built and its exact installed hash is verified in `out/testing/pixel5/committed-single-sample/identity.json`. The actual native `FieldSceneFollowsTheGpusRules` case passes, zero warnings/failures/unrun, 0.712 test seconds; save/settings fingerprints remain exact (`native-field-1792774214512892400/` under the local evidence folder). A replacement USB cable resolves the repeated transport interruptions sufficiently for the completed checks.

The installed Shipping/non-debuggable APK is `out/android/packages/Shipping/20261023T164630Z-bccef12e/Weltall-Android-Shipping-arm64.apk`, source `5339d3d2f9ba`, manifest code4 / `0.8.3-android-alpha.2.1`, 989,201,126 bytes, SHA256 `56fb4100dabd2505401ac55fe0aaae1436833e3c4fcb1d6bf250fd5a7e9605bd`. Its package retains the existing signer and passes actual embedded PAK integrity/both-disc listing, ZIP16KiB and native alignment/symbol verification. The exact installed hash is verified (`shipping-single-sample/identity.json`, `shipping-inspection/`). The in-game project-version text still says alpha.2, with the correct source revision visible in Controls; use the manifest/code and revision to identify this candidate.

The Pixel initially had no Weltall installation or saves. A valid local Disc2 checkpoint is copied into previously empty Card1 No03 for normal loading; its checksum and unchanged settings are recorded in `out/testing/pixel5/checkpoint-staging.json`. Never overwrite this slot or later player progress. The Moto and its protected files are untouched.

Normal Shipping cold loading of Card1 No03 reaches the Disc2 save point. Ordinary touch movement enters a two-enemy Gear battle; reviewed field/menu/battle images show the expected output. Physical Cross confirms menus; confusing internal logical button names caused earlier navigation attempts to time out into the title's attract movie. That was a test-navigation error, not evidence of broken touch input.

## Bounded battle measurements

Pixel Android14 does not expose useful `SurfaceFlinger --list` / per-layer `--latency` samples here. The successful captures use SurfaceFlinger **per-layer TimeStats**, including complete quantized present-to-present histograms for the live game SurfaceView. Do not label these raw frame timestamps, Unreal CPU/GPU timing, or directly equivalent to the Moto's earlier measurement method/settings.

| Workload | Consecutive 30-second window FPS | Weighted FPS | Layer frames |
| --- | --- | --- | --- |
| Animated two-enemy Gear battle/menu | 46.259, 46.066, 46.315, 46.371, 45.948, 46.392 | 46.225 | 8,123 |
| Normal attack inputs and changing battle camera/menu | 53.097, 59.291 | 56.188 | 3,259 |

The menu capture spans 182.469 host seconds; the attack capture 60.797 seconds. Every recorded present interval is in the 16/17/32/33ms histogram bins for the first capture, or 16/17/33ms bins for the second. Neither has a recorded long-stall bucket. Each capture retains the same process and foreground app. All sampled Android thermal statuses and sensor severities are zero. Settings/quality/resolution and volume were not lowered or changed for these captures. Camera/content changes explain why the attack workload is not a matched FPS comparison with the fixed menu workload.

Evidence: `out/testing/pixel5/timestats-gear-menu/` and `timestats-gear-attacks/`, including original per-window statistics, thermal samples, summaries and start/end images. Helpers are private in that same ignored tree. This establishes short-session default-quality behavior in this scene, not locked60fps, whole-game performance, equilibrium/battery life or optional-MSAA compatibility.

No builder, automation input or profiler remains active. The phone is left playable in the ordinary unsaved Gear battle, with the checkpoint retained. The launch override is removed, TimeStats collection disabled and the original stay-awake setting 0 restored. No game-derived evidence or APK is committed or uploaded.

Next: ordinary alpha play and feedback, additional representative effects/areas as useful, and separate qualification before enabling optional MSAA on this old driver. Pixel and Moto are both Adreno; this adds device/driver coverage, not Mali coverage. The Moto remains on its preceding installed build; it was not updated during this Pixel task.
