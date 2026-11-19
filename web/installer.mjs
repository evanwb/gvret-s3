// The firmware deliberately ignores DTR/RTS. Request its software bootloader
// before ESP Web Tools connects to the ROM's newly enumerated serial port.
export async function enterBootloader(serial) {
  const port = await serial.requestPort();
  await port.open({ baudRate: 1200 });
  try {
    await port.close();
  } catch (error) {
    // Reboot can detach the device while close() is still in progress.
    if (error.name !== "NetworkError") throw error;
  }
}

export async function loadManifest(fetcher = fetch) {
  const response = await fetcher("./manifest.json", { cache: "no-store" });
  if (!response.ok) throw new Error("The firmware manifest is unavailable.");
  const manifest = await response.json();
  const build = manifest.builds?.find((item) => item.chipFamily === "ESP32-S3");
  if (!manifest.version || build?.parts?.length !== 1 || build.parts[0].offset !== 0)
    throw new Error("The firmware manifest does not describe an ESP32-S3 factory image.");
  const firmware = await fetcher(build.parts[0].path, { method: "HEAD", cache: "no-store" });
  if (!firmware.ok) throw new Error("The firmware binary is unavailable.");
  return manifest;
}

async function initialize() {
  const bootloader = document.querySelector("#bootloader");
  const install = document.querySelector("#install-button");
  const status = document.querySelector("#status");
  const setStatus = (message, error = false) => {
    status.textContent = message;
    status.dataset.error = String(error);
  };
  const supported = window.isSecureContext && "serial" in navigator;
  if (supported) {
    bootloader.disabled = false;
    bootloader.addEventListener("click", async () => {
      bootloader.disabled = true;
      try {
        await enterBootloader(navigator.serial);
        setStatus("Bootloader requested. Select Install firmware and choose the newly appeared ROM port.");
      } catch (error) {
        if (error.name === "NotFoundError") setStatus("No port selected. Choose your GVRET application port to request the bootloader.");
        else setStatus("Could not complete the request. If the device restarted, select its new ROM port to install. Otherwise, close other serial applications and retry.", true);
      } finally {
        bootloader.disabled = false;
      }
    });
  }

  try {
    const manifest = await loadManifest();
    const build = document.querySelector("#build");
    build.textContent = `Build ${manifest.version.slice(0, 12)}`;
    build.href = `https://github.com/evanwb/gvret-s3/commit/${encodeURIComponent(manifest.version)}`;
    build.hidden = false;
    document.querySelector("#download").hidden = false;
    if (!supported) {
      setStatus(window.isSecureContext
        ? "Browser flashing needs Web Serial. Open this page in desktop Chrome or Edge, or download the binary below."
        : "Browser flashing needs HTTPS or localhost. You can still download the binary below.", true);
      return;
    }
    await import("https://unpkg.com/esp-web-tools@10.5.2/dist/web/install-button.js?module");
    install.disabled = false;
    document.querySelector("#install").hidden = false;
    setStatus("Ready to install. Your browser will ask you to select a serial port.");
  } catch (error) {
    setStatus(`${error.message} Reload the page or use the manual flashing instructions.`, true);
  }
}

if (typeof document !== "undefined") initialize();
