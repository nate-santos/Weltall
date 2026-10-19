# Local gameplay checkpoints

At the user's request on October 19, 2026, later-story saves now provide the normal starting points for Android validation. Replaying the opening is unnecessary unless a startup/prologue change needs testing. These are original memory-card saves loaded through the native menu, not emulator save states or staged map/party/story values.

The [GameFAQs North American save listing](https://gamefaqs.gamespot.com/ps/199365-xenogears/saves) supplied five DexDrive cards. All 56 saves have the USA game identifier and valid original-game checksums. Downloads, extracted saves, provenance/hashes, screenshots and logs remain exclusively under ignored `out/testing/`; none belongs in Git or CI.

| Download / contributor | Local save count | Validation use | Moto location |
| --- | --- | --- | --- |
| [12349 / Duke_Darkwood](https://gamefaqs.gamespot.com/ps/199365-xenogears/saves/12349) | 12 | Disc 1 checkpoints from the cave through later chapters | Imported byte-identically into Card 3, No. 01–12 |
| [14932 / Unos_Hambalos](https://gamefaqs.gamespot.com/ps/199365-xenogears/saves/14932) | 15 | Disc 1 story and Battling checkpoints | Downloaded locally; not imported yet |
| [14935 / Unos_Hambalos](https://gamefaqs.gamespot.com/ps/199365-xenogears/saves/14935) | 15 | Later Disc 1 / Disc 2 checkpoints | Downloaded locally; not imported yet |
| [14936 / Unos_Hambalos](https://gamefaqs.gamespot.com/ps/199365-xenogears/saves/14936) | 13 | Disc 2 checkpoints | Downloaded locally; not imported yet |
| [3683 / Mykas0](https://gamefaqs.gamespot.com/ps/199365-xenogears/saves/3683) | 1 | Late Disc 2 entry near a save point | Imported byte-identically into Card 4, No. 01 |

Local catalog: `out/testing/save-documents/gamefaqs/manifest.json`. Each download is `xenogears.ID.gme`; extracted original save bytes are in `ID/Saves/Card1/NoNN.sav`. The catalog records source URLs, contributor names, file/save SHA-256 values, story progress, party and levels. Copies also remain in the browser's local Downloads folder. Do not rely on website descriptions for the exact restored location: save 3683 is described as a world-map save but currently loads inside the late dungeon, so it is not a world-map acceptance result.

## Verified runtime checks

- Duke_Darkwood No. 01 loads into the cave on PC and Moto through the normal Load flow. The selected save hash is `ba142f3e32f9e38bbbb8e1230d332cbabd938418940ec682ff65d6b59d891c2a`. Android's original Cards 1/2 stay unchanged; all 12 imported Card 3 files match their source bytes (`device-import-card3.json`).
- On the Moto, a 350 ms Left hold moves toward the cave's save point. Square opens the native menu; File offers Save at that point. A new save was written into previously empty Card 5, No. 01, without staging menu availability. It has a valid checksum, story progress 52 and SHA-256 `c7fcb1bb9b0f83358ecf81ac7275ce00f3792b508efd64817b7c35146eba1c3e`. Android reloads it successfully; PC Continue also loads it successfully with bytes unchanged. This is one real legal-save-point check, not complete save/lifecycle coverage.
- Mykas0 No. 01 loads on both platforms after the normal Disc 2 prompt is confirmed. Android uses the already bundled second disc in the same process. The selected save hash is `a94b91fddae2c3a509487ea8a0a58f6b13e23ebf505eac979166d202be1159d6` (`device-import-card4.json`). This validates a Disc 2 save entry, not the original story's Disc 1 → 2 transition. The party has low HP in this source save; heal through normal game actions before testing combat.

After all these operations, all 16 expected phone save files still match their references byte for byte: the two initial fixtures, 12 Disc 1 imports, one Disc 2 import and the new native cave save (`device-final-save-retention.json`).

PC evidence is under `out/testing/unreal/pc-game-reference/`: `disc1-downloaded-cave-load`, `disc1-android-cave-save-pc-load`, and `disc2-downloaded-world-confirmed`. All exit 0. The earlier `disc2-downloaded-world-load` remains at the expected disc prompt and is not a completed load. Local `save_reference.py` records inputs and source hash in each request, then preserves the selected save bytes while loading through Continue. `--disc-confirm` adds inputs for the disc prompt; `--source` accepts a local save under `out/` for cross-platform comparison.

Phone evidence is under `out/testing/android-app/`: `disc1-downloaded-cave-loaded`, `cave-walk-toward-savepoint`, `cave-file-save-availability`, `cave-save-free-block-confirm`, `cave-save-finished`, `cave-real-save`, `cave-own-save-reloaded`, `disc2-card4-load-selection`, `disc2-required-prompt`, and `disc2-downloaded-loaded`. Some earlier `disc2-import-*` attempts never opened the system picker; they are not successful import evidence. The later `disc2-card4-preview`, verified file bytes and subsequent runtime load establish success.

## Resume procedure

Square opens the field menu; Triangle jumps. Enter Options → Saves to import a selected GME through Android's picker. Choose an empty card and inspect the preview before copying. Imports keep their original slot numbers. Preserve Cards 1/2's initial transfer fixtures, Card 3's downloaded collection, Card 4's Disc 2 checkpoint and Card 5's new gameplay save until they are no longer needed. Never delete modified or additional user saves.

The native File screen shows two card slots and four dots for its four pairs of stores. L1/R1 changes the pair: first dot is Cards 1/2, second 3/4, third 5/6, fourth 7/8. The original on-screen CARD 1/2 labels name the two slots within the selected pair. Menu opening/closing animations consume time; the local touch helper's `--gap 1` spaces inputs safely. Inspect the current screen before assuming a remembered menu selection. Loading asks Yes/No horizontally: Left selects Yes, then Confirm; dismiss Finished and confirm INSERT DISC 2 when requested.

The last phone state is the loaded Disc 2 checkpoint, with Controls closed and default touch appearance. All PC reference processes have exited. Continue with actual field navigation, character/Gear battles, world-map movement, Battling, original story disc transition, lifecycle interruptions and extended sessions using these checkpoints. Downloaded saves supplement the required extended playthrough; they do not establish alpha readiness alone.
