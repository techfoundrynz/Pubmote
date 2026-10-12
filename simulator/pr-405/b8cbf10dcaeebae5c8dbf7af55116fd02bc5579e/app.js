import init, { start } from "./pkg/pubremote_web.js";

const status = document.querySelector("#status");
const panel = document.querySelector("#panel");
const query = new URLSearchParams(location.search);
const size = query.get("panel") || "466,466,0";
panel.value = size;
panel.addEventListener("change", () => {
  const url = new URL(location.href);
  url.searchParams.set("panel", panel.value);
  location.assign(url);
});
try {
  await init();
  const [width, height, square] = size.split(",").map(Number);
  // Keep panel dimensions in CSS pixels.
  const canvases = [[document.querySelector("#remote"), width, height],
    [document.querySelector("#controls"), 420, 620]];
  for (const [canvas, w, h] of canvases) {
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;
  }
  // Some browsers report devicePixelContentBoxSize without applying the
  // current devicePixelRatio (including Chromium's emulated high-DPI mode).
  // Winit trusts that value for its viewport while scaling text by the DPR.
  // Normalize only the two Slint canvases, through observers created at startup.
  const NativeResizeObserver = window.ResizeObserver;
  window.ResizeObserver = class extends NativeResizeObserver {
    constructor(callback) {
      super((entries, observer) => {
        for (const entry of entries) {
          if (!canvases.some(([canvas]) => canvas === entry.target) || !entry.devicePixelContentBoxSize) continue;
          const inlineSize = Math.round(entry.contentRect.width * devicePixelRatio);
          const blockSize = Math.round(entry.contentRect.height * devicePixelRatio);
          const reported = entry.devicePixelContentBoxSize[0];
          if (reported?.inlineSize !== inlineSize || reported?.blockSize !== blockSize) {
            Object.defineProperty(entry, "devicePixelContentBoxSize", { value: [{ inlineSize, blockSize }] });
          }
        }
        callback(entries, observer);
      });
    }
  };
  try {
    window.simulator = await start(new URL("./", location.href).href, width, height, Boolean(square));
  } finally {
    window.ResizeObserver = NativeResizeObserver;
  }
  status.textContent = "Ready";
  document.body.dataset.ready = "true";
  fetch("./report/summary.json").then(r => {
    if (r.ok) document.querySelector("#report-link").hidden = false;
  }).catch(() => {});
  fetch("./build.json").then(r => r.ok ? r.json() : null).then(info => {
    if (info) document.querySelector("#revision").textContent = `${info.label} · ${info.commit.slice(0, 8)}`;
  }).catch(() => {});
} catch (error) {
  status.textContent = `Simulator failed to load: ${error}`;
  document.body.dataset.error = "true";
  console.error(error);
}
