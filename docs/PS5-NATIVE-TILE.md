# PS5 native tile research and hardware gate

Research snapshot: 2026-10-06.

## Primary upstream sources reviewed

- `ps5-payload-dev/tv4play` at `69fb2dff3de3d709e090b4742c7833a4aa521aba`
  - `main.c` embeds `sce_sys/param.json`, artwork and WebApp files;
  - writes an empty `/system_ex/app/BREW10004/eboot.bin` plus metadata after remounting `system_ex`;
  - writes WebApp files under `/user/app/BREW10004/`;
  - resolves private NID `Wudg3Xe3heE` for `sceAppInstUtilAppInstallTitleDir` and falls back to `sceAppInstUtilAppInstallAll`;
  - calls `sceAppInstUtilAppUnInstall` before its reinstall;
  - uses Web Based Media App category `66048` and `webAppUri`;
  - its short URL resolves to `http://localhost:8080`, where official `websrv` exposes `/fs/user/app/...`.
- `ps5-payload-dev/websrv` at `1afd476c5044d68df4e45dc773769e5de3b2cdd6`
  - launcher metadata uses a direct `deeplinkUri` to `http://127.0.0.1:8080`;
  - installer writes `/user/app/FAKE00000/sce_sys/{param.json,icon0.png}` and registers `/user/app/` through the same AppInstUtil NID/fallback;
  - `hbldr` is a different path: it borrows a BigApp slot and is the source of the already-running/infinite-loading failure AudioBridge must avoid;
  - README explicitly notes that Homebrew can crash when a previous instance is already running.

Additional implementation cross-check: the self-hosted `ps5-web-file-manager` installer at `3659cbbc40022e32229f9919f045018a988f2ef5` uses Media category `65536`, a direct `deeplinkUri`, only `/user/app`, and the same resolved install function.

## AudioBridge decision

AudioBridge combines only the tested pieces needed for a self-hosted daemon UI:

- unique candidate Title ID `ABRG18195`;
- Media category `65536`;
- direct `deeplinkUri` `http://127.0.0.1:18195/`;
- `/user/app/ABRG18195/sce_sys` source files;
- AppInstUtil registration without `hbldr`, Homebrew Launcher or runtime `websrv`;
- no `/system_ex` remount because no WebApp files need websrv's `/fs` route;
- no uninstall-before-update: owned files are atomically refreshed, then the title is registered again;
- conservative collision refusal when the Title ID exists without AudioBridge's ownership marker;
- separate exact-path uninstaller; `/data/fgg-playpods-gui` is out of its scope.

There is no authoritative public registry for PS5 homebrew Title IDs. A GitHub search found no existing `ABRG18195` on the research date. This is risk reduction, not proof of global uniqueness.

## WebApp, Media app and BigApp boundaries

- `applicationCategoryType: 66048` is a Web Based Media App. The reviewed tv4play form depends on an HTTP origin that can serve its installed files; upstream uses localhost websrv.
- `applicationCategoryType: 65536` is a Prospero Native Media App. Community-tested launchers use it with `deeplinkUri` for a direct browser URL.
- A tile URI opens WebKit; it does not execute the raw AudioBridge ELF or restart a dead backend.
- `hbldr` launches native homebrew in a borrowed BigApp slot. AudioBridge does not need a display/audio BigApp process because its Web GUI is served by a headless payload, and the real PS5 test already showed the long-lived backend works through elfldr.

## Firmware-dependent risks

These interfaces and formats are not public Sony SDK contracts:

1. `sceAppInstUtilAppInstallTitleDir` is resolved through a private NID and may be absent or change behavior.
2. The `InstallAll` fallback may index differently across firmware.
3. Dashboard placement, metadata refresh/caching and `ru-RU` localization can vary.
4. Some firmware may reject category `65536` with only `deeplinkUri`, even though current reference implementations use it.
5. The exact WebKit error when localhost has no listener is system-owned. No local HTML can be shown when the HTTP payload is not running.
6. AppInstUtil uninstall may remove source directories itself or may leave them. AudioBridge follows it with only known-file `unlink` and empty-directory `rmdir` calls.
7. A stale `/user/appmeta/ABRG18195` without the owned source marker is treated as a collision. Recovery must be manual until its origin is confirmed; the payload will not guess.

## Required real-PS5 gate

Record firmware, exploit/elfldr version and the complete
`/data/fgg-playpods-gui/gui-playpods.log` for each run.

1. Fresh console state: send `audiobridge-gui.elf` to port 9021.
2. Confirm notifications, backend access by LAN IP, tile name/icon and Media placement.
3. Open the tile and verify the URL reaches the same GUI through `127.0.0.1:18195`.
4. Scan and connect a known A2DP/SBC device; verify sound and DualSense operation.
5. Record `/api/status` after at least 30 seconds in Stable mode.
6. Disconnect, select Low latency, reconnect, and record `/api/status` plus five-second stream log lines for at least five minutes.
7. Power-cycle or reboot. Confirm the tile remains. Open it before sending the payload and record the WebKit failure screen/time-to-error.
8. Send the payload again. Confirm the existing tile opens, pairing is retained, and only one backend instance runs.
9. Build a payload with a newer `VERSION`, send it, and verify icon/metadata refresh without pairing loss.
10. Send `audiobridge-uninstall.elf`. Confirm only `ABRG18195` disappears and `/data/fgg-playpods-gui/{paired.key,saved-device.txt}` remains.

Do not delete the temporary legacy websrv rollback package from release/CI until all ten checks pass on target firmware.
