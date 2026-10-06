import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WebPackageTests(unittest.TestCase):
    def test_required_web_assets_exist(self):
        for relative in ("web/index.html", "web/styles.css", "web/app.js", "homebrew.js"):
            path = ROOT / relative
            self.assertTrue(path.is_file(), relative)
            self.assertGreater(path.stat().st_size, 100, relative)

    def test_audiobridge_brand_and_local_preview(self):
        page = (ROOT / "web/index.html").read_text(encoding="utf-8")
        tile = (ROOT / "homebrew.js").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("AudioBridge — GUI", page)
        self.assertIn("AudioBridge — GUI", tile)
        self.assertIn("FathiGhanem", readme)
        self.assertIn("FGGstore/FGG-PlayPods", readme)
        self.assertIn('data-language="ru"', page)
        self.assertIn('data-language="en"', page)
        script = (ROOT / "web/app.js").read_text(encoding="utf-8")
        self.assertIn("audiobridge-language", script)
        self.assertIn("Bluetooth-аудио для PS5", script)
        self.assertIn('href="styles.css"', page)
        self.assertIn('src="app.js"', page)
        self.assertNotIn('href="/styles.css"', page)
        self.assertNotIn('src="/app.js"', page)

    def test_frontend_uses_documented_api(self):
        script = (ROOT / "web/app.js").read_text(encoding="utf-8")
        for endpoint in (
            "/api/status", "/api/devices", "/api/scan",
            "/api/connect", "/api/disconnect", "/api/saved",
            "/api/audio-profile",
        ):
            self.assertIn(endpoint, script)

    def test_websrv_extension_contract(self):
        script = (ROOT / "homebrew.js").read_text(encoding="utf-8")
        self.assertRegex(script, r"async\s+function\s+main\s*\(")
        self.assertIn("ApiClient.launchApp", script)
        self.assertIn("/eboot.elf", script)
        self.assertIn('window.location.hostname || "127.0.0.1"', script)
        self.assertIn('`http://${host}:18195/`', script)

    def test_version_is_a_new_release(self):
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")
        self.assertNotEqual(version, "0.1.5")
        page = (ROOT / "web/index.html").read_text(encoding="utf-8")
        metadata = json.loads((ROOT / "audiobridge-gui.elf.json").read_text(encoding="utf-8"))
        self.assertIn(f'<span class="version">{version}</span>', page)
        self.assertEqual(metadata["version"], version)

    def test_native_tile_uses_unique_local_deeplink(self):
        metadata = json.loads((ROOT / "tile/sce_sys/param.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["titleId"], "ABRG18195")
        self.assertEqual(metadata["applicationCategoryType"], 65536)
        self.assertEqual(metadata["deeplinkUri"], "http://127.0.0.1:18195/")
        self.assertNotIn("webAppUri", metadata)

        installer = (ROOT / "src/tile.c").read_text(encoding="utf-8")
        self.assertIn("audiobridge-owner.txt", installer)
        self.assertIn("refusing to overwrite", installer)
        self.assertIn("sceAppInstUtilAppUnInstall", installer)
        self.assertNotIn("rm -rf", installer)

    def test_latency_profiles_keep_quality_and_expose_metrics(self):
        a2dp = (ROOT / "src/a2dp.c").read_text(encoding="utf-8")
        api = (ROOT / "src/http_server.c").read_text(encoding="utf-8")
        self.assertIn("STABLE_MAX_LAG_MS      250", a2dp)
        self.assertIn("LOW_LATENCY_MAX_LAG_MS 80", a2dp)
        self.assertIn("LOW_LATENCY_SBC_FRAMES 2", a2dp)
        self.assertIn("MAX_BITPOOL     53", a2dp)
        for field in ("packetDurationUs", "queueMs", "trimmedFrames", "captureOverruns"):
            self.assertIn(field, api)

    def test_shared_controller_is_conservative_and_has_fail_safe(self):
        hci = (ROOT / "src/hci.c").read_text(encoding="utf-8")
        bt = (ROOT / "src/bt.c").read_text(encoding="utf-8")
        a2dp = (ROOT / "src/a2dp.c").read_text(encoding="utf-8")
        self.assertIn("#define EVENT_READS 1", hci)
        self.assertIn("#define ACL_READS   1", hci)
        for forbidden in (
            "OP_SET_EVENT_MASK", "OP_WRITE_LOCAL_NAME", "OP_WRITE_SCAN_ENABLE",
            "OP_WRITE_CLASS_OF_DEV", "OP_WRITE_INQUIRY_MODE", "OP_WRITE_SSP_MODE",
        ):
            self.assertNotIn(forbidden, bt)
        self.assertIn("COMPLETION_STALL_MS", bt)
        self.assertIn("Bluetooth controller vendor fault event", bt)
        self.assertIn("A2DP packet transmission stalled", a2dp)
        self.assertIn("Capture overruns and stale audio grew rapidly", a2dp)
        self.assertNotIn("key_forget();", bt)

    def test_profile_selection_is_pending_while_streaming(self):
        backend = (ROOT / "src/backend.c").read_text(encoding="utf-8")
        api = (ROOT / "src/http_server.c").read_text(encoding="utf-8")
        frontend = (ROOT / "web/app.js").read_text(encoding="utf-8")
        self.assertIn("g_state.selected_profile = selected", backend)
        self.assertIn("a2dp_set_profile(g_state.selected_profile)", backend)
        self.assertIn("selectedAudioProfile", api)
        self.assertIn("activeAudioProfile", api)
        self.assertIn("Disconnect and connect again", frontend)
        self.assertIn("Отключите и подключите устройство повторно", frontend)
        self.assertIn("button.disabled = false", frontend)

    def test_frontend_polling_does_not_overlap_or_rebuild_unchanged_lists(self):
        script = (ROOT / "web/app.js").read_text(encoding="utf-8")
        self.assertIn("if (state.polling) return", script)
        self.assertIn("state.polling = true", script)
        self.assertIn("state.polling = false", script)
        self.assertNotIn("setInterval(", script)
        self.assertNotIn("Promise.all", script)
        self.assertIn("if (key === state.devicesKey) return", script)
        self.assertIn("if (key === state.savedKey) return", script)

    def test_http_api_diagnostics_and_timeouts(self):
        server = (ROOT / "src/http_server.c").read_text(encoding="utf-8")
        for field in (
            "packetSendRate", "completionReports", "assumedCompletions",
            "missingReports", "stallMs", "safetyStopReason", "revisions",
            "testMetadata", "record-manually",
        ):
            self.assertIn(field, server)
        self.assertIn("SO_RCVTIMEO", server)
        self.assertIn("SO_SNDTIMEO", server)
        self.assertIn("htonl(INADDR_ANY)", server)

    def test_user_facing_terms_cover_a2dp_audio_devices(self):
        page = (ROOT / "web/app.js").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        instructions = (ROOT / "INSTALL-RU.txt").read_text(encoding="utf-8")
        for text in (page, readme, instructions):
            self.assertIn("A2DP", text)
            self.assertIn("SBC", text)
        self.assertIn("Bluetooth-аудиоустрой", page)
        self.assertIn("Bluetooth-аудиоустрой", readme)

    def test_legacy_metadata_is_valid_json(self):
        # Payload Manager metadata is retained as an optional compatibility asset.
        metadata = json.loads((ROOT / "audiobridge-gui.elf.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["name"], "AudioBridge-GUI")


if __name__ == "__main__":
    unittest.main()
