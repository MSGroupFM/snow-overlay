const assert = require("node:assert/strict");
const path = require("node:path");
const fs = require("node:fs/promises");
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || "playwright");

const web = path.join(__dirname, "../web");
const build = path.join(__dirname, "../build");
const types = { "/": "text/html", "/demo.css": "text/css", "/demo.mjs": "text/javascript", "/snow.mjs": "text/javascript" };

async function wire(page) {
  await page.route("http://snow.test/**", async route => {
    const url = new URL(route.request().url());
    if (!Object.hasOwn(types, url.pathname)) return route.fulfill({ status: 404 });
    await route.fulfill({
      contentType: types[url.pathname],
      body: await fs.readFile(path.join(web, url.pathname === "/" ? "index.html" : url.pathname.slice(1))),
    });
  });
  await page.addInitScript(() => {
    let seed = 42;
    Math.random = () => {
      seed = (1664525 * seed + 1013904223) >>> 0;
      return seed / 4294967296;
    };
  });
}

async function snowCount(page, inverted = false, corner = null) {
  return page.evaluate(({ inverted, corner }) => {
    const shelf = document.querySelector(".shelf-left").getBoundingClientRect();
    const canvas = document.querySelector("canvas");
    const origin = canvas.getBoundingClientRect();
    const x = corner === "right" ? shelf.right - 35 : shelf.left;
    const pixels = canvas.getContext("2d").getImageData(
      Math.round(x - origin.left), Math.round((inverted ? shelf.bottom : shelf.top - 12) - origin.top),
      corner ? 35 : Math.floor(shelf.width), 12
    ).data;
    let snow = 0;
    for (let i = 3; i < pixels.length; i += 4) if (pixels[i] > 225) snow++;
    return snow;
  }, { inverted, corner });
}

(async () => {
  await fs.mkdir(build, { recursive: true });
  const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_BIN });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 850 } });
    const errors = [];
    page.on("pageerror", error => errors.push(error));
    await wire(page);
    await page.clock.install();
    await page.goto("http://snow.test/");
    await page.locator(".shelf-left").evaluate(element => {
      element.style.borderTopLeftRadius = "40px 5px";
      element.style.borderBottomRightRadius = "45px 6px";
    });
    await page.locator("#intensity").selectOption("blizzard");
    await page.clock.runFor(28000);
    const snow = await snowCount(page);
    assert(snow > 300, "a marked surface should accumulate snow");
    assert(await snowCount(page, false, "left") < 12, "snow should clear the top-left corner");

    const right = page.locator(".shelf-right");
    const rightBox = await right.boundingBox();
    await right.evaluate(element => {
      window.rightGeometrySamples = 0;
      const read = element.getBoundingClientRect.bind(element);
      element.getBoundingClientRect = () => {
        window.rightGeometrySamples++;
        return read();
      };
    });
    await page.mouse.move(rightBox.x + 40, rightBox.y + 6);
    await page.mouse.down();
    await page.clock.runFor(20);
    const beforeDrag = await page.evaluate(() => window.rightGeometrySamples);
    await page.mouse.move(rightBox.x + 55, rightBox.y - 32);
    await page.clock.runFor(20);
    assert(await page.evaluate(() => window.rightGeometrySamples) > beforeDrag,
      "geometry must be sampled on the next frame while a shelf is dragged");
    await page.mouse.up();

    const shelf = await page.locator(".shelf-left").boundingBox();
    await page.evaluate(() => {
      window.underlayClicks = 0;
      document.querySelector(".shelf-left").addEventListener("click", () => window.underlayClicks++);
    });
    await page.mouse.click(shelf.x + 5, shelf.y + 6);
    assert.equal(await page.evaluate(() => window.underlayClicks), 1);
    await page.mouse.move(shelf.x + shelf.width - 2, shelf.y - 4, { steps: 12 });
    await page.clock.runFor(35);
    assert(await snowCount(page) < snow / 3, "sweeping should clear the marked surface");

    await page.locator("#snowballs").click();
    assert.equal(await page.locator("canvas").evaluate(el => getComputedStyle(el).pointerEvents), "auto");
    await page.mouse.click(shelf.x + 70, shelf.y - 6);
    await page.clock.runFor(80);
    await page.mouse.click(shelf.x + 73, shelf.y - 18);
    await page.clock.runFor(80);
    const snowball = await page.evaluate(({ x, y }) => {
      const canvas = document.querySelector("canvas");
      const bounds = canvas.getBoundingClientRect();
      const data = canvas.getContext("2d").getImageData(x - bounds.left, y - bounds.top, 1, 1).data;
      return data[3];
    }, { x: shelf.x + 70, y: shelf.y - 18 });
    assert.equal(snowball, 255, "the second snowball should sit on top of the first");

    await page.keyboard.press("Escape");
    assert.equal(await page.locator("#snowballs").getAttribute("aria-pressed"), "false");
    await page.locator("#pause").click();
    assert.equal(await page.locator("canvas").evaluate(el => getComputedStyle(el).visibility), "hidden");
    await page.locator("#pause").click();
    assert.equal(await page.locator("canvas").evaluate(el => getComputedStyle(el).visibility), "visible");
    await page.clock.runFor(2000);
    await page.screenshot({ path: path.join(build, "web-preview.png") });

    await page.locator("#australia").click();
    assert.equal(await page.locator("#australia").getAttribute("aria-pressed"), "true");
    await page.clock.runFor(700);
    const rising = await page.locator("canvas").evaluate(canvas => {
      const ctx = canvas.getContext("2d");
      function visiblePixels(y) {
        const pixels = ctx.getImageData(0, y, canvas.width, 105).data;
        let visible = 0;
        for (let i = 3; i < pixels.length; i += 4) if (pixels[i] > 100) visible++;
        return visible;
      }
      return { top: visiblePixels(0), bottom: visiblePixels(canvas.height - 105) };
    });
    assert.equal(rising.top, 0, "Australia snow must start at the bottom");
    assert(rising.bottom > 100);
    await page.clock.runFor(27000);
    assert(await snowCount(page, true) > 280, "Australia snow must settle beneath the shelf");
    assert(await snowCount(page, true, "right") < 12, "Australia snow should clear the bottom-right corner");
    assert(await snowCount(page) < 80, "Australia snow should leave the top edge clear");
    await page.screenshot({ path: path.join(build, "web-australia-preview.png") });
    await page.locator("#australia").click();
    assert.equal(await page.locator("#australia").getAttribute("aria-pressed"), "false");

    await page.emulateMedia({ reducedMotion: "reduce" });
    await page.locator("#reduced").waitFor({ state: "visible" });
    assert.equal(await page.locator("canvas").evaluate(el => getComputedStyle(el).visibility), "hidden");
    assert.equal(await page.locator("#reduced").isVisible(), true);
    assert.deepEqual(errors, []);

    const mobile = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
    await wire(mobile);
    await mobile.goto("http://snow.test/");
    assert.equal(await mobile.evaluate(() => document.documentElement.scrollWidth), 390);
    await mobile.locator("#snowballs").tap();
    assert.equal(await mobile.locator("#snowballs").getAttribute("aria-pressed"), "true");
    console.log("Browser checks passed");
  } finally {
    await browser.close();
  }
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
