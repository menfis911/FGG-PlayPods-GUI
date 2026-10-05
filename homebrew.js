// websrv extension: expose the AudioBridge tile and open its native Web GUI.
async function main() {
  return {
    mainText: "AudioBridge — GUI",
    secondaryText: "PS5 Bluetooth audio · A2DP",
    onclick: async () => {
      await ApiClient.launchApp(window.workingDir + "/eboot.elf");
      const host = window.location.hostname || "127.0.0.1";
      const url = `http://${host}:18195/`;
      for (let attempt = 0; attempt < 40; attempt += 1) {
        try {
          const response = await fetch(url + "api/status", { cache: "no-store" });
          if (response.ok) {
            window.location.href = url;
            return true;
          }
        } catch (_) {
          // The native worker and HTTP listener are still starting.
        }
        await new Promise((resolve) => setTimeout(resolve, 250));
      }
      throw new Error("AudioBridge backend did not start");
    }
  };
}
