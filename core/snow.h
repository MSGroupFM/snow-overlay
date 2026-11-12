#ifndef SNOW_H
#define SNOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SNOW_MAX_SURFACES 129
#define SNOW_MAX_FLAKES 768
#define SNOW_MAX_BALLS 48
#define SNOW_BINS 128
#define SNOW_MAX_SPANS 32
#define SNOW_MAX_DEPTH 24.0f

typedef struct {
    float x, y, width, height;
} SnowRect;

typedef struct {
    uint64_t id;
    SnowRect rect;
    bool catches;
    float corner_radius;
} SnowSurface;

typedef struct {
    float left, right;
} SnowSpan;

typedef struct {
    SnowSurface surface;
    float height[SNOW_BINS];
    SnowSpan spans[SNOW_MAX_SPANS];
    size_t span_count;
    uint64_t revision;
} SnowDrift;

typedef struct {
    float x, y, vx, vy, radius;
    float deposit;
    uint64_t skip;
    bool falling_chunk;
} SnowFlake;

typedef struct {
    uint64_t surface;
    float offset, rise, radius;
    uint8_t appearance;
} SnowBall;

typedef enum {
    SNOWMAN_NONE, SNOWMAN_BASE, SNOWMAN_BODY, SNOWMAN_HEAD
} SnowmanPart;

typedef enum { SNOW_BREAK_NONE, SNOW_BREAK_BALL, SNOW_BREAK_SNOWMAN } SnowBreak;
enum { SNOWMAN_APPEARANCES = 6 };

typedef struct {
    SnowRect screen;
    SnowDrift drifts[SNOW_MAX_SURFACES];
    SnowDrift previous[SNOW_MAX_SURFACES];
    SnowFlake flakes[SNOW_MAX_FLAKES];
    SnowBall balls[SNOW_MAX_BALLS];
    size_t drift_count, flake_count, ball_count, chunks_emitted;
    uint64_t revision;
    uint32_t random;
    float budget, wind, previous_x, previous_y;
    float geometry_elapsed;
    float ball_x, ball_y, ball_radius;
    SnowBall ball_origin;
    SnowBall attached_balls[SNOW_MAX_BALLS - 1];
    size_t attached_count;
    bool pointer_known, holding, moving_ball, inverted;
} SnowScene;

void snow_init(SnowScene *scene, SnowRect screen, uint32_t seed);
void snow_set_inverted(SnowScene *scene, bool enabled);
float snow_screen_y(const SnowScene *scene, float y);
void snow_surfaces(SnowScene *scene, const SnowSurface *surfaces, size_t count);
void snow_step(SnowScene *scene, float dt, int intensity, float wind);
void snow_sweep(SnowScene *scene, float x, float y);
void snow_begin_ball(SnowScene *scene, float x, float y);
bool snow_has_ball_at(const SnowScene *scene, float x, float y);
bool snow_pick_ball(SnowScene *scene, float x, float y);
SnowBreak snow_break_ball(SnowScene *scene, float x, float y);
bool snow_grow_snowman(SnowScene *scene, float near_x, float range, SnowBall *base);
void snow_throw_snowmen(SnowScene *scene, float target_x, float target_y);
void snow_cancel_ball(SnowScene *scene);
void snow_move_ball(SnowScene *scene, float x, float y);
void snow_drop_ball(SnowScene *scene, float x, float y);
void snow_clear(SnowScene *scene);
bool snow_exposed(const SnowScene *scene, size_t index, float x);
bool snow_point_visible(const SnowScene *scene, size_t index, float x, float y);
float snow_depth(const SnowDrift *drift, SnowSpan span, float x);
float snow_edge_inset(SnowSurface surface);
float snow_ball_height(const SnowScene *scene, const SnowBall *ball, float x);
float snow_attached_ball_height(const SnowScene *scene, const SnowBall *ball);
SnowmanPart snow_snowman_part(const SnowScene *scene, const SnowBall *ball);
SnowmanPart snow_snowman_details(const SnowScene *scene, const SnowBall *ball, unsigned *appearance);
bool snow_holds_snowman(const SnowScene *scene);
const SnowDrift *snow_ball_surface(const SnowScene *scene, const SnowBall *ball);

#endif
