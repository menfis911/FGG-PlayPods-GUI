import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]


class HttpApiIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "http-harness"
        subprocess.run(
            [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                '-DAUDIOBRIDGE_VERSION="test"',
                "-I", str(ROOT / "src"), "-I", str(ROOT / "tests/fixtures"),
                str(ROOT / "tests/fixtures/http_harness.c"),
                str(ROOT / "src/http_server.c"), "-o", str(cls.binary),
            ],
            check=True,
        )
        cls.server = subprocess.Popen([str(cls.binary)])
        for _ in range(40):
            try:
                urlopen("http://127.0.0.1:18197/api/status", timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.05)
        else:
            raise RuntimeError("HTTP test server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        cls.server.wait(timeout=3)
        cls.temp.cleanup()

    def get_json(self, path, data=None):
        request = Request(
            f"http://127.0.0.1:18197{path}",
            data=json.dumps(data).encode() if data is not None else None,
            headers={"Content-Type": "application/json"},
        )
        with urlopen(request, timeout=2) as response:
            return response.status, json.load(response)

    def test_status_distinguishes_selected_and_active_profiles(self):
        status, body = self.get_json("/api/status")
        self.assertEqual(status, 200)
        self.assertEqual(body["activeAudioProfile"], "stable")
        self.assertEqual(body["selectedAudioProfile"], "low_latency")
        self.assertEqual(body["revisions"], {"state": 3, "devices": 4, "saved": 5})
        self.assertEqual(body["latency"]["maxQueueMs"], 85)
        self.assertEqual(body["latency"]["completionReports"], 120)

    def test_profile_can_be_selected_while_streaming(self):
        status, body = self.get_json("/api/audio-profile", {"profile": "low_latency"})
        self.assertEqual(status, 200)
        self.assertTrue(body["accepted"])

    def test_existing_endpoints_remain_available(self):
        for path in ("/api/devices", "/api/saved", "/api/audio-profile"):
            status, _ = self.get_json(path)
            self.assertEqual(status, 200)


if __name__ == "__main__":
    unittest.main()
