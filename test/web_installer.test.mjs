import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { enterBootloader, loadManifest } from "../web/installer.mjs";

const manifest = JSON.parse(await readFile(new URL("../web/manifest.json", import.meta.url)));

test("bootloader touch opens the selected port at 1200 baud, then releases it", async () => {
  const calls = [];
  const port = {
    open: async (options) => calls.push(["open", options]),
    close: async () => calls.push(["close"]),
  };
  await enterBootloader({ requestPort: async () => port });
  assert.deepEqual(calls, [["open", { baudRate: 1200 }], ["close"]]);
});

test("a reboot disconnect during close is expected", async () => {
  await enterBootloader({ requestPort: async () => ({
    open: async () => {},
    close: async () => { throw new DOMException("Device disconnected", "NetworkError"); },
  }) });
});

test("cancelling the port chooser propagates without touching a port", async () => {
  const cancelled = new DOMException("Cancelled", "NotFoundError");
  await assert.rejects(enterBootloader({ requestPort: async () => { throw cancelled; } }), cancelled);
});

test("failed open is not reported as a successful bootloader request", async () => {
  const unavailable = new DOMException("Port busy", "NetworkError");
  let closed = false;
  await assert.rejects(enterBootloader({ requestPort: async () => ({
    open: async () => { throw unavailable; },
    close: async () => { closed = true; },
  }) }), unavailable);
  assert.equal(closed, false);
});

test("unexpected close failures propagate", async () => {
  const failure = new Error("Close failed");
  await assert.rejects(enterBootloader({ requestPort: async () => ({
    open: async () => {},
    close: async () => { throw failure; },
  }) }), failure);
});

test("installer checks the actual manifest and merged binary at offset zero", async () => {
  const calls = [];
  const loaded = await loadManifest(async (url, options) => {
    calls.push([url, options]);
    return { ok: true, json: async () => manifest };
  });
  assert.equal(loaded.new_install_improv_wait_time, 0);
  assert.deepEqual(calls, [
    ["./manifest.json", { cache: "no-store" }],
    ["gvret-s3-factory.bin", { method: "HEAD", cache: "no-store" }],
  ]);
});

test("missing manifest leaves the installer unavailable", async () => {
  await assert.rejects(loadManifest(async () => ({ ok: false })), /manifest is unavailable/);
});

test("a missing binary leaves the installer unavailable", async () => {
  await assert.rejects(loadManifest(async (url) => ({
    ok: url.endsWith("manifest.json"), json: async () => manifest,
  })), /binary is unavailable/);
});

test("an application-only manifest is rejected", async () => {
  const wrong = structuredClone(manifest);
  wrong.builds[0].parts[0].offset = 0x10000;
  await assert.rejects(loadManifest(async () => ({ ok: true, json: async () => wrong })), /factory image/);
});
