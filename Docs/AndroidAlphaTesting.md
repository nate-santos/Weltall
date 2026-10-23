# Private Android alpha testing

This guide is for the owner's manually shared friend-testing APK. The general Android port remains beta. The limited alpha.2 Shipping APK is prepared for manual friend testing. It includes both storage reductions and keeps the existing signing identity for updates.

Build: **0.8.3-android-alpha.2 (e7eae3009efb)**, Android version code **3**. APK: **989,192,294 bytes** (989 MB / 943.37 MiB). SHA-256:

```text
0e536c5dba5339a493f733c7251146b76ea54ea05853afb789bb834647b73418
```

Allow **4 GB free before downloading** as a practical recommendation for the download, installation/update staging and app data. This is headroom derived from the measured package/components, not a tested minimum free-space boundary. The existing Moto reports **1.51 GB total** in Android Settings: **1.09 GB app**, **415 MB user data**, **98.30 kB cache**, rounded by Android. That installation retains historical test data and logs. Saves, caches and driver data can change the total during play; there are no private whole-disc duplicates.

## Install and update

Use a compatible ARM64 Android phone with Vulkan and ASTC support. This APK requires Android 12 or newer (API 31; target API 36). The Moto G Stylus 2025 is the tested phone. Other phones and GPU families need testing.

Download the complete APK, then open it from your download or file-sharing app. If Android asks, allow that particular app to install unknown apps, then return and finish installation. USB debugging and a computer are unnecessary. Keep enough free space for the downloaded APK, installed app and temporary installation/update overhead; use the recommendation accompanying that exact release.

Both discs are included and play offline. On first launch, leave the game open while it verifies them. It then reads them from the packaged data without another whole-disc copy in user data. An update from an older build may verify and reclaim its old app-generated copies. Saves and settings are preserved.

For updates, install the newer owner-supplied APK over Weltall. Keep the existing app installed. If Android reports a signature or version error, report it to the owner. Uninstalling removes private saves/settings; export your memory cards before any optional reinstall. The original game's Options → Saves page offers card export/import through Android's document picker. Keep your own exported save backups.

## Playing

Hold the phone horizontally; picture and controls follow either landscape direction. Tap **Controls** to adjust idle/pressed transparency, button size, side areas and positions. That button follows the opacity setting too; a touch in its area still opens it when it is transparent. Choose your preferred volume.

Pair a Bluetooth controller in Android settings or connect a USB controller, then return to Weltall. The Stadia has been tested. You can mix controller and touch input. Test disconnecting and reconnecting during a natural break, checking that movement stops and touch controls remain responsive. Android/controller system actions may return Home; reopen Weltall if that happens.

Play normally: explore, read dialogue, try character and Gear battles, save to an empty slot and reload. Try movies, both discs as progression permits, landscape flips and brief Home/return interruptions. A rare Android-forced process restart returns to the title; load a saved game to continue. Unsaved play does not survive process death.

## Reporting a problem

Open **Controls → Export diagnostic report**, choose a location in Android's picker, and send that JSON file to the owner with your report. Export is optional and never uploads automatically. It includes build/device/storage information, a bounded game-log tail and save/settings filenames and fingerprints. It excludes save contents and disc bytes. You may inspect it before sharing.

Include:

- Build label shown at the top of Controls; phone model and Android version.
- Controller/headphones, if relevant; disc, area and nearest save/checkpoint.
- What you did, what you expected, what happened, and whether it repeats.
- A screenshot or short clip if it helps explain a rendering or input issue.

Prioritize crashes, lost saves, blocked progress, stuck controls, audio faults, incorrect pictures and repeatable stalls. Small isolated timing differences do not need investigation.

## Current coverage limits

The full game has not been played through on Android. Only the Moto/Adreno GPU and Stadia controller have physical test coverage. Longer progression, a normal competitive Battling match/results and broader phones/controllers remain friend-testing work. Short representative battle comparisons retain original picture quality and show no meaningful slowdown from the smaller storage layout; they do not promise 60 FPS in every scene or on every phone.

The owner delivery folder contains only the signed APK, SHA-256 checksum, exact release notes and this guide. Keys, matching private native symbols, standalone discs, player saves and internal captures stay on the development computer.

## First fresh-device check

Normal installation through Files and an in-place update from version code 2 to 3 passed on the Moto. Its saves/settings were retained at installation. A separate clean Android user or second phone was unavailable; the normal Moto was never wiped. Synthetic clean-initialization fixtures and both real-disc logical verification passed, but a fresh-device first launch of this exact Shipping APK remains a first-tester check under the plan's fallback.

For the first friend: install the complete APK, launch offline, leave verification open until it finishes, then confirm New Game and the opening movie work, save into an empty slot and reload after restarting. Export a diagnostic report for the owner. Confirm that a second launch does not repeat whole-disc preparation. Test ordinary audio at your preferred volume; this final Moto Shipping run stayed muted and verified active audio output without a new listening confirmation.
