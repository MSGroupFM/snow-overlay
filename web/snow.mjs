const bins = 128;
const rates = { light: 26, steady: 54, blizzard: 125 };
const maxDepth = 24;
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

/**
 * Start a local snow effect. Set surfaces to a selector inside root.
 * Omit root to use the viewport. For a section, use a positioned wrapper.
 */
export function mountSnow({
  root = document.body,
  surfaces = "[data-snow-surface]",
  intensity = "steady",
  wind = 0,
  zIndex = 100,
  respectReducedMotion = true,
  australia: initiallyAustralia = false,
} = {}) {
  if (!(root instanceof HTMLElement) || !root.isConnected) {
    throw new TypeError("mountSnow requires a connected HTML element");
  }
  const viewport = root === document.body;
  if (!viewport && getComputedStyle(root).position === "static") {
    throw new TypeError("The snow container needs a CSS position such as relative");
  }
  root.querySelectorAll(surfaces);
  const canvas = document.createElement("canvas");
  canvas.setAttribute("aria-hidden", "true");
  Object.assign(canvas.style, {
    position: viewport ? "fixed" : "absolute",
    left: "0",
    top: "0",
    display: "block",
    pointerEvents: "none",
    zIndex: String(zIndex),
    maxWidth: "none",
    maxHeight: "none",
  });
  const context = canvas.getContext("2d", { alpha: true });
  if (!context) throw new Error("Canvas 2D is unavailable");
  const texture = document.createElement("canvas");
  texture.width = texture.height = 20;
  const textureContext = texture.getContext("2d");
  textureContext.beginPath();
  textureContext.arc(10, 10, 9, 0, 2 * Math.PI);
  textureContext.fillStyle = "#f0faff";
  textureContext.fill();

  const floorId = Symbol("floor");
  const listeners = new AbortController();
  const motion = window.matchMedia("(prefers-reduced-motion: reduce)");
  const flakes = [];
  const balls = [];
  let drifts = [];
  let driftById = new Map();
  let width = 0;
  let height = 0;
  let left = 0;
  let top = 0;
  let ratio = 0;
  let frame = 0;
  let lastFrame = 0;
  let lastGeometry = -Infinity;
  let budget = 0;
  let driftWind = 0;
  let chunksEmitted = 0;
  let geometryTime = 0;
  let scrollUpdate = false;
  let australia = Boolean(initiallyAustralia);
  let paused = false;
  let interactive = false;
  let onScreen = true;
  let destroyed = false;
  let previousPointer;
  let held;
  let pointerId;
  let rate = rates.steady;
  let windTarget = 0;

  function setIntensity(value) {
    if (!Object.hasOwn(rates, value)) throw new RangeError("Use light, steady, or blizzard");
    rate = rates[value];
  }

  function setWind(value) {
    if (!Number.isFinite(value)) throw new TypeError("Wind must be a number");
    windTarget = clamp(value, -1, 1);
  }

  function binAt(drift, x) {
    return clamp(Math.floor((x - drift.x) * bins / drift.width), 0, bins - 1);
  }

  function screenY(y) {
    return australia ? height - y : y;
  }

  function spanAt(drift, x) {
    return drift.spans.find(span => x >= span.left && x < span.right);
  }

  function covered(drift, x, y) {
    return drifts.some(other =>
      other.order < drift.order && x >= other.x && x < other.x + other.width &&
      y >= other.y && y < other.y + other.height
    );
  }

  function makeSpans(drift) {
    if (drift.y <= 1 || drift.y >= height) return [];
    let left = Math.max(0, drift.x + (drift.leftInset ?? 0));
    const right = Math.min(width, drift.x + drift.width - (drift.rightInset ?? 0));
    const spans = [];
    while (left < right && spans.length < 32) {
      let blockedUntil = left;
      let end = right;
      for (let i = 0; i < drift.order; i++) {
        const front = drifts[i];
        const frontRight = front.x + front.width;
        if (drift.y < front.y || drift.y >= front.y + front.height || frontRight <= left) continue;
        if (front.x <= left) blockedUntil = Math.max(blockedUntil, frontRight);
        else end = Math.min(end, front.x);
      }
      if (blockedUntil > left) {
        left = blockedUntil;
      } else {
        spans.push({ left, right: end });
        left = end;
      }
    }
    return spans;
  }

  function depthAt(drift, span, x) {
    const offset = clamp((x - drift.x) * bins / drift.width - .5, 0, bins - 1);
    const index = Math.floor(offset);
    const depth = drift.snow[index] + ((drift.snow[index + 1] ?? drift.snow[index]) - drift.snow[index]) * (offset - index);
    const taper = clamp(Math.min(x - span.left, span.right - x) / 8, 0, 1);
    return depth * taper * taper * (3 - 2 * taper);
  }

  function emitChunk(x, y, removed, vx, vy, skip) {
    if (removed < .12 || flakes.length >= 768 || chunksEmitted >= 48) return;
    chunksEmitted++;
    flakes.push({
      x, y, vx: vx + (Math.random() - .5) * 55, vy: vy + Math.random() * 23,
      r: clamp(1.4 + Math.sqrt(removed), 1.5, 5.1),
      deposit: clamp(removed * .55, .1, 1.7), skip,
    });
  }

  function shake(drift, vx, vy, speed) {
    const fraction = clamp((speed - 45) / 4900, 0, .085);
    if (!fraction) return;
    let nextChunk = -Infinity;
    for (let i = 0; i < bins; i++) {
      const x = drift.x + (i + .5) * drift.width / bins;
      const depth = drift.snow[i];
      if (depth < .35 || !spanAt(drift, x)) continue;
      const removed = depth * fraction;
      drift.snow[i] -= removed;
      if (x >= nextChunk && removed >= .12) {
        emitChunk(x, drift.y - depth - 1, removed * 2, clamp(vx * .56, -290, 290),
          clamp(vy * .4 - speed * .33 - 26, -250, -30));
        nextChunk = x + 30;
      }
    }
  }

  function scrape(drift, old, moving, before, dx, dy, elapsed, steps) {
    const left = Math.min(moving.x, before.x);
    const right = Math.max(moving.x, before.x) + moving.width;
    if (right <= drift.x || left >= drift.x + drift.width || Math.min(moving.y, before.y) > drift.y ||
        Math.max(moving.y, before.y) + moving.height < drift.y - maxDepth) return;
    let nextChunk = -Infinity;
    const last = binAt(drift, right);
    for (let i = binAt(drift, left); i <= last; i++) {
      const depth = drift.snow[i];
      const x = drift.x + (i + .5) * drift.width / bins;
      const oldX = old.x + (i + .5) * old.width / bins;
      if (depth < .1 || !spanAt(old, oldX)) continue;
      for (let step = 1; step <= steps; step++) {
        const probeX = before.x + dx * step / steps;
        const probeY = before.y + dy * step / steps;
        if (x < probeX || x >= probeX + moving.width || probeY > drift.y ||
            probeY + moving.height < drift.y - depth) continue;
        drift.snow[i] = 0;
        if (x >= nextChunk) {
          emitChunk(x, drift.y - depth, depth, clamp(dx * .20 / elapsed, -125, 125),
            clamp(dy * .20 / elapsed, 24, 140), drift.id);
          nextChunk = x + 18;
        }
        break;
      }
    }
  }

  function moveSurfaces(previous, elapsed) {
    for (const drift of drifts) {
      const before = previous.get(drift.id);
      if (drift.id === floorId || !before || before.width !== drift.width || before.height !== drift.height) continue;
      const dx = drift.x - before.x;
      const dy = drift.y - before.y;
      const distance = Math.hypot(dx, dy);
      if (distance < 1.5) continue;
      shake(drift, dx / elapsed, dy / elapsed, distance / elapsed);
      const steps = Math.ceil(clamp(distance / 10, 1, 18));
      for (let i = drift.order + 1; i < drifts.length; i++) {
        const other = drifts[i];
        const old = previous.get(other.id);
        if (old) scrape(other, old, drift, before, dx, dy, elapsed, steps);
      }
    }
  }

  function edgeInsets(style, rect) {
    const length = (value, size) => {
      if (value.endsWith("%")) return Math.max(0, parseFloat(value) * size / 100) || 0;
      return value.endsWith("px") ? Math.max(0, parseFloat(value)) || 0 : 0;
    };
    const corners = [style.borderTopLeftRadius, style.borderTopRightRadius,
      style.borderBottomRightRadius, style.borderBottomLeftRadius].map(value => {
      const [x, y = x] = value.split(/\s+/);
      return { x: length(x, rect.width), y: length(y, rect.height) };
    });
    const [tl, tr, br, bl] = corners;
    const scale = Math.min(1, rect.width / (tl.x + tr.x), rect.width / (bl.x + br.x),
      rect.height / (tl.y + bl.y), rect.height / (tr.y + br.y));
    return { leftInset: (australia ? bl.x : tl.x) * scale, rightInset: (australia ? br.x : tr.x) * scale };
  }

  function readGeometry(now) {
    const elapsed = clamp((now - geometryTime) / 1000, .03, .18);
    geometryTime = now;
    const host = viewport ? null : root.getBoundingClientRect();
    left = viewport ? 0 : host.left + root.clientLeft;
    top = viewport ? 0 : host.top + root.clientTop;
    const nextWidth = viewport ? document.documentElement.clientWidth : root.clientWidth;
    const nextHeight = viewport ? document.documentElement.clientHeight : root.clientHeight;
    const nextRatio = Math.min(window.devicePixelRatio || 1, 2,
      4096 / Math.max(1, nextWidth, nextHeight));
    const resized = nextWidth !== width || nextHeight !== height || nextRatio !== ratio;
    if (resized) {
      width = nextWidth;
      height = nextHeight;
      ratio = nextRatio;
      canvas.style.width = width + "px";
      canvas.style.height = height + "px";
      canvas.width = Math.max(1, Math.round(width * ratio));
      canvas.height = Math.max(1, Math.round(height * ratio));
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
    }

    const targets = [];
    for (const element of root.querySelectorAll(surfaces)) {
      if (element === canvas) continue;
      const rect = element.getBoundingClientRect();
      const x = rect.left - left;
      const y = australia ? screenY(rect.bottom - top) : rect.top - top;
      if (rect.width < 12 || rect.height < 1 || x >= width || x + rect.width <= 0 ||
          y > height || y + rect.height <= 0) continue;
      const style = getComputedStyle(element);
      if (style.visibility !== "visible" || Number(style.opacity) <= 0.05) continue;
      targets.push({
        id: element, x, y, width: rect.width, height: rect.height,
        z: Number.parseInt(style.zIndex, 10) || 0,
        ...edgeInsets(style, rect),
      });
      if (targets.length === 64) break;
    }
    targets.reverse().sort((a, b) => b.z - a.z);
    targets.push({ id: floorId, x: 0, y: height - 1, width: Math.max(1, width), height: 1 });

    const previous = driftById;
    const unchanged = targets.length === drifts.length && targets.every((target, i) => {
      const old = drifts[i];
      return old.id === target.id && old.x === target.x && old.y === target.y &&
        old.width === target.width && old.height === target.height &&
        old.leftInset === target.leftInset && old.rightInset === target.rightInset;
    });
    const allowMotion = !scrollUpdate && !resized;
    scrollUpdate = false;
    if (unchanged) return;

    drifts = targets.map((target, order) => ({
      ...target, order, snow: previous.get(target.id)?.snow || new Float32Array(bins),
    }));
    for (const drift of drifts) drift.spans = makeSpans(drift);
    driftById = new Map(drifts.map(drift => [drift.id, drift]));
    if (allowMotion) moveSurfaces(previous, elapsed);
    for (let i = balls.length - 1; i >= 0; i--) {
      if (!driftById.has(balls[i].id)) balls.splice(i, 1);
    }
  }

  function spawn() {
    if (flakes.length >= 672) return;
    const depth = Math.random();
    flakes.push({
      x: Math.random() * width,
      y: -5,
      vx: (Math.random() - 0.5) * 32,
      vy: 42 + depth * 105,
      r: 0.8 + depth * 2.6,
    });
  }

  function settle(flake, oldY) {
    let contact = Infinity;
    let target;
    for (const drift of drifts) {
      if (!drift.spans.length || drift.y < oldY || drift.y > flake.y + maxDepth ||
          flake.x < drift.x || flake.x >= drift.x + drift.width || flake.skip === drift.id) continue;
      const span = spanAt(drift, flake.x);
      if (!span) continue;
      const edge = drift.y - depthAt(drift, span, flake.x);
      if (oldY <= edge && flake.y >= edge && edge < contact) {
        contact = edge;
        target = drift;
      }
    }
    if (!target) return false;
    const bin = binAt(target, flake.x);
    const deposit = flake.deposit || (.2 + flake.r * .11);
    for (let i = Math.max(0, bin - 1); i <= Math.min(bins - 1, bin + 1); i++) {
      if (!spanAt(target, target.x + (i + .5) * target.width / bins)) continue;
      const limit = maxDepth - 4 + 2.2 * Math.sin(i * .12) - 1.4 * Math.sin(i * .51);
      const depth = target.snow[i];
      target.snow[i] = Math.min(limit, depth + deposit * (i === bin ? 1 : .42) * (1 - depth / (limit * 1.45)));
    }
    return true;
  }

  function step(dt) {
    chunksEmitted = 0;
    budget += rate * dt * clamp(width / 1200, 0.45, 1.8);
    while (budget >= 1) {
      spawn();
      budget--;
    }
    driftWind += (windTarget * 155 - driftWind) * Math.min(dt * 4, 1);
    for (let i = flakes.length - 1; i >= 0; i--) {
      const flake = flakes[i];
      const oldY = flake.y;
      if (flake.deposit) flake.vy = Math.min(460, flake.vy + 590 * dt);
      flake.x += (flake.vx + driftWind * (flake.deposit ? .18 : 1)) * dt;
      if (!flake.deposit) flake.x = (flake.x + width) % width;
      flake.y += flake.vy * dt;
      if ((flake.vy > 0 && settle(flake, oldY)) || flake.y > height + 8 ||
          (flake.deposit && (flake.x < -8 || flake.x > width + 8))) {
        flakes[i] = flakes[flakes.length - 1];
        flakes.pop();
      }
    }
  }

  function circle(x, y, radius, fill) {
    context.beginPath();
    context.arc(x, y, radius, 0, Math.PI * 2);
    context.fillStyle = fill;
    context.fill();
  }

  function drawBall(x, y, radius) {
    circle(x, y, radius, "#bdd8f0");
    circle(x - radius * 0.13, y - radius * 0.19, radius * 0.78, "#f7fcff");
  }

  function draw() {
    context.clearRect(0, 0, width, height);
    for (const flake of flakes) {
      context.globalAlpha = Math.min(.96, .39 + flake.r * .13);
      context.drawImage(texture, flake.x - flake.r, screenY(flake.y) - flake.r, flake.r * 2, flake.r * 2);
    }
    context.globalAlpha = 1;
    context.beginPath();
    for (const drift of drifts) {
      if (!drift.snow.some(depth => depth > .08)) continue;
      const spacing = clamp(drift.width / bins * .55, 2, 7);
      for (const span of drift.spans) {
        context.moveTo(span.left, screenY(drift.y));
        for (let x = span.left + spacing; x < span.right; x += spacing) {
          context.lineTo(x, screenY(drift.y - depthAt(drift, span, x)));
        }
        context.lineTo(span.right, screenY(drift.y));
        context.closePath();
      }
    }
    context.fillStyle = "rgba(237,247,255,.94)";
    context.fill();
    for (const ball of balls) {
      const drift = driftById.get(ball.id);
      const x = drift.x + ball.offset * drift.width;
      const span = spanAt(drift, x);
      if (!span) continue;
      const y = drift.y - depthAt(drift, span, x) - ball.rise - ball.radius;
      if (!covered(drift, x, y)) drawBall(x, screenY(y), ball.radius);
    }
    if (held) drawBall(held.x, screenY(held.y), held.radius);
  }

  function removeSnow(x, y, radius, fraction, push = 0) {
    let collected = 0;
    for (const drift of drifts) {
      if (!drift.spans.length || drift.y < y - radius || drift.y > y + radius + maxDepth + 5 ||
          x + radius < drift.x || x - radius >= drift.x + drift.width) continue;
      const last = binAt(drift, x + radius);
      let nextChunk = -Infinity;
      for (let bin = binAt(drift, x - radius); bin <= last; bin++) {
        const depth = drift.snow[bin];
        const center = drift.x + (bin + 0.5) * drift.width / bins;
        if (!depth || y < drift.y - depth - radius || !spanAt(drift, center)) continue;
        const removed = depth * fraction;
        drift.snow[bin] -= removed;
        if (fraction === 1 && center >= nextChunk) {
          emitChunk(center, drift.y - depth, removed, push, 30, drift.id);
          nextChunk = center + 12;
        }
        collected += removed * drift.width / bins;
      }
    }
    return collected;
  }

  function eventPoint(event) {
    return { x: event.clientX - left, y: screenY(event.clientY - top) };
  }

  function sweep(event) {
    if (!running() || interactive || event.pointerType === "touch") return;
    const point = eventPoint(event);
    if (point.x < 0 || point.x >= width || point.y < 0 || point.y >= height) {
      previousPointer = undefined;
      return;
    }
    if (previousPointer) {
      const dx = point.x - previousPointer.x;
      const dy = point.y - previousPointer.y;
      const distance = Math.hypot(dx, dy);
      if (distance >= 1) {
        const count = Math.ceil(clamp(distance / 12, 1, 24));
        for (let i = 1; i <= count; i++) {
          removeSnow(previousPointer.x + dx * i / count, previousPointer.y + dy * i / count, 13, 1,
            clamp(dx * 2.8, -180, 180));
        }
      }
    }
    previousPointer = point;
  }

  function moveBall(event) {
    if (!held || event.pointerId !== pointerId) return;
    Object.assign(held, eventPoint(event));
    const removed = removeSnow(held.x, held.y + held.radius * 0.6, held.radius + 9, 0.65);
    held.radius = Math.min(34, Math.sqrt(held.radius ** 2 + removed * 0.18));
  }

  function beginBall(event) {
    if (!interactive || !running() || held || event.button !== 0) return;
    canvas.setPointerCapture(event.pointerId);
    pointerId = event.pointerId;
    held = { ...eventPoint(event), radius: 6 };
    moveBall(event);
  }

  function cancelBall() {
    const id = pointerId;
    held = undefined;
    pointerId = undefined;
    if (id !== undefined && canvas.hasPointerCapture(id)) canvas.releasePointerCapture(id);
  }

  function dropBall(event) {
    if (!held || event.pointerId !== pointerId) return;
    moveBall(event);
    const { x, y, radius } = held;
    cancelBall();
    if (balls.length >= 48) return;
    let target;
    let best = Math.max(75, height * 0.26);
    for (const drift of drifts) {
      const span = spanAt(drift, x);
      if (!span) continue;
      let rise = 0;
      let offset = (x - drift.x) / drift.width;
      for (const ball of balls) {
        const ballX = drift.x + ball.offset * drift.width;
        if (ball.id === drift.id && Math.abs(x - ballX) < ball.radius && ball.rise + 2 * ball.radius > rise) {
          rise = ball.rise + 2 * ball.radius;
          offset = ball.offset;
        }
      }
      const edge = drift.y - depthAt(drift, span, x);
      const distance = Math.abs(y + radius - edge + rise);
      if (distance < best) {
        best = distance;
        target = { id: drift.id, offset, rise, radius };
      }
    }
    if (target) balls.push(target);
  }

  function running() {
    return !destroyed && !paused && !document.hidden && onScreen &&
      !(respectReducedMotion && motion.matches);
  }

  function setInteractive(active) {
    interactive = Boolean(active) && running();
    previousPointer = undefined;
    if (!interactive) cancelBall();
    canvas.style.pointerEvents = interactive ? "auto" : "none";
    canvas.style.touchAction = interactive ? "none" : "";
    canvas.style.cursor = interactive ? "crosshair" : "";
    canvas.dispatchEvent(new CustomEvent("snow:interaction", { bubbles: true, detail: interactive }));
  }

  function animate(now) {
    frame = 0;
    if (!running()) return;
    const frameInterval = lastGeometry === -Infinity ? 15 : 32;
    if (lastFrame && now - lastFrame < frameInterval) {
      frame = requestAnimationFrame(animate);
      return;
    }
    if (now - lastGeometry >= 75) {
      readGeometry(now);
      lastGeometry = now;
    }
    if (width > 0 && height > 0) {
      step(lastFrame ? Math.min((now - lastFrame) / 1000, 0.05) : 1 / 60);
      draw();
    }
    lastFrame = now;
    frame = requestAnimationFrame(animate);
  }

  function refresh() {
    if (running()) {
      canvas.style.visibility = "visible";
      if (!frame) {
        lastGeometry = -Infinity;
        frame = requestAnimationFrame(animate);
      }
    } else {
      cancelAnimationFrame(frame);
      frame = 0;
      lastFrame = 0;
      canvas.style.visibility = "hidden";
      setInteractive(false);
    }
  }

  function clear() {
    flakes.length = 0;
    balls.length = 0;
    budget = 0;
    chunksEmitted = 0;
    cancelBall();
    for (const drift of drifts) drift.snow.fill(0);
    context.clearRect(0, 0, width, height);
  }

  function invalidateGeometry() {
    lastGeometry = -Infinity;
  }

  setIntensity(intensity);
  setWind(wind);
  root.append(canvas);
  const passive = { signal: listeners.signal, passive: true };
  const active = { signal: listeners.signal };
  window.addEventListener("pointermove", sweep, passive);
  window.addEventListener("pointerout", event => {
    if (!event.relatedTarget) previousPointer = undefined;
  }, passive);
  window.addEventListener("scroll", () => {
    scrollUpdate = true;
    invalidateGeometry();
  }, { ...passive, capture: true });
  window.addEventListener("resize", invalidateGeometry, passive);
  window.addEventListener("keydown", event => {
    if (interactive && event.key === "Escape") setInteractive(false);
  }, active);
  document.addEventListener("visibilitychange", refresh, passive);
  motion.addEventListener("change", refresh, passive);
  canvas.addEventListener("pointerdown", beginBall, active);
  canvas.addEventListener("pointermove", moveBall, passive);
  canvas.addEventListener("pointerup", dropBall, active);
  canvas.addEventListener("pointercancel", cancelBall, active);
  canvas.addEventListener("lostpointercapture", cancelBall, active);

  const observer = viewport ? null : new IntersectionObserver(entries => {
    onScreen = entries[0].isIntersecting;
    refresh();
  });
  observer?.observe(root);
  const resize = new ResizeObserver(invalidateGeometry);
  resize.observe(root);
  refresh();

  return Object.freeze({
    setIntensity,
    setWind,
    setInteractive,
    refreshSurfaces: invalidateGeometry,
    setAustralia(enabled) {
      enabled = Boolean(enabled);
      if (australia === enabled) return;
      australia = enabled;
      clear();
      drifts = [];
      driftById.clear();
      previousPointer = undefined;
      invalidateGeometry();
    },
    clear,
    pause() {
      paused = true;
      refresh();
    },
    resume() {
      paused = false;
      refresh();
    },
    destroy() {
      if (destroyed) return;
      destroyed = true;
      listeners.abort();
      observer?.disconnect();
      resize.disconnect();
      refresh();
      canvas.remove();
    },
  });
}
