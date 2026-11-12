#include "snow.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static float mass(const SnowDrift *drift) {
    float result = 0;
    for (size_t i = 0; i < SNOW_BINS; i++) result += drift->height[i];
    return result;
}

static void check_moving_snowman(bool inverted) {
    static SnowScene scene;
    snow_init(&scene, (SnowRect){0, 0, 700, 500}, 42);
    snow_set_inverted(&scene, inverted);
    const SnowSurface surface = {7, {50, inverted ? 230 : 180, 500, 90}, true, 10};
    snow_surfaces(&scene, &surface, 1);
    scene.balls[scene.ball_count++] = (SnowBall){7, .3f, 0, 10, 1};
    scene.balls[scene.ball_count++] = (SnowBall){7, .3f, 20, 8, 2};
    scene.balls[scene.ball_count++] = (SnowBall){7, .3f, 36, 6, 3};
    scene.balls[scene.ball_count++] = (SnowBall){7, .1f, 0, 7, 0};
    assert(snow_pick_ball(&scene, 200, 170));
    for (size_t i = 62; i < 78; i++) scene.drifts[0].height[i] = 12;
    float snow = mass(&scene.drifts[0]);
    snow_move_ball(&scene, 325, 161);
    assert(scene.ball_radius == 10 && mass(&scene.drifts[0]) == snow);
    assert(scene.ball_x == 325 && scene.ball_y == 161 && snow_holds_snowman(&scene));
    assert(scene.ball_count == 1 && scene.balls[0].offset == .1f);
    snow_drop_ball(&scene, 332, 161);
    assert(scene.ball_count == 4);
    SnowBall base = scene.balls[1];
    assert(base.radius == 10 && base.rise == 0 && base.offset > .5f);
    for (size_t i = 2; i < 4; i++) assert(scene.balls[i].offset == base.offset);
    assert(scene.balls[2].radius == 8 && scene.balls[2].rise == 20);
    assert(scene.balls[3].radius == 6 && scene.balls[3].rise == 36 && scene.balls[3].appearance == 3);
    assert(mass(&scene.drifts[0]) == snow);
    assert(snow_snowman_part(&scene, &scene.balls[1]) == SNOWMAN_BASE);
    assert(snow_snowman_part(&scene, &scene.balls[2]) == SNOWMAN_BODY);
    assert(snow_snowman_part(&scene, &scene.balls[3]) == SNOWMAN_HEAD);
    float y = snow_ball_height(&scene, &base, 332);
    assert(snow_pick_ball(&scene, 332, y) && snow_holds_snowman(&scene));
    snow_drop_ball(&scene, -15, 10);
    assert(scene.ball_count == 4 && !scene.holding);
    float head_y = snow_ball_height(&scene, &scene.balls[3], 332);
    assert(snow_break_ball(&scene, 332, head_y));
    assert(scene.ball_count == 1 && scene.balls[0].offset == .1f && scene.flake_count > 20);
    assert(snow_pick_ball(&scene, 100, 173));
    snow_move_ball(&scene, 325, 161);
    assert(scene.ball_radius > 7 && mass(&scene.drifts[0]) < snow);
}

static void check_side_collision(bool inverted, bool moves_right, bool obstacle_in_front, bool slow) {
    static SnowScene scene;
    const SnowRect screen = {0, 0, 680, 450};
    snow_init(&scene, screen, 42);
    snow_set_inverted(&scene, inverted);
    size_t moving = obstacle_in_front ? 1 : 0, obstacle = 1 - moving;
    SnowSurface windows[2];
    windows[moving] = (SnowSurface){7, {moves_right ? 40 : 374, 200, 160, 60}, true, 12};
    windows[obstacle] = (SnowSurface){8, {260, 145, 70, 150}, true, 12};
    snow_surfaces(&scene, windows, 2);
    for (size_t i = 0; i < SNOW_BINS; i++) scene.drifts[moving].height[i] = 12;
    /* A snow column can pass through the full obstacle between snapshots. */
    if (slow) {
        for (int i = 0; i < 120; i++) {
            windows[moving].rect.x++;
            snow_surfaces(&scene, windows, 2);
        }
    } else {
        windows[moving].rect.x += moves_right ? 180 : -120;
        snow_surfaces(&scene, windows, 2);
    }
    assert(scene.drifts[moving].height[moves_right ? 95 : 33] == 0);
    assert(scene.drifts[moving].height[moves_right ? 12 : 115] > 10);
    bool contact = false;
    for (size_t i = 0; i < scene.flake_count; i++) {
        SnowFlake flake = scene.flakes[i];
        if (flake.falling_chunk && flake.skip == windows[moving].id) {
            assert(moves_right ? flake.x < 260 && flake.vx < 0 : flake.x > 330 && flake.vx > 0);
            contact = true;
        }
    }
    assert(contact);
}

static void check_snowman_aim(float target_y) {
    static SnowScene scene;
    snow_init(&scene, (SnowRect){0, 0, 680, 450}, 42);
    const SnowSurface window = {7, {100, 220, 200, 90}, true, 12};
    snow_surfaces(&scene, &window, 1);
    scene.balls[scene.ball_count++] = (SnowBall){7, .5f, 0, 14, 0};
    scene.balls[scene.ball_count++] = (SnowBall){7, .5f, 28, 11, 0};
    scene.balls[scene.ball_count++] = (SnowBall){7, .5f, 50, 9, 0};

    const float target_x = 440;
    snow_throw_snowmen(&scene, target_x, target_y);
    assert(scene.flake_count == 1);
    float closest = INFINITY;
    for (int i = 0; i < 120; i++) {
        snow_step(&scene, .01f, 0, 0);
        for (size_t j = 0; j < scene.flake_count; j++) {
            SnowFlake shot = scene.flakes[j];
            if (shot.falling_chunk && shot.skip == window.id) {
                closest = fminf(closest, hypotf(shot.x - target_x, shot.y - target_y));
            }
        }
    }
    assert(closest < 9);
}

int main(void) {
    check_moving_snowman(false);
    check_moving_snowman(true);
    check_side_collision(false, true, false, false);
    check_side_collision(true, false, true, false);
    check_side_collision(false, true, true, true);
    check_snowman_aim(60);
    check_snowman_aim(365);
    static SnowScene scene;
    const SnowSurface windows[] = {
        {14, {280, 150, 160, 260}, true, 3},
        {17, {110, 320, 610, 210}, true, 3},
    };
    snow_init(&scene, (SnowRect){-120, 0, 960, 640}, 42);
    snow_surfaces(&scene, windows, 2);
    assert(scene.drift_count == 3);
    assert(snow_exposed(&scene, 1, 180));
    assert(!snow_exposed(&scene, 1, 340));
    for (int i = 0; i < 1000; i++) snow_step(&scene, 1.0f / 30, 2, 0.5f);
    assert(mass(&scene.drifts[0]) > 0);
    assert(mass(&scene.drifts[1]) > 0);
    assert(mass(&scene.drifts[2]) > 0);
    assert(scene.drifts[1].height[45] == 0);

    const float old_mass = mass(&scene.drifts[0]);
    SnowSurface moved[] = {windows[0], windows[1]};
    moved[0].rect.x += 50;
    moved[0].rect.y += 35;
    snow_surfaces(&scene, moved, 2);
    assert(mass(&scene.drifts[0]) < old_mass);
    assert(mass(&scene.drifts[0]) > old_mass * .80f);
    bool lifted = false;
    for (size_t i = 0; i < scene.flake_count; i++) {
        SnowFlake flake = scene.flakes[i];
        if (flake.deposit > 0 && flake.vy < 0 && flake.vx > 0) lifted = true;
    }
    assert(lifted);
    assert(!snow_exposed(&scene, 0, moved[0].rect.x - 1));
    assert(!snow_exposed(&scene, 0, moved[0].rect.x + moved[0].rect.width));
    snow_sweep(&scene, 335, 179);
    snow_sweep(&scene, 460, 179);
    assert(mass(&scene.drifts[0]) < old_mass);

    snow_clear(&scene);
    assert(!scene.flake_count && !scene.ball_count && mass(&scene.drifts[0]) == 0);
    snow_begin_ball(&scene, 410, 179);
    snow_drop_ball(&scene, 410, 179);
    snow_begin_ball(&scene, 414, 167);
    snow_drop_ball(&scene, 414, 167);
    assert(scene.ball_count == 2);
    assert(scene.balls[0].surface == moved[0].id);
    assert(scene.balls[1].rise == 2 * scene.balls[0].radius);
    assert(fabsf(scene.balls[0].offset - scene.balls[1].offset) < .0001f);
    SnowBall upper = scene.balls[1];
    float ball_x = moved[0].rect.x + upper.offset * moved[0].rect.width;
    float ball_y = snow_ball_height(&scene, &upper, ball_x);
    assert(snow_has_ball_at(&scene, ball_x, ball_y));
    assert(snow_pick_ball(&scene, ball_x, ball_y) && scene.ball_count == 1);
    snow_drop_ball(&scene, scene.screen.x - 30, 100);
    assert(scene.ball_count == 2 && scene.balls[1].offset == upper.offset && scene.balls[1].rise == upper.rise);
    assert(snow_pick_ball(&scene, ball_x, ball_y));
    snow_drop_ball(&scene, 452, 179);
    assert(scene.ball_count == 2 && scene.balls[1].offset > upper.offset);
    assert(scene.balls[1].rise == 0 && scene.balls[1].radius == upper.radius);
    snow_surfaces(&scene, moved + 1, 1);
    assert(scene.ball_count == 0);
    snow_init(&scene, (SnowRect){0, 0, 700, 500}, 19);
    SnowSurface shelves[] = {
        {8, {45, 95, 130, 80}, true, 3},
        {9, {90, 250, 530, 100}, true, 3},
        {10, {60, 375, 620, 80}, true, 3},
    };
    snow_surfaces(&scene, shelves, 3);
    for (size_t i = 0; i < SNOW_BINS; i++) scene.drifts[1].height[i] = 8;
    assert(scene.drifts[1].span_count == 1);
    SnowSpan span = scene.drifts[1].spans[0];
    assert(span.left >= shelves[1].rect.x);
    assert(span.right <= shelves[1].rect.x + shelves[1].rect.width);
    assert(snow_depth(&scene.drifts[1], span, span.left) == 0);
    assert(snow_depth(&scene.drifts[1], span, span.right) == 0);
    assert(snow_depth(&scene.drifts[1], span, span.left + 15) == 8);

    snow_sweep(&scene, 180, 241);
    snow_sweep(&scene, 230, 241);
    assert(scene.flake_count > 0);
    assert(scene.flakes[0].falling_chunk && scene.flakes[0].skip == shelves[1].id);
    for (int i = 0; i < 28; i++) snow_step(&scene, 1.0f / 30, 0, 0);
    assert(mass(&scene.drifts[2]) > 0);

    shelves[0].rect.x = 275;
    shelves[0].rect.y = 195;
    float before = mass(&scene.drifts[1]);
    snow_surfaces(&scene, shelves, 3);
    assert(mass(&scene.drifts[1]) < before);
    assert(scene.drifts[1].span_count == 2);
    assert(scene.drifts[1].spans[0].right == 275);
    assert(scene.drifts[1].spans[1].left == 405);
    snow_init(&scene, (SnowRect){0, -100, 700, 500}, 72);
    SnowSurface target = {32, {75, 30, 540, 100}, true, 3};
    snow_set_inverted(&scene, true);
    snow_surfaces(&scene, &target, 1);
    assert(snow_screen_y(&scene, scene.drifts[0].surface.rect.y) == 130);
    assert(snow_screen_y(&scene, snow_screen_y(&scene, 30)) == 30);
    for (int i = 0; i < 720; i++) snow_step(&scene, 1.0f / 30, 2, 0);
    assert(mass(&scene.drifts[0]) > 0);
    uint64_t revision = scene.revision;
    snow_set_inverted(&scene, false);
    snow_surfaces(&scene, &target, 1);
    assert(scene.drifts[0].surface.rect.y == 30);
    assert(scene.revision > revision && scene.flake_count == 0);
    target.corner_radius = 26;
    snow_surfaces(&scene, &target, 1);
    assert(!snow_exposed(&scene, 0, 100));
    assert(snow_exposed(&scene, 0, 101));
    assert(!snow_exposed(&scene, 0, 589));
    target.corner_radius = 0;
    snow_surfaces(&scene, &target, 1);
    assert(snow_exposed(&scene, 0, 75));
    puts("Desktop simulation passed");
}
