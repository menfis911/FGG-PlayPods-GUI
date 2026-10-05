// websrv extension: expose a tile and open the native backend's Web GUI.
async function main() {
  return {
    mainText: "FGG-PlayPods-GUI",
    secondaryText: "Bluetooth audio · Web GUI",
    onclick: async () => {
      await ApiClient.launchApp(window.workingDir + "/eboot.elf");
      const url = "http://127.0.0.1:18195/";
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
      throw new Error("FGG-PlayPods-GUI backend did not start");
    }
  };
}
