#include "snow.h"
#include <math.h>
#include <string.h>

static float clamp(float value, float lower, float upper) {
    return fminf(upper, fmaxf(lower, value));
}

static float random_unit(SnowScene *scene) {
    uint32_t value = scene->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    scene->random = value;
    return (float)(value >> 8) / 16777216.0f;
}

static bool inside(SnowRect rect, float x, float y) {
    return x >= rect.x && x < rect.x + rect.width &&
           y >= rect.y && y < rect.y + rect.height;
}

static size_t bin_at(SnowRect rect, float x) {
    return (size_t)clamp((x - rect.x) * SNOW_BINS / rect.width, 0, SNOW_BINS - 1);
}

static const SnowSpan *span_at(const SnowDrift *drift, float x) {
    for (size_t i = 0; i < drift->span_count; i++) {
        if (x >= drift->spans[i].left && x < drift->spans[i].right) return &drift->spans[i];
    }
    return NULL;
}

float snow_depth(const SnowDrift *drift, SnowSpan span, float x) {
    SnowRect rect = drift->surface.rect;
    float offset = clamp((x - rect.x) * SNOW_BINS / rect.width - .5f, 0, SNOW_BINS - 1);
    size_t first = (size_t)offset;
    size_t next = first + 1 < SNOW_BINS ? first + 1 : first;
    float depth = drift->height[first] + (drift->height[next] - drift->height[first]) * (offset - first);
    float taper = clamp(fminf(x - span.left, span.right - x) / 8, 0, 1);
    return depth * taper * taper * (3 - 2 * taper);
}

bool snow_point_visible(const SnowScene *scene, size_t index, float x, float y) {
    for (size_t i = 0; i < index; i++) {
        if (inside(scene->drifts[i].surface.rect, x, y)) return false;
    }
    return true;
}

bool snow_exposed(const SnowScene *scene, size_t index, float x) {
    return span_at(&scene->drifts[index], x) != NULL;
}

float snow_edge_inset(SnowSurface surface) {
    return clamp(surface.corner_radius, 0, fminf(surface.rect.width, surface.rect.height) * .5f);
}

static void make_spans(SnowScene *scene, size_t index) {
    SnowDrift *drift = &scene->drifts[index];
    drift->span_count = 0;
    if (!drift->surface.catches) return;
    SnowRect rect = drift->surface.rect, screen = scene->screen;
    if (rect.y <= screen.y + 1 || rect.y >= screen.y + screen.height) return;
    float inset = snow_edge_inset(drift->surface);
    float left = fmaxf(screen.x, rect.x + inset);
    float right = fminf(screen.x + screen.width, rect.x + rect.width - inset);
    while (left < right && drift->span_count < SNOW_MAX_SPANS) {
        float blocked_until = left;
        float end = right;
        for (size_t i = 0; i < index; i++) {
            SnowRect front = scene->drifts[i].surface.rect;
            float front_right = front.x + front.width;
            if (rect.y < front.y || rect.y >= front.y + front.height || front_right <= left) continue;
            if (front.x <= left) blocked_until = fmaxf(blocked_until, front_right);
            else end = fminf(end, front.x);
        }
        if (blocked_until > left) {
            left = blocked_until;
        } else {
            drift->spans[drift->span_count++] = (SnowSpan){left, end};
            left = end;
        }
    }
}

static const float chunk_gravity = 590, chunk_terminal_speed = 460;

static SnowFlake *emit_chunk(SnowScene *scene, float x, float y, float removed, float vx, float vy,
                            bool falls_through, uint64_t surface) {
    if (removed < .12f || scene->flake_count == SNOW_MAX_FLAKES || scene->chunks_emitted >= 48) return NULL;
    scene->chunks_emitted++;
    SnowFlake *flake = &scene->flakes[scene->flake_count++];
    *flake = (SnowFlake){
        x, y, vx + (random_unit(scene) - .5f) * 55, vy + random_unit(scene) * 23,
        clamp(1.4f + sqrtf(removed), 1.5f, 5.1f), clamp(removed * .55f, .10f, 1.7f),
        surface, falls_through
    };
    return flake;
}

static bool same_rect(SnowRect a, SnowRect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static bool on_screen(SnowRect rect, SnowRect screen) {
    return rect.x < screen.x + screen.width && rect.x + rect.width > screen.x &&
           rect.y < screen.y + screen.height && rect.y + rect.height > screen.y;
}

static const SnowDrift *find_drift(const SnowDrift *drifts, size_t count, uint64_t id) {
    for (size_t i = 0; i < count; i++) {
        if (drifts[i].surface.id == id) return &drifts[i];
    }
    return NULL;
}

static void shake_drift(SnowScene *scene, SnowDrift *drift, float vx, float vy, float speed) {
    SnowRect rect = drift->surface.rect;
    float fraction = clamp((speed - 45) / 4900, 0, .085f);
    if (fraction <= 0) return;
    float next_chunk = -INFINITY;
    for (size_t bin = 0; bin < SNOW_BINS; bin++) {
        float x = rect.x + (bin + .5f) * rect.width / SNOW_BINS;
        if (drift->height[bin] < .35f || !span_at(drift, x)) continue;
        float depth = drift->height[bin];
        float removed = depth * fraction;
        drift->height[bin] -= removed;
        drift->revision = ++scene->revision;
        if (x >= next_chunk && removed >= .12f) {
            emit_chunk(scene, x, rect.y - depth - 1, removed * 2, clamp(vx * .56f, -290, 290),
                       clamp(vy * .40f - speed * .33f - 26, -250, -30), false, 0);
            next_chunk = x + 30;
        }
    }
}

static void scrape_drift(SnowScene *scene, SnowDrift *drift, const SnowDrift *old,
                         SnowRect previous, SnowRect current, float vx, float vy, int steps) {
    SnowRect rect = drift->surface.rect;
    float left = fminf(previous.x, current.x), right = fmaxf(previous.x + previous.width, current.x + current.width);
    if (right <= rect.x || left >= rect.x + rect.width ||
        fminf(previous.y, current.y) > rect.y ||
        fmaxf(previous.y + previous.height, current.y + current.height) < rect.y - SNOW_MAX_DEPTH) return;
    float next_chunk = -INFINITY;
    size_t last = bin_at(rect, right);
    for (size_t bin = bin_at(rect, left); bin <= last; bin++) {
        float depth = drift->height[bin];
        float x = rect.x + (bin + .5f) * rect.width / SNOW_BINS;
        float old_x = old->surface.rect.x + (bin + .5f) * old->surface.rect.width / SNOW_BINS;
        if (depth < .10f || !span_at(old, old_x)) continue;
        for (int step = 1; step <= steps; step++) {
            float t = (float)step / steps;
            float probe_x = previous.x + (current.x - previous.x) * t;
            float probe_y = previous.y + (current.y - previous.y) * t;
            if (x < probe_x || x >= probe_x + current.width ||
                probe_y > rect.y || probe_y + current.height < rect.y - depth) continue;
            drift->height[bin] = 0;
            drift->revision = ++scene->revision;
            if (x >= next_chunk) {
                emit_chunk(scene, x, rect.y - depth, depth, clamp(vx * .20f, -125, 125),
                           clamp(vy * .20f, 24, 140), true, drift->surface.id);
                next_chunk = x + 18;
            }
            break;
        }
    }
}

static void collide_drift_with_sides(SnowScene *scene, size_t moving, const SnowDrift *const *previous, float elapsed) {
    SnowDrift *drift = &scene->drifts[moving];
    const SnowDrift *old = previous[moving];
    if (!old->span_count) return;
    SnowRect start = old->surface.rect, end = drift->surface.rect;
    float dx = end.x - start.x, dy = end.y - start.y;
    if (fabsf(dx) < .01f) return;

    for (size_t i = 0; i + 1 < scene->drift_count; i++) {
        if (i == moving || !previous[i]) continue;
        SnowSurface obstacle = scene->drifts[i].surface;
        SnowRect before = previous[i]->surface.rect, after = obstacle.rect;
        if (before.width != after.width || before.height != after.height) continue;
        float wall_dx = after.x - before.x, wall_dy = after.y - before.y;
        float travel = dx - wall_dx;
        if (fabsf(travel) < .01f) continue;
        float inset = snow_edge_inset(obstacle);
        if (2 * inset >= before.height) continue;
        float top = before.y + inset, bottom = before.y + before.height - inset;
        float relative_dy = dy - wall_dy;
        if (fmaxf(start.y, start.y + relative_dy) <= top ||
            fminf(start.y, start.y + relative_dy) - SNOW_MAX_DEPTH >= bottom) continue;

        float side = travel > 0 ? before.x : before.x + before.width;
        float left = fminf(side, side - travel), right = fmaxf(side, side - travel);
        if (right < start.x || left > start.x + start.width) continue;
        size_t first = bin_at(start, left), last = bin_at(start, right);
        float last_chunk_x = -INFINITY, last_chunk_y = -INFINITY;
        for (size_t bin = first; bin <= last; bin++) {
            float x = start.x + (bin + .5f) * start.width / SNOW_BINS;
            float t = (side - x) / travel;
            if (t < 0 || t > 1 || drift->height[bin] < .10f) continue;
            const SnowSpan *span = span_at(old, x);
            if (!span) continue;
            float depth = snow_depth(old, *span, x);
            float base = start.y + relative_dy * t;
            if (depth < .10f || base <= top || base - depth >= bottom) continue;
            drift->height[bin] = 0;
            drift->revision = ++scene->revision;

            float contact_y = fmaxf(top, base - depth) + wall_dy * t;
            if (x - last_chunk_x < 14 && fabsf(contact_y - last_chunk_y) < 14) continue;
            float outward = travel > 0 ? -1 : 1;
            float contact_x = side + wall_dx * t + outward * 5;
            float push = clamp(fabsf(travel) * .18f / elapsed, 40, 140) * outward;
            emit_chunk(scene, contact_x, contact_y, depth, push, 32, true, drift->surface.id);
            last_chunk_x = x;
            last_chunk_y = contact_y;
        }
    }
}

void snow_init(SnowScene *scene, SnowRect screen, uint32_t seed) {
    memset(scene, 0, sizeof *scene);
    scene->screen = screen;
    scene->random = seed ? seed : 1;
    snow_surfaces(scene, NULL, 0);
}

void snow_set_inverted(SnowScene *scene, bool enabled) {
    if (enabled == scene->inverted) return;
    uint64_t revision = scene->revision;
    snow_init(scene, scene->screen, scene->random);
    scene->inverted = enabled;
    /* View caches must not reuse revisions from the preceding scene. */
    scene->drifts[0].revision = scene->revision = revision + 1;
}

float snow_screen_y(const SnowScene *scene, float y) {
    SnowRect screen = scene->screen;
    return scene->inverted ? screen.y + screen.height - (y - screen.y) : y;
}

void snow_surfaces(SnowScene *scene, const SnowSurface *surfaces, size_t count) {
    SnowSurface visible[SNOW_MAX_SURFACES];
    size_t visible_count = 0;
    for (size_t i = 0; i < count && visible_count < SNOW_MAX_SURFACES - 1; i++) {
        SnowSurface surface = surfaces[i];
        if (!on_screen(surface.rect, scene->screen)) continue;
        if (scene->inverted) surface.rect.y = snow_screen_y(scene, surface.rect.y + surface.rect.height);
        visible[visible_count++] = surface;
    }
    visible[visible_count++] = (SnowSurface){
        .rect = {scene->screen.x, scene->screen.y + scene->screen.height - 1, scene->screen.width, 1},
        .catches = true,
    };
    bool same = visible_count == scene->drift_count;
    for (size_t i = 0; same && i < visible_count; i++) {
        SnowSurface current = scene->drifts[i].surface;
        same = current.id == visible[i].id && current.catches == visible[i].catches &&
               current.corner_radius == visible[i].corner_radius &&
               same_rect(current.rect, visible[i].rect);
    }
    float elapsed = clamp(scene->geometry_elapsed, .03f, .18f);
    scene->geometry_elapsed = 0;
    if (same) return;

    memcpy(scene->previous, scene->drifts, scene->drift_count * sizeof(SnowDrift));
    size_t previous_count = scene->drift_count;
    scene->drift_count = visible_count;
    const SnowDrift *previous[SNOW_MAX_SURFACES];
    for (size_t i = 0; i < visible_count; i++) {
        SnowDrift *drift = &scene->drifts[i];
        drift->surface = visible[i];
        memset(drift->height, 0, sizeof drift->height);
        const SnowDrift *old = find_drift(scene->previous, previous_count, visible[i].id);
        previous[i] = old;
        if (old && visible[i].catches) memcpy(drift->height, old->height, sizeof drift->height);
        make_spans(scene, i);
        bool unchanged = old && same_rect(old->surface.rect, visible[i].rect) &&
                         old->surface.catches == visible[i].catches && old->span_count == drift->span_count &&
                         memcmp(old->spans, drift->spans, drift->span_count * sizeof(SnowSpan)) == 0;
        drift->revision = unchanged ? old->revision : ++scene->revision;
    }
    for (size_t i = 0; i + 1 < visible_count; i++) {
        SnowDrift *drift = &scene->drifts[i];
        const SnowDrift *old = previous[i];
        SnowRect now = visible[i].rect;
        if (!old || old->surface.rect.width != now.width || old->surface.rect.height != now.height) continue;
        SnowRect before = old->surface.rect;
        float dx = now.x - before.x, dy = now.y - before.y;
        collide_drift_with_sides(scene, i, previous, elapsed);
        float distance = hypotf(dx, dy);
        if (distance < 1.5f) continue;
        shake_drift(scene, drift, dx / elapsed, dy / elapsed, distance / elapsed);
        int steps = (int)ceilf(clamp(distance / 10, 1, 18));
        for (size_t j = i + 1; j < visible_count; j++) {
            SnowDrift *other = &scene->drifts[j];
            const SnowDrift *old_other = previous[j];
            if (old_other && other->surface.catches) {
                scrape_drift(scene, other, old_other, before, now, dx / elapsed, dy / elapsed, steps);
            }
        }
    }
    for (size_t i = 0; i < scene->ball_count;) {
        if (snow_ball_surface(scene, &scene->balls[i])) {
            i++;
        } else {
            scene->balls[i] = scene->balls[--scene->ball_count];
        }
    }
}

static void spawn(SnowScene *scene) {
    if (scene->flake_count >= SNOW_MAX_FLAKES - 96) return;
    float depth = random_unit(scene);
    SnowRect screen = scene->screen;
    SnowFlake *flake = &scene->flakes[scene->flake_count++];
    *flake = (SnowFlake){
        .x = screen.x + random_unit(scene) * screen.width, .y = screen.y - 5,
        .vx = (random_unit(scene) - 0.5f) * 32, .vy = 42 + depth * 105,
        .radius = 0.8f + depth * 2.6f
    };
}

static bool settle(SnowScene *scene, const SnowFlake *flake, float before) {
    size_t target = scene->drift_count;
    float contact = flake->y + 1;
    for (size_t i = 0; i < scene->drift_count; i++) {
        SnowDrift *drift = &scene->drifts[i];
        SnowRect rect = drift->surface.rect;
        if (!drift->span_count || rect.y < before || rect.y > flake->y + SNOW_MAX_DEPTH ||
            flake->x < rect.x || flake->x >= rect.x + rect.width ||
            (flake->falling_chunk && flake->skip == drift->surface.id)) continue;
        const SnowSpan *span = span_at(drift, flake->x);
        if (!span) continue;
        float top = rect.y - snow_depth(drift, *span, flake->x);
        if (before <= top && flake->y >= top && top < contact) {
            target = i;
            contact = top;
        }
    }
    if (target == scene->drift_count) return false;
    SnowDrift *drift = &scene->drifts[target];
    size_t bin = bin_at(drift->surface.rect, flake->x);
    size_t left = bin > 0 ? bin - 1 : 0;
    size_t right = bin + 1 < SNOW_BINS ? bin + 1 : SNOW_BINS - 1;
    float deposit = flake->deposit > 0 ? flake->deposit : .20f + flake->radius * .11f;
    for (size_t i = left; i <= right; i++) {
        float x = drift->surface.rect.x + (i + .5f) * drift->surface.rect.width / SNOW_BINS;
        if (!span_at(drift, x)) continue;
        float limit = SNOW_MAX_DEPTH - 4 + 2.2f * sinf((float)i * .12f) - 1.4f * sinf((float)i * .51f);
        float weight = i == bin ? 1 : .42f;
        float old = drift->height[i];
        drift->height[i] = fminf(limit, old + deposit * weight * (1 - old / (limit * 1.45f)));
        if (drift->height[i] != old) drift->revision = ++scene->revision;
    }
    return true;
}

void snow_step(SnowScene *scene, float dt, int intensity, float wind) {
    dt = clamp(dt, 0, 0.05f);
    scene->geometry_elapsed += dt;
    scene->chunks_emitted = 0;
    const float rates[] = {26, 54, 125};
    intensity = intensity < 0 ? 0 : intensity > 2 ? 2 : intensity;
    scene->budget += rates[intensity] * dt * clamp(scene->screen.width / 1200, 0.45f, 1.8f);
    while (scene->budget >= 1) {
        spawn(scene);
        scene->budget -= 1;
    }
    scene->wind += (clamp(wind, -1, 1) * 155 - scene->wind) * fminf(dt * 4, 1);
    SnowRect screen = scene->screen;
    for (size_t i = 0; i < scene->flake_count;) {
        SnowFlake *flake = &scene->flakes[i];
        float before = flake->y;
        bool chunk = flake->deposit > 0;
        if (chunk) flake->vy = fminf(chunk_terminal_speed, flake->vy + chunk_gravity * dt);
        flake->x += (flake->vx + scene->wind * (chunk ? .18f : 1)) * dt;
        flake->y += flake->vy * dt;
        if (!chunk && flake->x < screen.x) flake->x += screen.width;
        if (!chunk && flake->x >= screen.x + screen.width) flake->x -= screen.width;
        if ((flake->vy > 0 && settle(scene, flake, before)) || flake->y > screen.y + screen.height + 8 ||
            (chunk && (flake->x < screen.x - 8 || flake->x > screen.x + screen.width + 8))) {
            *flake = scene->flakes[--scene->flake_count];
        } else {
            i++;
        }
    }
}

static float remove_snow(SnowScene *scene, float x, float y, float radius, float fraction, float push) {
    float removed = 0;
    for (size_t i = 0; i < scene->drift_count; i++) {
        SnowDrift *drift = &scene->drifts[i];
        SnowRect rect = drift->surface.rect;
        if (!drift->span_count || rect.y < y - radius || rect.y > y + radius + SNOW_MAX_DEPTH + 5 ||
            x + radius < rect.x || x - radius >= rect.x + rect.width) continue;
        size_t first = bin_at(rect, x - radius);
        size_t last = bin_at(rect, x + radius);
        float next_chunk = -INFINITY;
        for (size_t bin = first; bin <= last; bin++) {
            float center = rect.x + (bin + 0.5f) * rect.width / SNOW_BINS;
            float height = drift->height[bin];
            if (height <= 0 || y < rect.y - height - radius || !snow_exposed(scene, i, center)) continue;
            float removed_here = height * fraction;
            drift->height[bin] -= removed_here;
            drift->revision = ++scene->revision;
            if (fraction == 1 && center >= next_chunk) {
                emit_chunk(scene, center, rect.y - height, removed_here, push, 30, true, drift->surface.id);
                next_chunk = center + 12;
            }
            removed += removed_here * rect.width / SNOW_BINS;
        }
    }
    return removed;
}

void snow_sweep(SnowScene *scene, float x, float y) {
    if (scene->pointer_known) {
        float distance = hypotf(x - scene->previous_x, y - scene->previous_y);
        if (distance >= 1) {
            int steps = (int)ceilf(clamp(distance / 12, 1, 24));
            for (int i = 1; i <= steps; i++) {
                float t = (float)i / steps;
                remove_snow(scene, scene->previous_x + (x - scene->previous_x) * t,
                            scene->previous_y + (y - scene->previous_y) * t, 13, 1,
                            clamp((x - scene->previous_x) * 2.8f, -180, 180));
            }
        }
    }
    scene->previous_x = x;
    scene->previous_y = y;
    scene->pointer_known = true;
}

const SnowDrift *snow_ball_surface(const SnowScene *scene, const SnowBall *ball) {
    for (size_t i = 0; i < scene->drift_count; i++) {
        const SnowDrift *drift = &scene->drifts[i];
        if (drift->surface.id == ball->surface && drift->surface.catches) return drift;
    }
    return NULL;
}

float snow_ball_height(const SnowScene *scene, const SnowBall *ball, float x) {
    const SnowDrift *drift = snow_ball_surface(scene, ball);
    if (!drift) return 0;
    SnowRect rect = drift->surface.rect;
    const SnowSpan *span = span_at(drift, x);
    return rect.y - (span ? snow_depth(drift, *span, x) : 0) - ball->rise - ball->radius;
}

static bool same_column(const SnowBall *a, const SnowBall *b) {
    return a->surface == b->surface && a->offset == b->offset;
}

static bool touching_balls(const SnowBall *lower, const SnowBall *upper) {
    return fabsf(upper->rise - (lower->rise + 2 * lower->radius)) < .01f;
}

SnowmanPart snow_snowman_details(const SnowScene *scene, const SnowBall *ball, unsigned *appearance) {
    const SnowBall *parts[3];
    size_t count = 0;
    for (size_t i = 0; i < scene->ball_count; i++) {
        const SnowBall *part = &scene->balls[i];
        if (!same_column(part, ball)) continue;
        if (count == 3) return SNOWMAN_NONE;
        size_t slot = count++;
        while (slot > 0 && parts[slot - 1]->rise > part->rise) {
            parts[slot] = parts[slot - 1];
            slot--;
        }
        parts[slot] = part;
    }
    if (count != 3 || parts[0]->rise != 0 || !touching_balls(parts[0], parts[1]) ||
        !touching_balls(parts[1], parts[2])) return SNOWMAN_NONE;
    if (appearance) *appearance = parts[2]->appearance;
    if (ball->rise == parts[0]->rise) return SNOWMAN_BASE;
    if (ball->rise == parts[1]->rise) return SNOWMAN_BODY;
    return SNOWMAN_HEAD;
}

SnowmanPart snow_snowman_part(const SnowScene *scene, const SnowBall *ball) {
    return snow_snowman_details(scene, ball, NULL);
}

bool snow_holds_snowman(const SnowScene *scene) {
    return scene->holding && scene->attached_count == 2 && scene->ball_origin.rise == 0 &&
           touching_balls(&scene->ball_origin, &scene->attached_balls[0]) &&
           touching_balls(&scene->attached_balls[0], &scene->attached_balls[1]);
}

static float attached_ball_rise(const SnowScene *scene, const SnowBall *ball) {
    return ball->rise - scene->ball_origin.rise + 2 * (scene->ball_radius - scene->ball_origin.radius);
}

float snow_attached_ball_height(const SnowScene *scene, const SnowBall *ball) {
    return scene->ball_y + scene->ball_radius - attached_ball_rise(scene, ball) - ball->radius;
}

static size_t ball_at(const SnowScene *scene, float x, float y) {
    for (size_t i = scene->ball_count; i > 0; i--) {
        const SnowBall *ball = &scene->balls[i - 1];
        const SnowDrift *drift = snow_ball_surface(scene, ball);
        if (!drift) continue;
        SnowRect rect = drift->surface.rect;
        float center_x = rect.x + ball->offset * rect.width;
        float center_y = snow_ball_height(scene, ball, center_x);
        if (hypotf(x - center_x, y - center_y) <= ball->radius &&
            snow_exposed(scene, drift - scene->drifts, center_x) &&
            snow_point_visible(scene, drift - scene->drifts, center_x, center_y)) return i - 1;
    }
    return scene->ball_count;
}

bool snow_has_ball_at(const SnowScene *scene, float x, float y) {
    return ball_at(scene, x, y) < scene->ball_count;
}

bool snow_pick_ball(SnowScene *scene, float x, float y) {
    if (scene->holding) return false;
    size_t index = ball_at(scene, x, y);
    if (index == scene->ball_count) return false;
    scene->ball_origin = scene->balls[index];
    scene->balls[index] = scene->balls[--scene->ball_count];
    for (size_t i = 0; i < scene->ball_count;) {
        SnowBall part = scene->balls[i];
        if (!same_column(&part, &scene->ball_origin) || part.rise <= scene->ball_origin.rise) {
            i++;
            continue;
        }
        size_t slot = scene->attached_count++;
        while (slot > 0 && scene->attached_balls[slot - 1].rise > part.rise) {
            scene->attached_balls[slot] = scene->attached_balls[slot - 1];
            slot--;
        }
        scene->attached_balls[slot] = part;
        scene->balls[i] = scene->balls[--scene->ball_count];
    }
    scene->ball_radius = scene->ball_origin.radius;
    scene->ball_x = x;
    scene->ball_y = y;
    scene->moving_ball = scene->holding = true;
    return true;
}

static void place_held_stack(SnowScene *scene, SnowBall base) {
    base.radius = scene->ball_radius;
    scene->balls[scene->ball_count++] = base;
    for (size_t i = 0; i < scene->attached_count; i++) {
        const SnowBall *part = &scene->attached_balls[i];
        scene->balls[scene->ball_count++] = (SnowBall){
            base.surface, base.offset, base.rise + attached_ball_rise(scene, part), part->radius, part->appearance
        };
    }
}

void snow_cancel_ball(SnowScene *scene) {
    if (scene->moving_ball && scene->ball_count + scene->attached_count < SNOW_MAX_BALLS &&
        snow_ball_surface(scene, &scene->ball_origin)) {
        place_held_stack(scene, scene->ball_origin);
    }
    scene->attached_count = 0;
    scene->moving_ball = scene->holding = false;
}

static void shatter_ball(SnowScene *scene, float x, float y, float radius) {
    int pieces = (int)(7 + .4f * radius);
    for (int i = 0; i < pieces; i++) {
        float spread = 2.0f * i / (pieces - 1) - 1;
        float height = random_unit(scene) - .5f;
        emit_chunk(scene, x + spread * radius * .7f, y + height * radius,
                   .4f + radius * .075f, spread * (65 + 3 * radius), -40 - random_unit(scene) * 65, false, 0);
    }
}

SnowBreak snow_break_ball(SnowScene *scene, float x, float y) {
    if (scene->holding) {
        bool hit = hypotf(x - scene->ball_x, y - scene->ball_y) <= scene->ball_radius;
        for (size_t i = 0; i < scene->attached_count && !hit; i++) {
            const SnowBall *part = &scene->attached_balls[i];
            hit = hypotf(x - scene->ball_x, y - snow_attached_ball_height(scene, part)) <= part->radius;
        }
        if (!hit) return SNOW_BREAK_NONE;
        SnowBreak result = snow_holds_snowman(scene) ? SNOW_BREAK_SNOWMAN : SNOW_BREAK_BALL;
        shatter_ball(scene, scene->ball_x, scene->ball_y, scene->ball_radius);
        for (size_t i = 0; i < scene->attached_count; i++) {
            const SnowBall *part = &scene->attached_balls[i];
            shatter_ball(scene, scene->ball_x, snow_attached_ball_height(scene, part), part->radius);
        }
        scene->moving_ball = false;
        snow_cancel_ball(scene);
        return result;
    }
    size_t index = ball_at(scene, x, y);
    if (index == scene->ball_count) return SNOW_BREAK_NONE;
    SnowBall broken = scene->balls[index];
    bool whole_snowman = snow_snowman_part(scene, &scene->balls[index]) != SNOWMAN_NONE;
    const SnowDrift *drift = snow_ball_surface(scene, &broken);
    float center = drift->surface.rect.x + broken.offset * drift->surface.rect.width;
    shatter_ball(scene, center, snow_ball_height(scene, &broken, center), broken.radius);
    scene->balls[index] = scene->balls[--scene->ball_count];
    for (size_t i = 0; i < scene->ball_count;) {
        SnowBall *part = &scene->balls[i];
        if (same_column(part, &broken)) {
            if (whole_snowman) {
                shatter_ball(scene, center, snow_ball_height(scene, part, center), part->radius);
                *part = scene->balls[--scene->ball_count];
                continue;
            }
            if (part->rise > broken.rise) part->rise -= 2 * broken.radius;
        }
        i++;
    }
    return whole_snowman ? SNOW_BREAK_SNOWMAN : SNOW_BREAK_BALL;
}

bool snow_grow_snowman(SnowScene *scene, float near_x, float range, SnowBall *base) {
    if (scene->ball_count + scene->attached_count + scene->holding + 3 > SNOW_MAX_BALLS) return false;
    const float radius = 14;
    size_t selected = scene->drift_count, candidates = 0;
    float chosen_x = 0;
    for (size_t i = 0; i < scene->drift_count; i++) {
        const SnowDrift *drift = &scene->drifts[i];
        SnowRect rect = drift->surface.rect;
        if (!drift->span_count || rect.y < scene->screen.y + 83) continue;
        for (size_t bin = 0; bin < SNOW_BINS; bin += 2) {
            float x = rect.x + (bin + .5f) * rect.width / SNOW_BINS;
            if (fabsf(x - near_x) > range) continue;
            const SnowSpan *span = span_at(drift, x);
            if (!span || x - span->left < radius + 4 || span->right - x < radius + 4 ||
                snow_depth(drift, *span, x) < 1.4f) continue;
            if (!snow_point_visible(scene, i, x, rect.y - 38) ||
                !snow_point_visible(scene, i, x, rect.y - 66)) continue;
            bool occupied = false;
            for (size_t j = 0; j < scene->ball_count && !occupied; j++) {
                const SnowBall *ball = &scene->balls[j];
                occupied = ball->surface == drift->surface.id &&
                    fabsf(rect.x + ball->offset * rect.width - x) < ball->radius + radius + 5;
            }
            if (occupied) continue;
            if (random_unit(scene) * ++candidates < 1) {
                selected = i;
                chosen_x = x;
            }
        }
    }
    if (selected == scene->drift_count) return false;
    SnowDrift *drift = &scene->drifts[selected];
    SnowRect rect = drift->surface.rect;
    float offset = (chosen_x - rect.x) / rect.width;
    unsigned appearance = (unsigned)(random_unit(scene) * SNOWMAN_APPEARANCES);
    *base = (SnowBall){drift->surface.id, offset, 0, radius, appearance};
    scene->balls[scene->ball_count++] = *base;
    scene->balls[scene->ball_count++] = (SnowBall){base->surface, offset, 28, 11, appearance};
    scene->balls[scene->ball_count++] = (SnowBall){base->surface, offset, 50, 8.5f, appearance};
    for (size_t bin = bin_at(rect, chosen_x - 20); bin <= bin_at(rect, chosen_x + 20); bin++) {
        float x = rect.x + (bin + .5f) * rect.width / SNOW_BINS;
        drift->height[bin] *= .25f + .75f * clamp(fabsf(x - chosen_x) / 22, 0, 1);
    }
    drift->revision = ++scene->revision;
    for (int i = -3; i <= 3; i++) {
        emit_chunk(scene, chosen_x + i * 3, rect.y - 5, 1.1f, i * 29, -96, false, 0);
    }
    return true;
}

void snow_throw_snowmen(SnowScene *scene, float target_x, float target_y) {
    SnowRect screen = scene->screen;
    target_x = clamp(target_x, screen.x, screen.x + screen.width);
    target_y = clamp(target_y, screen.y, screen.y + screen.height);
    size_t throws = 0;
    for (size_t i = 0; i < scene->ball_count && throws < 4; i++) {
        const SnowBall *ball = &scene->balls[i];
        if (ball->rise < 1 || snow_snowman_part(scene, ball) != SNOWMAN_HEAD) continue;
        const SnowDrift *drift = snow_ball_surface(scene, ball);
        if (!drift) continue;
        SnowRect rect = drift->surface.rect;
        float x = rect.x + ball->offset * rect.width, y = snow_ball_height(scene, ball, x);
        if (!span_at(drift, x) || !snow_point_visible(scene, drift - scene->drifts, x, y)) continue;
        float dx = target_x - x, dy = target_y - y;
        float flight = fmaxf(clamp(hypotf(dx, dy) / 680, .32f, 1.65f), dy / 390 + .08f);
        float vy = dy / flight - .5f * chunk_gravity * flight;
        if (vy + chunk_gravity * flight > chunk_terminal_speed) {
            // The remaining part of the descent runs at terminal speed.
            vy = chunk_terminal_speed - sqrtf(2 * chunk_gravity * (chunk_terminal_speed * flight - dy));
        }
        SnowFlake *shot = emit_chunk(scene, x, y, 16, 0, 0, true, ball->surface);
        if (!shot) break;
        shot->vx = dx / flight - scene->wind * .18f;
        shot->vy = vy - .01f * chunk_gravity;
        throws++;
    }
}

void snow_begin_ball(SnowScene *scene, float x, float y) {
    if (scene->holding) return;
    scene->holding = true;
    scene->ball_x = x;
    scene->ball_y = y;
    scene->ball_radius = 6;
    snow_move_ball(scene, x, y);
}

void snow_move_ball(SnowScene *scene, float x, float y) {
    if (!scene->holding) return;
    if (scene->moving_ball && x == scene->ball_x && y == scene->ball_y) return;
    scene->ball_x = x;
    scene->ball_y = y;
    float collected = remove_snow(scene, x, y + scene->ball_radius * 0.6f,
                                  scene->ball_radius + 9, 0.65f, 0);
    scene->ball_radius = fminf(34, sqrtf(scene->ball_radius * scene->ball_radius + collected * 0.18f));
}

void snow_drop_ball(SnowScene *scene, float x, float y) {
    if (!scene->holding) return;
    snow_move_ball(scene, x, y);
    if (scene->ball_count + scene->attached_count >= SNOW_MAX_BALLS) {
        snow_cancel_ball(scene);
        return;
    }
    size_t best = scene->drift_count;
    float best_distance = INFINITY, best_rise = 0, best_offset = 0;
    for (size_t i = 0; i < scene->drift_count; i++) {
        const SnowDrift *drift = &scene->drifts[i];
        SnowRect rect = drift->surface.rect;
        const SnowSpan *span = span_at(drift, x);
        if (!span) continue;
        float top = rect.y - snow_depth(drift, *span, x);
        float rise = 0;
        float offset = (x - rect.x) / rect.width;
        for (size_t j = 0; j < scene->ball_count; j++) {
            SnowBall ball = scene->balls[j];
            float ball_x = rect.x + ball.offset * rect.width;
            if (ball.surface == drift->surface.id && fabsf(x - ball_x) < ball.radius &&
                ball.rise + ball.radius * 2 > rise) {
                rise = ball.rise + ball.radius * 2;
                offset = ball.offset;
            }
        }
        float distance = fabsf(y + scene->ball_radius - (top - rise));
        if (distance < best_distance && distance < fmaxf(75, scene->screen.height * 0.26f)) {
            best_distance = distance;
            best = i;
            best_rise = rise;
            best_offset = offset;
        }
    }
    if (best < scene->drift_count) {
        unsigned appearance = scene->moving_ball ? scene->ball_origin.appearance :
            (unsigned)(random_unit(scene) * SNOWMAN_APPEARANCES);
        place_held_stack(scene, (SnowBall){scene->drifts[best].surface.id, best_offset, best_rise,
                                         scene->ball_radius, appearance});
        scene->moving_ball = false;
    }
    snow_cancel_ball(scene);
}

void snow_clear(SnowScene *scene) {
    scene->flake_count = 0;
    scene->ball_count = 0;
    scene->budget = 0;
    scene->chunks_emitted = 0;
    scene->attached_count = 0;
    scene->moving_ball = scene->holding = false;
    scene->pointer_known = false;
    for (size_t i = 0; i < scene->drift_count; i++) {
        memset(scene->drifts[i].height, 0, sizeof scene->drifts[i].height);
        scene->drifts[i].revision = ++scene->revision;
    }
}
