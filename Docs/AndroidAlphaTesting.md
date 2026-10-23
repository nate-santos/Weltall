# Private Android alpha testing

This guide is for the owner's manually shared friend-testing APK. The general Android port remains beta. The storage candidate is still undergoing final Shipping qualification; a source build alone is not a qualified distribution. The final delivery folder must include its exact build label, checksum, download size, measured space recommendation and known gaps before it is sent.

## Install and update

Use a compatible ARM64 Android phone with Vulkan support. The current project minimum is Android 12; confirm that against the delivered APK's release notes. The Moto G Stylus 2025 is the tested phone. Other phones and GPU families need testing.

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

The final owner delivery folder should contain only the signed APK, SHA-256 checksum, exact release notes and this guide. Keys, matching private native symbols, standalone discs, player saves and internal captures stay on the development computer.
