import { mountSnow } from "./snow.mjs";

const scene = document.getElementById("scene");
const snow = mountSnow({ root: scene });
const intensity = document.getElementById("intensity");
const wind = document.getElementById("wind");
const windOutput = document.getElementById("wind-output");
const snowballs = document.getElementById("snowballs");
const australiaButton = document.getElementById("australia");
const clear = document.getElementById("clear");
const pause = document.getElementById("pause");
const reduced = document.getElementById("reduced");
const motion = window.matchMedia("(prefers-reduced-motion: reduce)");
let paused = false;
let interactive = false;
let australia = false;

function updateControls() {
  reduced.hidden = !motion.matches;
  for (const button of [intensity, wind, snowballs, australiaButton, clear, pause]) button.disabled = motion.matches;
  snowballs.disabled = paused || motion.matches;
  snowballs.setAttribute("aria-pressed", String(interactive));
  pause.textContent = paused ? "Продолжить" : "Пауза";
}

intensity.addEventListener("change", () => snow.setIntensity(intensity.value));
wind.addEventListener("input", () => {
  const strength = Number(wind.value);
  windOutput.value = strength < 0 ? "Влево" : strength > 0 ? "Вправо" : "Штиль";
  snow.setWind(strength);
});
snowballs.addEventListener("click", () => snow.setInteractive(!interactive));
australiaButton.addEventListener("click", () => {
  australia = !australia;
  snow.setAustralia(australia);
  australiaButton.setAttribute("aria-pressed", String(australia));
});
clear.addEventListener("click", () => snow.clear());
pause.addEventListener("click", () => {
  paused = !paused;
  if (paused) snow.pause();
  else snow.resume();
  updateControls();
});
scene.addEventListener("snow:interaction", event => {
  interactive = event.detail;
  updateControls();
});
motion.addEventListener("change", updateControls);

for (const shelf of scene.querySelectorAll("[data-snow-surface]")) {
  let drag;
  shelf.addEventListener("pointerdown", event => {
    if (event.button !== 0 || drag) return;
    shelf.setPointerCapture(event.pointerId);
    drag = {
      id: event.pointerId, x: event.clientX, y: event.clientY,
      left: shelf.offsetLeft, top: shelf.offsetTop,
    };
    shelf.classList.add("dragging");
    snow.refreshSurfaces();
  });
  shelf.addEventListener("pointermove", event => {
    if (drag?.id !== event.pointerId) return;
    const left = Math.max(0, Math.min(scene.clientWidth - shelf.offsetWidth, drag.left + event.clientX - drag.x));
    const top = Math.max(0, Math.min(scene.clientHeight - shelf.offsetHeight, drag.top + event.clientY - drag.y));
    shelf.style.left = left + "px";
    shelf.style.right = "auto";
    shelf.style.top = top + "px";
    snow.refreshSurfaces();
  });
  shelf.addEventListener("lostpointercapture", () => {
    drag = undefined;
    shelf.classList.remove("dragging");
    snow.refreshSurfaces();
  });
}
updateControls();
