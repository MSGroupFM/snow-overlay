#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "snow.h"
#include "cleanup.h"
#include "holiday.h"
#include "settings.h"

typedef NS_ENUM(NSInteger, DesktopSelection) {
    DesktopSelectionUnavailable,
    DesktopSelectionUnchanged,
    DesktopSelectionChanged
};

@interface SnowView : NSView
@property(nonatomic, readonly) SnowScene *scene;
@property(nonatomic, readonly, getter=isOnActiveDesktop) BOOL onActiveDesktop;
@property(nonatomic, copy) void (^leaveInteraction)(void);
@property(nonatomic) NSTimeInterval particleFrameInterval;
@property(nonatomic) BOOL separatesDraggedDrift;
@property(nonatomic) NSTimeInterval mischiefDuration;
- (instancetype)initWithScreen:(SnowRect)screen seed:(uint32_t)seed;
- (void)hideDetachedDrift;
- (void)beginCleanup:(SnowCleanup)style atTime:(NSTimeInterval)time;
- (void)discardCleanup;
- (BOOL)isCleaningAtTime:(NSTimeInterval)time;
- (BOOL)shouldPassClickAt:(NSPoint)point;
- (DesktopSelection)selectDesktopAtSnowfallTime:(NSTimeInterval)time;
- (void)restoreDesktopAccumulationAtSnowfallTime:(NSTimeInterval)time;
- (void)clearDesktopAccumulation;
- (BOOL)startSantaAtTime:(NSTimeInterval)time;
- (void)advanceHolidayAtTime:(NSTimeInterval)time targetX:(CGFloat)x;
- (void)resetHolidayEffects;
- (void)render;
- (void)renderWithDraggedWindow:(uint64_t)window horizontalOffset:(CGFloat)offset;
- (void)renderWithDraggedWindow:(uint64_t)window horizontalOffset:(CGFloat)offset atTime:(NSTimeInterval)time;
@end

@interface SnowPanel : NSPanel
@end

@implementation SnowPanel
- (BOOL)canBecomeKeyWindow { return !self.ignoresMouseEvents; }
@end

static SnowPanel *make_snow_panel(NSRect frame, SnowView *view) {
    SnowPanel *panel = [[SnowPanel alloc] initWithContentRect:frame
                      styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                        backing:NSBackingStoreBuffered defer:YES];
    panel.releasedWhenClosed = NO;
    panel.opaque = NO;
    panel.backgroundColor = NSColor.clearColor;
    panel.hasShadow = NO;
    panel.ignoresMouseEvents = YES;
    panel.hidesOnDeactivate = NO;
    panel.level = CGWindowLevelForKey(kCGDockWindowLevelKey) + 1;
    panel.collectionBehavior = NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorFullScreenNone |
                               NSWindowCollectionBehaviorIgnoresCycle;
    panel.animationBehavior = NSWindowAnimationBehaviorNone;
    panel.contentView = view;
    return panel;
}

@interface DetachedDriftView : NSView
@end

@implementation DetachedDriftView
- (BOOL)isFlipped { return YES; }
@end

typedef struct {
    uint8_t heights[SNOW_BINS];
    SnowSpan spans[SNOW_MAX_SPANS];
    size_t spanCount;
    float width;
    bool inverted;
} DriftShape;

typedef struct {
    uint64_t id;
    float heights[SNOW_BINS];
} SavedDrift;

typedef struct {
    uint64_t surface;
    float offset;
    NSTimeInterval started;
} SnowmanGrowth;

@interface SnowDesktop : NSObject
@property(nonatomic) NSWindow *marker;
@property(nonatomic) NSData *drifts;
@property(nonatomic) NSData *balls;
@property(nonatomic) NSTimeInterval snowfallTime;
@end

@implementation SnowDesktop
@end

static bool update_drift_shape(DriftShape *previous, const SnowDrift *drift, bool inverted) {
    DriftShape current = {.spanCount = drift->span_count, .width = drift->surface.rect.width, .inverted = inverted};
    for (size_t i = 0; i < SNOW_BINS; i++) current.heights[i] = (uint8_t)lroundf(drift->height[i] * 2);
    for (size_t i = 0; i < drift->span_count; i++) {
        current.spans[i] = (SnowSpan){drift->spans[i].left - drift->surface.rect.x,
                                     drift->spans[i].right - drift->surface.rect.x};
    }
    if (previous->spanCount == current.spanCount && previous->width == current.width &&
        previous->inverted == inverted && memcmp(previous->heights, current.heights, sizeof current.heights) == 0 &&
        memcmp(previous->spans, current.spans, current.spanCount * sizeof(SnowSpan)) == 0) return false;
    *previous = current;
    return true;
}

@implementation SnowView {
    CALayer *_flakeGroup;
    CALayer *_driftGroup;
    CALayer *_ballGroup;
    SnowPanel *_driftPanel;
    CAShapeLayer *_detachedDrift;
    NSMutableArray<CALayer *> *_flakes;
    NSMutableArray<CAShapeLayer *> *_drifts;
    NSMutableArray<CALayer *> *_balls;
    CGImageRef _flakeImage;
    CGImageRef _snowmanDetails[2][SNOWMAN_APPEARANCES][2];
    uint64_t _driftRevisions[SNOW_MAX_SURFACES];
    DriftShape _driftShapes[SNOW_MAX_SURFACES];
    uint64_t _draggedWindow, _previousDraggedWindow;
    CGFloat _dragOffset;
    size_t _shownFlakes, _shownDrifts, _shownBalls;
    NSTimeInterval _lastParticleFrame;
    CALayer *_cleanupLayer;
    NSTimeInterval _cleanupUntil;
    NSMutableArray<SnowDesktop *> *_desktops;
    SnowDesktop *_activeDesktop;
    BOOL _restoreDesktop;
    CALayer *_santaFlight;
    NSTimeInterval _santaStarted, _santaDuration, _mischiefUntil, _nextVolley;
    CGFloat _santaMarks[3];
    NSUInteger _nextSantaMark, _growthSlot;
    BOOL _santaGrewSnowman;
    SnowmanGrowth _growth[4];
}

static CGImageRef flake_image(void) {
    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, 20, 20, 8, 0, color,
                                              (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(color);
    if (!ctx) return NULL;
    CGContextSetRGBFillColor(ctx, .94, .98, 1, 1);
    CGContextFillEllipseInRect(ctx, CGRectMake(1, 1, 18, 18));
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return image;
}

- (instancetype)initWithScreen:(SnowRect)screen seed:(uint32_t)seed {
    self = [super initWithFrame:NSMakeRect(0, 0, screen.width, screen.height)];
    if (self) {
        _scene = calloc(1, sizeof *_scene);
        if (!_scene) return nil;
        snow_init(_scene, screen, seed);
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        CALayer *root = [CALayer layer];
        root.masksToBounds = YES;
        self.layer = root;
        self.wantsLayer = YES;
        _flakeGroup = [CALayer layer];
        _driftGroup = [CALayer layer];
        _ballGroup = [CALayer layer];
        for (CALayer *group in @[_flakeGroup, _driftGroup, _ballGroup]) {
            group.frame = self.bounds;
            [root addSublayer:group];
        }
        _flakes = [NSMutableArray array];
        _drifts = [NSMutableArray array];
        _balls = [NSMutableArray array];
        _desktops = [NSMutableArray array];
        _flakeImage = flake_image();
    }
    return self;
}
- (void)dealloc {
    for (SnowDesktop *desktop in _desktops) [desktop.marker close];
    if (_flakeImage) CGImageRelease(_flakeImage);
    for (size_t part = 0; part < 2; part++) {
        for (size_t style = 0; style < SNOWMAN_APPEARANCES; style++) {
            for (size_t mood = 0; mood < 2; mood++) {
                if (_snowmanDetails[part][style][mood]) CGImageRelease(_snowmanDetails[part][style][mood]);
            }
        }
    }
    free(_scene);
}
- (void)viewWillMoveToWindow:(NSWindow *)newWindow {
    if (self.window != newWindow) {
        [self hideDetachedDrift];
        [_driftPanel close];
        _driftPanel = nil;
        _detachedDrift = nil;
    }
    [super viewWillMoveToWindow:newWindow];
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }

- (NSPoint)scenePoint:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    return NSMakePoint(point.x + _scene->screen.x, snow_screen_y(_scene, point.y + _scene->screen.y));
}
- (void)mouseDown:(NSEvent *)event {
    if ([self isCleaningAtTime:CACurrentMediaTime()]) return;
    NSPoint p = [self scenePoint:event];
    if (![self canStartBallAt:p]) {
        if (self.leaveInteraction) self.leaveInteraction();
        return;
    }
    [self.window makeKeyWindow];
    [self.window makeFirstResponder:self];
    if (!snow_pick_ball(_scene, p.x, p.y)) snow_begin_ball(_scene, p.x, p.y);
}
- (void)rightMouseDown:(NSEvent *)event {
    if ([self isCleaningAtTime:CACurrentMediaTime()]) return;
    NSPoint p = [self scenePoint:event];
    SnowBreak result = snow_break_ball(_scene, p.x, p.y);
    if (result == SNOW_BREAK_SNOWMAN) [self startMischiefAtTime:CACurrentMediaTime() nearX:p.x];
    if (result != SNOW_BREAK_NONE) return;
    if (![self canStartBallAt:p] && self.leaveInteraction) self.leaveInteraction();
}
- (BOOL)canStartBallAt:(NSPoint)point {
    if (snow_has_ball_at(_scene, point.x, point.y)) return YES;
    for (size_t i = 0; i < _scene->drift_count; i++) {
        SnowDrift *drift = &_scene->drifts[i];
        SnowRect rect = drift->surface.rect;
        if (drift->surface.id && CGRectContainsPoint(CGRectMake(rect.x, rect.y, rect.width, rect.height), point)) return NO;
    }
    return YES;
}
- (BOOL)shouldPassClickAt:(NSPoint)point {
    if (_scene->holding) return NO;
    if (!self.window.isVisible || !self.window.isOnActiveSpace || !NSPointInRect(point, self.window.frame)) return YES;
    NSPoint local = [self convertPoint:[self.window convertPointFromScreen:point] fromView:nil];
    NSPoint scenePoint = NSMakePoint(local.x + _scene->screen.x, snow_screen_y(_scene, local.y + _scene->screen.y));
    return ![self canStartBallAt:scenePoint];
}
- (void)mouseMoved:(NSEvent *)event {
    if (self.window.acceptsMouseMovedEvents && !self.window.ignoresMouseEvents &&
        [self shouldPassClickAt:[self.window convertPointToScreen:event.locationInWindow]]) {
        self.window.ignoresMouseEvents = YES;
    }
}
- (void)mouseDragged:(NSEvent *)event {
    NSPoint p = [self scenePoint:event];
    snow_move_ball(_scene, p.x, p.y);
}
- (void)mouseUp:(NSEvent *)event {
    NSPoint p = [self scenePoint:event];
    snow_drop_ball(_scene, p.x, p.y);
}
- (void)cancelOperation:(id)sender {
    if (self.leaveInteraction) self.leaveInteraction();
}

- (CGPoint)layerPointWithX:(float)x y:(float)y {
    SnowRect screen = _scene->screen;
    return CGPointMake(x - screen.x, snow_screen_y(_scene, y) - screen.y);
}

- (void)renderFlakes {
    for (size_t i = 0; i < _scene->flake_count; i++) {
        SnowFlake flake = _scene->flakes[i];
        if (i >= _flakes.count) {
            CALayer *layer = [CALayer layer];
            layer.contents = (__bridge id)_flakeImage;
            [_flakeGroup addSublayer:layer];
            [_flakes addObject:layer];
        }
        CALayer *layer = _flakes[i];
        CGFloat size = flake.radius * 2;
        if (layer.bounds.size.width != size) {
            layer.bounds = CGRectMake(0, 0, size, size);
            layer.opacity = MIN(.96f, .39f + flake.radius * .13f);
        }
        layer.position = [self layerPointWithX:flake.x y:flake.y];
        if (layer.hidden) layer.hidden = NO;
    }
    for (size_t i = _scene->flake_count; i < _shownFlakes; i++) _flakes[i].hidden = YES;
    _shownFlakes = _scene->flake_count;
}

- (CGFloat)offsetForDrift:(const SnowDrift *)drift {
    if (!_draggedWindow || drift->surface.id != _draggedWindow || drift->span_count != 1 ||
        !_dragOffset) return 0;
    SnowRect rect = drift->surface.rect, screen = _scene->screen;
    SnowSpan span = drift->spans[0];
    float inset = snow_edge_inset(drift->surface);
    if (span.left != rect.x + inset || span.right != rect.x + rect.width - inset) return 0;
    float left = span.left + _dragOffset, right = span.right + _dragOffset;
    if (left < screen.x || right > screen.x + screen.width) return 0;
    for (const SnowDrift *front = _scene->drifts; front < drift; front++) {
        SnowRect mask = front->surface.rect;
        if (left < mask.x + mask.width && right > mask.x &&
            rect.y >= mask.y && rect.y - SNOW_MAX_DEPTH < mask.y + mask.height) return 0;
    }
    return _dragOffset;
}

- (void)hideDetachedDrift {
    if (_driftPanel.parentWindow) [_driftPanel.parentWindow removeChildWindow:_driftPanel];
    if (_driftPanel.isVisible) [_driftPanel orderOut:nil];
}

- (NSWindow *)createDesktopMarker {
    NSRect screen = self.window.frame;
    SnowPanel *marker = [[SnowPanel alloc] initWithContentRect:NSMakeRect(NSMidX(screen), NSMidY(screen), 2, 2)
                         styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                           backing:NSBackingStoreBuffered defer:NO];
    marker.releasedWhenClosed = NO;
    marker.collectionBehavior = NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorIgnoresCycle |
                                NSWindowCollectionBehaviorFullScreenNone;
    marker.excludedFromWindowsMenu = YES;
    marker.opaque = NO;
    marker.backgroundColor = NSColor.clearColor;
    marker.hasShadow = NO;
    marker.hidesOnDeactivate = NO;
    marker.ignoresMouseEvents = YES;
    marker.animationBehavior = NSWindowAnimationBehaviorNone;
    [marker orderWindow:NSWindowBelow relativeTo:0];
    return marker;
}

- (void)saveDesktopAtSnowfallTime:(NSTimeInterval)time {
    if (!_activeDesktop) return;
    SavedDrift banks[SNOW_MAX_SURFACES];
    for (size_t i = 0; i < _scene->drift_count; i++) {
        banks[i].id = _scene->drifts[i].surface.id;
        memcpy(banks[i].heights, _scene->drifts[i].height, sizeof banks[i].heights);
    }
    snow_cancel_ball(_scene);
    _activeDesktop.drifts = [NSData dataWithBytes:banks length:_scene->drift_count * sizeof *banks];
    _activeDesktop.balls = [NSData dataWithBytes:_scene->balls length:_scene->ball_count * sizeof(SnowBall)];
    _activeDesktop.snowfallTime = time;
}

- (BOOL)isOnActiveDesktop {
    return _activeDesktop.marker.isOnActiveSpace;
}

- (DesktopSelection)selectDesktopAtSnowfallTime:(NSTimeInterval)time {
    SnowDesktop *selected = nil;
    for (SnowDesktop *desktop in _desktops.reverseObjectEnumerator) {
        if (desktop.marker.isOnActiveSpace) {
            selected = desktop;
            break;
        }
    }
    if (selected && selected == _activeDesktop) return DesktopSelectionUnchanged;
    if (!selected) {
        NSWindow *marker = [self createDesktopMarker];
        if (!marker.isOnActiveSpace) {
            [marker close];
            return DesktopSelectionUnavailable;
        }
        selected = [SnowDesktop new];
        selected.marker = marker;
        [_desktops addObject:selected];
    }
    BOOL firstDesktop = !_activeDesktop && _desktops.count == 1;
    [self saveDesktopAtSnowfallTime:time];
    _activeDesktop = selected;
    // Windows from removed desktops can migrate to the selected desktop.
    for (SnowDesktop *desktop in _desktops.copy) {
        if (desktop != selected && desktop.marker.isOnActiveSpace) {
            [desktop.marker close];
            [_desktops removeObjectIdenticalTo:desktop];
        }
    }
    [_desktops removeObjectIdenticalTo:selected];
    [_desktops addObject:selected];
    if (!firstDesktop) {
        [self discardCleanup];
        [self hideDetachedDrift];
        SnowFlake airborne[SNOW_MAX_FLAKES];
        size_t count = 0;
        float budget = _scene->budget, wind = _scene->wind;
        for (size_t i = 0; i < _scene->flake_count; i++) {
            if (_scene->flakes[i].deposit <= 0) airborne[count++] = _scene->flakes[i];
        }
        [self resetSnowScene];
        memcpy(_scene->flakes, airborne, count * sizeof *airborne);
        _scene->flake_count = count;
        _scene->budget = budget;
        _scene->wind = wind;
        _restoreDesktop = YES;
    }
    return firstDesktop ? DesktopSelectionUnchanged : DesktopSelectionChanged;
}

- (void)restoreDesktopAccumulationAtSnowfallTime:(NSTimeInterval)time {
    if (!_restoreDesktop) return;
    _restoreDesktop = NO;
    const SavedDrift *banks = _activeDesktop.drifts.bytes;
    size_t count = _activeDesktop.drifts.length / sizeof *banks;
    double inactive = MAX(0, time - _activeDesktop.snowfallTime);
    // Estimate settled snow in one pass. Inactive desktops never step particles.
    float fill = 1 - exp(-inactive / 220);
    for (size_t i = 0; i < _scene->drift_count; i++) {
        SnowDrift *drift = &_scene->drifts[i];
        for (size_t j = 0; j < count; j++) {
            if (banks[j].id != drift->surface.id) continue;
            memcpy(drift->height, banks[j].heights, sizeof drift->height);
            break;
        }
        if (fill > 0 && drift->span_count) {
            float offset = (drift->surface.id % 31) * .2f;
            for (size_t j = 0; j < SNOW_BINS; j++) {
                float profile = 12 + 2.5f * sinf(j * .12f + offset) + .9f * sinf(j * .38f);
                drift->height[j] += fmaxf(0, profile - drift->height[j]) * fill;
            }
        }
        drift->revision = ++_scene->revision;
    }
    const SnowBall *balls = _activeDesktop.balls.bytes;
    size_t ballCount = _activeDesktop.balls.length / sizeof *balls;
    for (size_t i = 0; i < ballCount; i++) {
        if (snow_ball_surface(_scene, &balls[i])) _scene->balls[_scene->ball_count++] = balls[i];
    }
    _activeDesktop.drifts = nil;
    _activeDesktop.balls = nil;
}

- (void)clearDesktopAccumulation {
    for (SnowDesktop *desktop in _desktops) {
        desktop.drifts = nil;
        desktop.balls = nil;
        desktop.snowfallTime = 0;
    }
    _restoreDesktop = NO;
}

- (void)discardCleanup {
    [_cleanupLayer removeFromSuperlayer];
    _cleanupLayer = nil;
    _cleanupUntil = 0;
}

- (BOOL)isCleaningAtTime:(NSTimeInterval)time {
    if (!_cleanupLayer) return NO;
    if (time < _cleanupUntil) return YES;
    [self discardCleanup];
    return NO;
}

- (void)resetSnowScene {
    [self resetHolidayEffects];
    uint64_t revision = _scene->revision;
    BOOL inverted = _scene->inverted;
    snow_init(_scene, _scene->screen, arc4random());
    _scene->inverted = inverted;
    _scene->drifts[0].revision = _scene->revision = revision + 1;
    memset(_driftShapes, 0, sizeof _driftShapes);
    _lastParticleFrame = 0;
}

- (BOOL)startSantaAtTime:(NSTimeInterval)time {
    if (_santaFlight || !self.window.isVisible || !self.window.isOnActiveSpace || [self isCleaningAtTime:time]) return NO;
    _santaStarted = time;
    _santaDuration = holiday_flight_duration(NSWidth(self.bounds));
    _nextSantaMark = 0;
    _santaGrewSnowman = NO;
    for (NSUInteger i = 0; i < 3; i++) {
        _santaMarks[i] = .31 + i * .20 + arc4random_uniform(1000) * .00008;
    }
    _santaFlight = holiday_flight_layer(_scene, self.bounds.size, _santaDuration,
                                       NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion);
    [self.layer addSublayer:_santaFlight];
    return YES;
}

- (BOOL)growSnowmanAtTime:(NSTimeInterval)time nearX:(CGFloat)x range:(CGFloat)range {
    SnowBall base;
    if (!snow_grow_snowman(_scene, x, range, &base)) return NO;
    if (!NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion) {
        _growth[_growthSlot++ % 4] = (SnowmanGrowth){base.surface, base.offset, time};
    }
    return YES;
}

- (void)startMischiefAtTime:(NSTimeInterval)time nearX:(CGFloat)x {
    if (self.mischiefDuration <= 0 || time < _mischiefUntil) return;
    _mischiefUntil = time + self.mischiefDuration;
    _nextVolley = time + .45;
    BOOL hasSnowman = NO;
    for (size_t i = 0; i < _scene->ball_count && !hasSnowman; i++) {
        hasSnowman = snow_snowman_part(_scene, &_scene->balls[i]) == SNOWMAN_HEAD;
    }
    if (!hasSnowman) [self growSnowmanAtTime:time nearX:x range:fminf(360, _scene->screen.width * .27f)];
}

- (void)advanceHolidayAtTime:(NSTimeInterval)time targetX:(CGFloat)x {
    if (_santaFlight) {
        CGFloat progress = (time - _santaStarted) / _santaDuration;
        while (_nextSantaMark < 3 && progress >= _santaMarks[_nextSantaMark]) {
            CGPoint p = holiday_flight_position(self.bounds.size, _santaMarks[_nextSantaMark++], _scene->inverted);
            CGFloat worldX = _scene->screen.x + fmax(0, fmin(_scene->screen.width, p.x - 86));
            CGFloat range = fmin(360, _scene->screen.width * .27);
            if (_nextSantaMark == 3 && !_santaGrewSnowman) range = _scene->screen.width;
            if ([self growSnowmanAtTime:time nearX:worldX range:range]) _santaGrewSnowman = YES;
        }
        if (progress >= 1) {
            [_santaFlight removeFromSuperlayer];
            _santaFlight = nil;
        }
    }
    if (time >= _mischiefUntil || self.mischiefDuration <= 0) {
        _mischiefUntil = 0;
    } else if (time >= _nextVolley) {
        snow_throw_snowmen(_scene, x);
        _nextVolley = time + .85;
    }
    for (NSUInteger i = 0; i < 4; i++) {
        if (_growth[i].started && time - _growth[i].started >= 1.15) _growth[i].started = 0;
    }
}

- (void)resetHolidayEffects {
    [_santaFlight removeFromSuperlayer];
    _santaFlight = nil;
    _mischiefUntil = _nextVolley = 0;
    memset(_growth, 0, sizeof _growth);
}

- (void)beginCleanup:(SnowCleanup)style atTime:(NSTimeInterval)time {
    [self discardCleanup];
    [self render]; // Transfer any detached drift before taking the visual snapshot.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (style != SnowCleanupImmediate && self.window.isVisible && self.window.isOnActiveSpace) {
        CGFloat width = NSWidth(self.bounds), height = NSHeight(self.bounds);
        BOOL groundPass = style == SnowCleanupPlow || style == SnowCleanupShovel;
        CFTimeInterval duration = 1.55;
        if (style == SnowCleanupPlow) duration = MAX(3.2, MIN(5.4, 1.3 + width / 700));
        if (style == SnowCleanupShovel) duration = MAX(3.8, MIN(6.0, 1.4 + width / 560));
        _cleanupUntil = time + duration;
        _cleanupLayer = [CALayer layer];
        _cleanupLayer.frame = self.bounds;
        [self.layer addSublayer:_cleanupLayer];
        CALayer *airborne = _flakeGroup;
        airborne.allowsGroupOpacity = NO;
        [airborne removeFromSuperlayer];
        [_cleanupLayer addSublayer:airborne];
        _flakeGroup = [CALayer layer];
        _flakeGroup.frame = self.bounds;
        [self.layer insertSublayer:_flakeGroup atIndex:0];
        [_flakes removeAllObjects];
        _shownFlakes = 0;
        CALayer *banks = [CALayer layer];
        banks.name = @"cleanup-banks";
        banks.frame = self.bounds;
        [_cleanupLayer addSublayer:banks];
        CALayer *particles = [CALayer layer];
        particles.name = @"cleanup-particles";
        if (!groundPass) [_cleanupLayer addSublayer:particles];
        CGFloat ground = _scene->inverted ? 1 : height - 1;
        CGFloat direction = _scene->inverted ? -1 : 1;
        CGFloat baseline = _scene->inverted ? 0 : SNOW_MAX_DEPTH;
        CGPoint air = airborne.position;
        CGFloat blow = style == SnowCleanupWind ? width : 0;
        animate_cleanup(airborne, @[[NSValue valueWithPoint:air],
                                    [NSValue valueWithPoint:CGPointMake(air.x + blow, air.y + direction * 58)],
                                    [NSValue valueWithPoint:CGPointMake(air.x + blow, air.y + direction * 58)]],
                        @[@1, @0, @0], @[@0, @.62, @1], duration);
        for (size_t i = 0; i < _shownDrifts; i++) {
            CAShapeLayer *old = _drifts[i];
            if (old.hidden || !old.path) continue;
            const SnowDrift *drift = &_scene->drifts[i];
            CAShapeLayer *bank = [CAShapeLayer layer];
            bank.frame = old.frame;
            bank.path = old.path;
            bank.fillColor = old.fillColor;
            bank.contentsScale = old.contentsScale;
            [banks addSublayer:bank];
            CGPoint origin = bank.position;
            CGFloat fall = ground - (CGRectGetMinY(old.frame) + baseline);
            CGFloat edge = CGRectGetMinY(old.frame) + baseline;
            NSArray<NSValue *> *positions;
            NSArray<NSNumber *> *opacity = @[@1, @1, @1, @0];
            NSArray<NSNumber *> *times = @[@0, @.16, @.53, @1];
            if (style == SnowCleanupWind) {
                cleanup_wind_erosion(bank, _scene, drift, baseline, direction, duration);
                for (size_t j = 0; j < drift->span_count; j++) {
                    SnowSpan span = drift->spans[j];
                    float depth = snow_depth(drift, span, (span.left + span.right) * .5f);
                    if (depth < .35f) continue;
                    CGRect source = CGRectMake(span.left - _scene->screen.x, 0, span.right - span.left, 0);
                    cleanup_drift_particles(particles, style, source, edge - direction * depth, fall,
                                            direction, width, duration);
                }
                continue;
            } else if (style == SnowCleanupThaw) {
                bank.anchorPoint = CGPointMake(.5, _scene->inverted ? 0 : 1);
                bank.position = CGPointMake(origin.x, edge);
                [bank addAnimation:cleanup_keyframes(@"transform.scale.y", @[@1, @.80, @.35, @.04], times, duration)
                            forKey:@"thaw-depth"];
                [bank addAnimation:cleanup_keyframes(@"opacity", @[@1, @1, @.63, @0], times, duration)
                            forKey:@"thaw-opacity"];
                cleanup_drift_particles(particles, style, old.frame, edge, fall, direction, width, duration);
                continue;
            } else {
                CGFloat jump = style == SnowCleanupShovel ? 34 * direction : 0;
                CGFloat sway = style == SnowCleanupAvalanche ? direction * 58 : 0;
                positions = @[[NSValue valueWithPoint:origin],
                              [NSValue valueWithPoint:CGPointMake(origin.x, origin.y - jump)],
                              [NSValue valueWithPoint:CGPointMake(origin.x + sway, origin.y + fall)],
                              [NSValue valueWithPoint:CGPointMake(origin.x + sway, origin.y + fall)]];
                if (style == SnowCleanupPlow || style == SnowCleanupShovel) {
                    times = @[@0, @.13, @.37, @1];
                    opacity = @[@1, @1, @1, @1];
                } else {
                    CGFloat lag = .28 * MIN(1, fabs(fall) / MAX(1, height));
                    times = @[@0, @(.04 + lag), @(.56 + lag), @1];
                    cleanup_drift_particles(particles, style, old.frame, edge - direction * 5, fall,
                                            direction, width, duration);
                }
            }
            animate_cleanup(bank, positions, opacity, times, duration);
        }
        if (groundPass) {
            CGFloat profile[CLEANUP_SNOW_SAMPLES] = {0};
            cleanup_snow_profile(_scene, profile);
            cleanup_ground_pass(_cleanupLayer, banks, style, profile, width, ground, direction, duration);
            if (style == SnowCleanupShovel) cleanup_ice_impact(_cleanupLayer, width * .65, ground, direction, duration);
        }
    }
    [self resetSnowScene];
    [self render];
    [CATransaction commit];
}

- (BOOL)showDetachedDrift:(CAShapeLayer *)source frame:(CGRect)frame {
    NSWindow *parent = self.window;
    if (!parent.isVisible) return NO;

    // SnowView uses a top-left origin. AppKit window frames use a bottom-left origin.
    CGRect visible = CGRectIntersection(CGRectIntegral(frame), self.bounds);
    if (CGRectIsEmpty(visible)) return NO;
    CGRect parentFrame = parent.frame;
    CGRect windowFrame = CGRectMake(CGRectGetMinX(parentFrame) + CGRectGetMinX(visible),
                                    CGRectGetMaxY(parentFrame) - CGRectGetMaxY(visible),
                                    CGRectGetWidth(visible), CGRectGetHeight(visible));
    if (!_driftPanel) {
        _driftPanel = [[SnowPanel alloc] initWithContentRect:windowFrame
                           styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                             backing:NSBackingStoreBuffered defer:NO];
        if (!_driftPanel) return NO;
        _driftPanel.releasedWhenClosed = NO;
        _driftPanel.opaque = NO;
        _driftPanel.backgroundColor = NSColor.clearColor;
        _driftPanel.hasShadow = NO;
        _driftPanel.ignoresMouseEvents = YES;
        _driftPanel.hidesOnDeactivate = NO;
        _driftPanel.animationBehavior = NSWindowAnimationBehaviorNone;
        _driftPanel.level = parent.level;
        _driftPanel.collectionBehavior = parent.collectionBehavior;
        _driftPanel.excludedFromWindowsMenu = YES;

        NSView *content = [[DetachedDriftView alloc] initWithFrame:NSMakeRect(0, 0, windowFrame.size.width,
                                                                              windowFrame.size.height)];
        content.layer = [CALayer layer];
        content.layer.masksToBounds = YES;
        content.wantsLayer = YES;
        _driftPanel.contentView = content;
        _detachedDrift = [CAShapeLayer layer];
        _detachedDrift.fillColor = source.fillColor;
        _detachedDrift.contentsScale = source.contentsScale;
        [content.layer addSublayer:_detachedDrift];
    }
    if (!CGSizeEqualToSize(_driftPanel.frame.size, windowFrame.size)) {
        [_driftPanel setFrame:windowFrame display:NO];
    } else if (!CGPointEqualToPoint(_driftPanel.frame.origin, windowFrame.origin)) {
        [_driftPanel setFrameOrigin:windowFrame.origin];
    }

    CGRect shapeFrame = CGRectMake(CGRectGetMinX(frame) - CGRectGetMinX(visible),
                                   CGRectGetMinY(frame) - CGRectGetMinY(visible),
                                   CGRectGetWidth(frame), CGRectGetHeight(frame));
    if (!CGRectEqualToRect(_detachedDrift.frame, shapeFrame)) _detachedDrift.frame = shapeFrame;
    if (_detachedDrift.path != source.path) _detachedDrift.path = source.path;
    if (_driftPanel.parentWindow != parent) [parent addChildWindow:_driftPanel ordered:NSWindowAbove];
    if (!_driftPanel.isVisible) [_driftPanel orderFrontRegardless];
    return YES;
}

- (void)renderDrifts {
    BOOL detached = NO;
    for (size_t i = 0; i < _scene->drift_count; i++) {
        const SnowDrift *drift = &_scene->drifts[i];
        if (i >= _drifts.count) {
            CAShapeLayer *layer = [CAShapeLayer layer];
            layer.fillColor = [NSColor colorWithRed:.93 green:.97 blue:1 alpha:.94].CGColor;
            layer.contentsScale = MAX(1, self.window.backingScaleFactor);
            [_driftGroup addSublayer:layer];
            [_drifts addObject:layer];
        }
        CAShapeLayer *layer = _drifts[i];
        BOOL changed = _driftRevisions[i] != drift->revision;
        uint64_t id = drift->surface.id;
        if (!changed && (!id || (id != _draggedWindow && id != _previousDraggedWindow))) continue;
        _driftRevisions[i] = drift->revision;
        if (!drift->span_count) {
            _driftShapes[i].spanCount = 0;
            layer.path = NULL;
            layer.hidden = YES;
            continue;
        }

        float origin = drift->spans[0].left;
        float width = drift->spans[drift->span_count - 1].right - origin;
        float baseline = _scene->inverted ? 0 : SNOW_MAX_DEPTH;
        if (changed && update_drift_shape(&_driftShapes[i], drift, _scene->inverted)) {
            SnowDrift displayed = *drift;
            for (size_t bin = 0; bin < SNOW_BINS; bin++) displayed.height[bin] = _driftShapes[i].heights[bin] * .5f;
            float direction = _scene->inverted ? 1 : -1;
            float spacing = fminf(7, fmaxf(2, drift->surface.rect.width / SNOW_BINS * .55f));
            CGMutablePathRef path = CGPathCreateMutable();
            bool hasSnow = false;
            for (size_t j = 0; j < drift->span_count; j++) {
                SnowSpan span = drift->spans[j];
                CGPathMoveToPoint(path, NULL, span.left - origin, baseline);
                for (float x = span.left + spacing; x < span.right; x += spacing) {
                    float depth = snow_depth(&displayed, span, x);
                    if (depth > .08f) hasSnow = true;
                    CGPathAddLineToPoint(path, NULL, x - origin, baseline + direction * depth);
                }
                CGPathAddLineToPoint(path, NULL, span.right - origin, baseline);
                CGPathCloseSubpath(path);
            }
            layer.path = hasSnow ? path : NULL;
            CGPathRelease(path);
        }
        BOOL shownInPanel = NO;
        if (layer.path) {
            CGPoint edge = [self layerPointWithX:origin y:drift->surface.rect.y];
            CGFloat offset = [self offsetForDrift:drift];
            CGRect frame = CGRectMake(edge.x + offset, edge.y - baseline, width, SNOW_MAX_DEPTH);
            if (self.separatesDraggedDrift && id && id == _draggedWindow) {
                shownInPanel = [self showDetachedDrift:layer frame:frame];
                detached |= shownInPanel;
            }
            if (!shownInPanel && !CGRectEqualToRect(layer.frame, frame)) layer.frame = frame;
        }
        BOOL hidden = !layer.path || shownInPanel;
        if (layer.hidden != hidden) layer.hidden = hidden;
    }
    for (size_t i = _scene->drift_count; i < _shownDrifts; i++) {
        _drifts[i].hidden = YES;
        _driftRevisions[i] = 0;
        _driftShapes[i].spanCount = 0;
    }
    _shownDrifts = _scene->drift_count;
    if (!detached) [self hideDetachedDrift];
}

- (void)drawBallAtIndex:(size_t)index x:(float)x y:(float)y radius:(float)radius part:(SnowmanPart)part
             appearance:(unsigned)appearance mischievous:(BOOL)mischievous opacity:(CGFloat)opacity {
    if (index >= _balls.count) {
        CALayer *ball = [CALayer layer];
        ball.backgroundColor = [NSColor colorWithRed:.74 green:.85 blue:.95 alpha:1].CGColor;
        CALayer *light = [CALayer layer];
        light.backgroundColor = [NSColor colorWithRed:.97 green:.99 blue:1 alpha:1].CGColor;
        [ball addSublayer:light];
        [_ballGroup addSublayer:ball];
        [_balls addObject:ball];
    }
    CALayer *ball = _balls[index];
    if (ball.opacity != (float)opacity) ball.opacity = opacity;
    CALayer *light = ball.sublayers.firstObject;
    CGPoint position = [self layerPointWithX:x y:y];
    if (!CGPointEqualToPoint(ball.position, position)) ball.position = position;
    if (ball.cornerRadius != radius) {
        ball.bounds = CGRectMake(0, 0, radius * 2, radius * 2);
        ball.cornerRadius = radius;
        light.frame = CGRectMake(.09f * radius, .13f * radius, 1.56f * radius, 1.56f * radius);
        light.cornerRadius = .78f * radius;
    }
    CALayer *details = ball.sublayers.count > 1 ? ball.sublayers.lastObject : nil;
    if (part == SNOWMAN_HEAD || part == SNOWMAN_BODY) {
        if (!details) {
            details = [CALayer layer];
            [ball addSublayer:details];
        }
        appearance %= SNOWMAN_APPEARANCES;
        CGImageRef *image = &_snowmanDetails[part == SNOWMAN_HEAD][appearance][mischievous ? 1 : 0];
        if (!*image) *image = snowman_details(part, appearance, mischievous);
        if (details.contents != (__bridge id)*image) details.contents = (__bridge id)*image;
        CGRect bounds = CGRectMake(0, 0, 4 * radius, 4 * radius);
        if (!CGRectEqualToRect(details.bounds, bounds)) {
            details.bounds = bounds;
            details.position = CGPointMake(radius, radius);
        }
        CGFloat direction = _scene->inverted ? -1 : 1;
        if (details.affineTransform.d != direction) details.affineTransform = CGAffineTransformMakeScale(1, direction);
        if (details.hidden) details.hidden = NO;
    } else if (details && !details.hidden) {
        details.hidden = YES;
    }
    if (ball.hidden) ball.hidden = NO;
}

- (void)render {
    [self renderWithDraggedWindow:0 horizontalOffset:0];
}

- (void)renderWithDraggedWindow:(uint64_t)window horizontalOffset:(CGFloat)offset {
    [self renderWithDraggedWindow:window horizontalOffset:offset atTime:0];
}

- (void)renderWithDraggedWindow:(uint64_t)window horizontalOffset:(CGFloat)offset atTime:(NSTimeInterval)time {
    _draggedWindow = window;
    _dragOffset = offset;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (!time || !_lastParticleFrame || time - _lastParticleFrame >= self.particleFrameInterval - .003) {
        [self renderFlakes];
        _lastParticleFrame = time;
    }
    [self renderDrifts];
    size_t shown = 0;
    NSTimeInterval renderTime = time ?: CACurrentMediaTime();
    BOOL mischievous = renderTime < _mischiefUntil;
    for (size_t i = 0; i < _scene->ball_count; i++) {
        SnowBall *ball = &_scene->balls[i];
        const SnowDrift *drift = snow_ball_surface(_scene, ball);
        if (!drift) continue;
        float x = drift->surface.rect.x + ball->offset * drift->surface.rect.width;
        float y = snow_ball_height(_scene, ball, x);
        size_t surface = (size_t)(drift - _scene->drifts);
        if (snow_exposed(_scene, surface, x) && snow_point_visible(_scene, surface, x, y)) {
            unsigned appearance = 0;
            SnowmanPart part = snow_snowman_details(_scene, ball, &appearance);
            CGFloat opacity = 1;
            for (NSUInteger j = 0; part != SNOWMAN_NONE && j < 4; j++) {
                SnowmanGrowth growth = _growth[j];
                if (!growth.started || growth.surface != ball->surface || growth.offset != ball->offset) continue;
                double progress = (renderTime - growth.started - ball->rise * .004) / .84;
                progress = fmax(0, fmin(1, progress));
                y += (ball->rise + ball->radius * 1.4f) * (1 - progress) * (1 - progress);
                opacity = fmin(1, progress * 3);
                break;
            }
            if (mischievous && part != SNOWMAN_NONE) x += sin(renderTime * 16 + ball->offset * 10) * 1.6;
            [self drawBallAtIndex:shown++ x:x + [self offsetForDrift:drift] y:y radius:ball->radius part:part
                       appearance:appearance mischievous:mischievous opacity:opacity];
        }
    }
    if (_scene->holding) {
        [self drawBallAtIndex:shown++ x:_scene->ball_x y:_scene->ball_y radius:_scene->ball_radius part:SNOWMAN_NONE
                   appearance:0 mischievous:NO opacity:1];
        BOOL snowman = snow_holds_snowman(_scene);
        unsigned appearance = snowman ? _scene->attached_balls[1].appearance : 0;
        for (size_t i = 0; i < _scene->attached_count; i++) {
            const SnowBall *ball = &_scene->attached_balls[i];
            SnowmanPart part = snowman ? (i == 0 ? SNOWMAN_BODY : SNOWMAN_HEAD) : SNOWMAN_NONE;
            [self drawBallAtIndex:shown++ x:_scene->ball_x y:snow_attached_ball_height(_scene, ball)
                           radius:ball->radius part:part appearance:appearance mischievous:mischievous opacity:1];
        }
    }
    for (size_t i = shown; i < _shownBalls; i++) _balls[i].hidden = YES;
    _shownBalls = shown;
    _previousDraggedWindow = _draggedWindow;
    [CATransaction commit];
}

@end

static BOOL overlaps(SnowRect a, SnowRect b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
           a.y < b.y + b.height && a.y + a.height > b.y;
}

static BOOL coversScreen(SnowRect rect, SnowRect screen) {
    return rect.x <= screen.x + 2 && rect.y <= screen.y + 2 &&
           rect.x + rect.width >= screen.x + screen.width - 2 &&
           rect.y + rect.height >= screen.y + screen.height - 2;
}

static NSArray<NSDictionary *> *copy_window_geometry(void) {
    return CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
}

static float windowCornerInset(void) {
    // Window Services reports bounds. Use a conservative inset for each AppKit style generation.
    if (@available(macOS 26.0, *)) return 26;
    return 12;
}

typedef struct {
    uint64_t window;
    CGRect initialFrame, frame;
    CGPoint initialPointer, pointer;
    unsigned stationarySamples;
    bool confirmed;
} WindowDrag;

static const CGFloat maxDragOffset = 64;

static CGFloat drag_offset_x(WindowDrag drag, CGFloat pointerX) {
    if (!drag.confirmed) return 0;
    CGFloat offset = pointerX - drag.initialPointer.x + drag.initialFrame.origin.x - drag.frame.origin.x;
    return fabs(offset) <= maxDragOffset ? offset : 0;
}

static CGPoint drag_pointer_for_frame(WindowDrag drag, CGRect frame, CGPoint from, CGPoint to) {
    // A background snapshot can represent any point between request and delivery.
    CGFloat dx = to.x - from.x, dy = to.y - from.y;
    CGFloat lengthSquared = dx * dx + dy * dy;
    if (!lengthSquared) return from;
    CGFloat expectedX = drag.initialPointer.x + frame.origin.x - drag.initialFrame.origin.x;
    CGFloat expectedY = drag.initialPointer.y + frame.origin.y - drag.initialFrame.origin.y;
    CGFloat fraction = ((expectedX - from.x) * dx + (expectedY - from.y) * dy) / lengthSquared;
    fraction = fmax(0, fmin(1, fraction));
    return CGPointMake(from.x + dx * fraction, from.y + dy * fraction);
}

static void observe_drag(WindowDrag *drag, CGRect frame, CGPoint pointer) {
    if (!CGSizeEqualToSize(drag->initialFrame.size, frame.size)) {
        *drag = (WindowDrag){0};
        return;
    }

    CGFloat px = pointer.x - drag->initialPointer.x, py = pointer.y - drag->initialPointer.y;
    CGFloat wx = frame.origin.x - drag->initialFrame.origin.x, wy = frame.origin.y - drag->initialFrame.origin.y;
    CGFloat distance = hypot(px, py), error = hypot(px - wx, py - wy);
    if (drag->confirmed && error > maxDragOffset) {
        *drag = (WindowDrag){0};
        return;
    }
    if (hypot(wx, wy) >= 3 && px * wx + py * wy > 0 && error <= fmin(maxDragOffset, 10 + distance * .55)) {
        drag->confirmed = true;
    }

    if (!CGPointEqualToPoint(frame.origin, drag->frame.origin)) {
        drag->stationarySamples = 0;
    } else if (hypot(pointer.x - drag->pointer.x, pointer.y - drag->pointer.y) > 3) {
        if (++drag->stationarySamples >= 2 && drag->confirmed) {
            *drag = (WindowDrag){0};
            return;
        }
    }
    drag->frame = frame;
    drag->pointer = pointer;
}

@interface SnowApp : NSObject <NSApplicationDelegate>
@property(nonatomic) NSMutableArray<SnowPanel *> *panels;
@property(nonatomic) NSStatusItem *status;
@property(nonatomic) NSMenuItem *pauseItem;
@property(nonatomic) NSMenuItem *ballItem;
@property(nonatomic) NSMenuItem *intensityItem;
@property(nonatomic) NSMenuItem *windItem;
@property(nonatomic) NSMenuItem *australiaItem;
@property(nonatomic) NSMenuItem *autoCleanupItem;
@property(nonatomic) SnowSettingsController *settings;
@property(nonatomic) id externalClickMonitor;
@property(nonatomic) NSTimer *timer;
@property(nonatomic) CADisplayLink *displayLink API_AVAILABLE(macos(14.0));
@property(nonatomic) NSTimeInterval lastFrame;
@property(nonatomic) NSTimeInterval lastGeometry;
@property(nonatomic) NSTimeInterval lastDragMotion;
@property(nonatomic) NSPoint geometryPointer;
@property(nonatomic) CGFloat primaryTop;
@property(nonatomic) int intensity;
@property(nonatomic) int wind;
@property(nonatomic) BOOL paused;
@property(nonatomic) BOOL interactive;
@property(nonatomic) BOOL australia;
@property(nonatomic) BOOL pointerWasDown;
@property(nonatomic) BOOL screensSleeping;
@property(nonatomic) BOOL sessionInactive;
@property(nonatomic) NSTimeInterval cleanupInterval;
@property(nonatomic) NSTimeInterval cleanupElapsed;
@property(nonatomic) SnowCleanup nextCleanup;
@property(nonatomic) NSTimeInterval snowfallTime;
@property(nonatomic) BOOL santaEnabled;
@property(nonatomic) NSTimeInterval santaInterval, santaElapsed, mischiefDuration;
@end

@implementation SnowApp {
    WindowDrag _drag;
    BOOL _dragAttempted;
    BOOL _desktopSelectionPending;
    BOOL _geometryRequestInFlight;
    uint64_t _geometryGeneration;
    NSArray<NSDictionary *> *_pendingWindowGeometry;
    NSPoint _pendingGeometryPointer;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    SnowPreferences preferences = snow_read_preferences(NSUserDefaults.standardUserDefaults);
    self.intensity = (int)preferences.intensity;
    self.wind = (int)preferences.wind;
    self.australia = preferences.australia;
    self.cleanupInterval = preferences.cleanupMinutes * 60;
    self.santaEnabled = preferences.santa;
    self.santaInterval = preferences.santaMinutes * 60;
    self.mischiefDuration = preferences.mischiefSeconds;
    self.nextCleanup = SnowCleanupPlow;
    [self makeMenu];
    [self configureScreens:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(configureScreens:)
                                               name:NSApplicationDidChangeScreenParametersNotification object:nil];
    NSNotificationCenter *workspace = NSWorkspace.sharedWorkspace.notificationCenter;
    [workspace addObserver:self selector:@selector(spaceChanged:) name:NSWorkspaceActiveSpaceDidChangeNotification object:nil];
    for (NSNotificationName name in @[NSWorkspaceScreensDidSleepNotification, NSWorkspaceScreensDidWakeNotification,
                                      NSWorkspaceSessionDidResignActiveNotification, NSWorkspaceSessionDidBecomeActiveNotification]) {
        [workspace addObserver:self selector:@selector(displayAvailabilityChanged:) name:name object:nil];
    }
}

- (void)resetFrameClock {
    [self invalidateWindowGeometry];
    [self.timer invalidate];
    self.timer = nil;
    if (@available(macOS 14.0, *)) {
        [self.displayLink invalidate];
        self.displayLink = nil;
    }
    self.lastFrame = 0;
    self.lastGeometry = 0;
    self.lastDragMotion = 0;
    self.pointerWasDown = NO;
    _desktopSelectionPending = YES;
    _drag = (WindowDrag){0};
    _dragAttempted = NO;
    if (self.paused || self.screensSleeping || self.sessionInactive || !self.panels.count) return;
    if (@available(macOS 14.0, *)) {
        self.displayLink = [NSScreen.screens.firstObject displayLinkWithTarget:self selector:@selector(displayFrame:)];
        if (self.displayLink) {
            self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
            [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
            return;
        }
    }
    self.timer = [NSTimer timerWithTimeInterval:1.0 / 60 target:self selector:@selector(timerFrame:)
                                      userInfo:nil repeats:YES];
    self.timer.tolerance = .002;
    [NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSRunLoopCommonModes];
}

- (void)displayAvailabilityChanged:(NSNotification *)notification {
    if (!NSThread.isMainThread) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self displayAvailabilityChanged:notification]; });
        return;
    }
    NSString *name = notification.name;
    if ([name isEqualToString:NSWorkspaceScreensDidSleepNotification]) self.screensSleeping = YES;
    if ([name isEqualToString:NSWorkspaceScreensDidWakeNotification]) self.screensSleeping = NO;
    if ([name isEqualToString:NSWorkspaceSessionDidResignActiveNotification]) self.sessionInactive = YES;
    if ([name isEqualToString:NSWorkspaceSessionDidBecomeActiveNotification]) self.sessionInactive = NO;
    if (self.screensSleeping || self.sessionInactive) {
        if (self.interactive) [self toggleInteraction:nil];
        for (SnowPanel *panel in self.panels) {
            SnowView *view = (SnowView *)panel.contentView;
            view.scene->pointer_known = false;
            [view hideDetachedDrift];
            [view discardCleanup];
            [view resetHolidayEffects];
            [panel orderOut:nil];
        }
    }
    [self resetFrameClock];
}

- (NSMenuItem *)item:(NSString *)title action:(SEL)action {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    item.target = self;
    return item;
}

- (void)makeMenu {
    self.status = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    self.status.button.title = @"❄";
    self.status.button.toolTip = @"Снегопад";
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Снегопад"];
    self.pauseItem = [self item:@"Пауза" action:@selector(togglePause:)];
    self.ballItem = [self item:@"Режим снежков" action:@selector(toggleInteraction:)];
    [menu addItem:self.pauseItem];
    [menu addItem:self.ballItem];
    NSMenuItem *clear = [self item:@"Очистить снег" action:nil];
    clear.submenu = [[NSMenu alloc] initWithTitle:clear.title];
    NSArray<NSString *> *effects = snow_cleanup_titles();
    for (NSUInteger i = 0; i < effects.count; i++) {
        NSMenuItem *effect = [self item:effects[i] action:@selector(clear:)];
        effect.tag = i;
        [clear.submenu addItem:effect];
    }
    [menu addItem:clear];
    self.autoCleanupItem = [self item:@"Автоочистка" action:nil];
    self.autoCleanupItem.submenu = [[NSMenu alloc] initWithTitle:self.autoCleanupItem.title];
    for (NSNumber *minutes in @[@0, @5, @15, @30]) {
        NSString *title = minutes.intValue ? [NSString stringWithFormat:@"%@ минут", minutes] : @"Выключена";
        NSMenuItem *item = [self item:title action:@selector(setCleanupIntervalOption:)];
        item.tag = minutes.intValue * 60;
        [self.autoCleanupItem.submenu addItem:item];
    }
    [menu addItem:self.autoCleanupItem];
    [menu addItem:NSMenuItem.separatorItem];

    self.intensityItem = [self item:@"Снегопад" action:nil];
    self.intensityItem.submenu = [[NSMenu alloc] initWithTitle:@"Снегопад"];
    NSArray<NSString *> *rates = @[@"Лёгкий", @"Ровный", @"Метель"];
    for (NSUInteger i = 0; i < rates.count; i++) {
        NSMenuItem *option = [self item:rates[i] action:@selector(setIntensityOption:)];
        option.tag = i;
        [self.intensityItem.submenu addItem:option];
    }
    [menu addItem:self.intensityItem];

    self.windItem = [self item:@"Ветер" action:nil];
    self.windItem.submenu = [[NSMenu alloc] initWithTitle:@"Ветер"];
    NSArray<NSString *> *winds = @[@"Влево", @"Штиль", @"Вправо"];
    for (NSUInteger i = 0; i < winds.count; i++) {
        NSMenuItem *option = [self item:winds[i] action:@selector(setWindOption:)];
        option.tag = (NSInteger)i - 1;
        [self.windItem.submenu addItem:option];
    }
    [menu addItem:self.windItem];
    self.australiaItem = [self item:@"Австралия" action:@selector(toggleAustralia:)];
    [menu addItem:self.australiaItem];
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *settings = [self item:@"Настройки…" action:@selector(showSettings:)];
    settings.keyEquivalent = @",";
    [menu addItem:settings];
    [menu addItem:[self item:@"Выход" action:@selector(quit:)]];
    self.status.menu = menu;
    [self updateMenu];
}

- (void)updateMenu {
    self.pauseItem.title = self.paused ? @"Продолжить" : @"Пауза";
    self.ballItem.title = self.interactive ? @"Выйти из режима снежков (Esc)" : @"Режим снежков";
    self.australiaItem.state = self.australia ? NSControlStateValueOn : NSControlStateValueOff;
    for (NSMenuItem *item in self.intensityItem.submenu.itemArray) {
        item.state = item.tag == self.intensity ? NSControlStateValueOn : NSControlStateValueOff;
    }
    for (NSMenuItem *item in self.windItem.submenu.itemArray) {
        item.state = item.tag == self.wind ? NSControlStateValueOn : NSControlStateValueOff;
    }
    for (NSMenuItem *item in self.autoCleanupItem.submenu.itemArray) {
        item.state = item.tag == self.cleanupInterval ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [self.settings refresh:[self currentPreferences] paused:self.paused];
}

- (SnowPreferences)currentPreferences {
    return (SnowPreferences){
        self.intensity, self.wind, self.australia, self.santaEnabled,
        (NSInteger)(self.cleanupInterval / 60), (NSInteger)(self.santaInterval / 60), (NSInteger)self.mischiefDuration
    };
}

- (void)savePreferences {
    snow_write_preferences(NSUserDefaults.standardUserDefaults, [self currentPreferences]);
    [self updateMenu];
}

- (void)setIntensityOption:(NSMenuItem *)item { self.intensity = (int)item.tag; [self savePreferences]; }
- (void)setWindOption:(NSMenuItem *)item { self.wind = (int)item.tag; [self savePreferences]; }
- (void)quit:(id)sender { [NSApp terminate:nil]; }

- (void)toggleAustralia:(id)sender {
    [self changeAustralia:!self.australia];
    [self savePreferences];
}

- (void)changeAustralia:(BOOL)enabled {
    if (self.australia == enabled) return;
    self.australia = enabled;
    self.cleanupElapsed = self.snowfallTime = 0;
    for (SnowPanel *panel in self.panels) {
        SnowView *view = (SnowView *)panel.contentView;
        [view discardCleanup];
        [view resetHolidayEffects];
        [view clearDesktopAccumulation];
        snow_set_inverted(view.scene, self.australia);
        [view render];
    }
    self.lastGeometry = 0;
}

- (void)setCleanupIntervalOption:(NSMenuItem *)item {
    self.cleanupInterval = item.tag;
    self.cleanupElapsed = 0;
    [self savePreferences];
}

- (void)applyPreferences:(SnowPreferences)preferences {
    self.intensity = (int)preferences.intensity;
    self.wind = (int)preferences.wind;
    [self changeAustralia:preferences.australia];
    if (self.cleanupInterval != preferences.cleanupMinutes * 60) {
        self.cleanupInterval = preferences.cleanupMinutes * 60;
        self.cleanupElapsed = 0;
    }
    if (self.santaEnabled != preferences.santa || self.santaInterval != preferences.santaMinutes * 60) {
        self.santaElapsed = 0;
        self.santaEnabled = preferences.santa;
        self.santaInterval = preferences.santaMinutes * 60;
    }
    self.mischiefDuration = preferences.mischiefSeconds;
    for (SnowPanel *panel in self.panels) ((SnowView *)panel.contentView).mischiefDuration = self.mischiefDuration;
    [self savePreferences];
}

- (void)showSettings:(id)sender {
    if (self.interactive) [self toggleInteraction:nil];
    if (!self.settings) {
        SnowSettingsController *settings = [SnowSettingsController new];
        __weak SnowApp *weakSelf = self;
        settings.onChange = ^(SnowPreferences preferences) { [weakSelf applyPreferences:preferences]; };
        settings.onFlight = ^{ [weakSelf beginSantaAtTime:CACurrentMediaTime()]; };
        settings.onCleanup = ^(SnowCleanup style) { [weakSelf clearWithStyle:style atTime:CACurrentMediaTime()]; };
        self.settings = settings;
    }
    [self.settings showPreferences:[self currentPreferences] paused:self.paused];
}

- (BOOL)beginSantaAtTime:(NSTimeInterval)time {
    if (self.paused || self.screensSleeping || self.sessionInactive) return NO;
    BOOL started = NO;
    for (SnowPanel *panel in self.panels) {
        SnowView *view = (SnowView *)panel.contentView;
        started |= [view startSantaAtTime:time];
    }
    if (started) self.santaElapsed = 0;
    return started;
}
- (void)clear:(NSMenuItem *)sender {
    [self clearWithStyle:sender.tag atTime:CACurrentMediaTime()];
}
- (void)clearWithStyle:(SnowCleanup)style atTime:(NSTimeInterval)time {
    if (self.interactive) [self toggleInteraction:nil];
    if (self.paused || NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion) style = SnowCleanupImmediate;
    self.cleanupElapsed = 0;
    self.lastDragMotion = 0;
    _drag = (WindowDrag){0};
    _dragAttempted = NO;
    self.snowfallTime = 0;
    self.lastGeometry = 0;
    for (SnowPanel *panel in self.panels) {
        SnowView *view = (SnowView *)panel.contentView;
        [view clearDesktopAccumulation];
        [view beginCleanup:view.isOnActiveDesktop ? style : SnowCleanupImmediate atTime:time];
    }
}
- (void)togglePause:(id)sender {
    if (!self.paused && self.interactive) [self toggleInteraction:nil];
    self.paused = !self.paused;
    for (SnowPanel *panel in self.panels) {
        SnowView *view = (SnowView *)panel.contentView;
        view.scene->pointer_known = false;
        [view hideDetachedDrift];
        [view discardCleanup];
        [view resetHolidayEffects];
        [panel orderOut:nil];
    }
    [self resetFrameClock];
    [self updateMenu];
}
- (void)toggleInteraction:(id)sender {
    if (self.paused) return;
    self.interactive = !self.interactive;
    if (self.interactive) [self updateGeometry];
    for (SnowPanel *panel in self.panels) {
        SnowView *view = (SnowView *)panel.contentView;
        snow_cancel_ball(view.scene);
        view.scene->pointer_known = false;
        panel.acceptsMouseMovedEvents = self.interactive;
        panel.ignoresMouseEvents = !self.interactive || [view shouldPassClickAt:NSEvent.mouseLocation];
        if (panel.ignoresMouseEvents && panel.isKeyWindow) [panel resignKeyWindow];
    }
    if (self.interactive) {
        NSPoint mouse = NSEvent.mouseLocation;
        for (SnowPanel *panel in self.panels) {
            if (panel.isVisible && panel.isOnActiveSpace && !panel.ignoresMouseEvents && NSPointInRect(mouse, panel.frame)) {
                [panel makeKeyWindow];
                [panel makeFirstResponder:panel.contentView];
                break;
            }
        }
        __weak SnowApp *weakSelf = self;
        NSEventMask monitored = NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskMouseMoved;
        self.externalClickMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:monitored
            handler:^(NSEvent *event) {
                SnowApp *app = weakSelf;
                if (!app.interactive) return;
                if (event.type == NSEventTypeMouseMoved) {
                    [app updateBallClickRoutingAt:NSEvent.mouseLocation];
                } else {
                    [app toggleInteraction:nil];
                }
            }];
    } else if (self.externalClickMonitor) {
        [NSEvent removeMonitor:self.externalClickMonitor];
        self.externalClickMonitor = nil;
    }
    [self updateMenu];
}

- (void)updateBallClickRoutingAt:(NSPoint)pointer {
    for (SnowPanel *panel in self.panels) {
        BOOL pass = [(SnowView *)panel.contentView shouldPassClickAt:pointer];
        if (panel.ignoresMouseEvents != pass) panel.ignoresMouseEvents = pass;
    }
}

- (void)spaceChanged:(NSNotification *)notification {
    if (!NSThread.isMainThread) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self spaceChanged:notification]; });
        return;
    }
    if (self.interactive) [self toggleInteraction:nil];
    [self invalidateWindowGeometry];
    _desktopSelectionPending = YES;
    self.lastGeometry = 0;
    self.lastDragMotion = 0;
    _drag = (WindowDrag){0};
    _dragAttempted = self.pointerWasDown;
    for (SnowPanel *panel in self.panels) {
        if (panel.isOnActiveSpace) continue;
        SnowView *view = (SnowView *)panel.contentView;
        [view hideDetachedDrift];
        [view discardCleanup];
        [view resetHolidayEffects];
        [panel orderOut:nil];
    }
}

- (void)configureScreens:(NSNotification *)notification {
    if (self.interactive) [self toggleInteraction:nil];
    for (SnowPanel *panel in self.panels) [panel close];
    self.panels = [NSMutableArray array];
    NSArray<NSScreen *> *screens = NSScreen.screens;
    self.primaryTop = NSMaxY(screens.firstObject.frame);
    for (NSScreen *screen in screens) {
        NSRect frame = screen.frame;
        SnowRect rect = {frame.origin.x, self.primaryTop - NSMaxY(frame), frame.size.width, frame.size.height};
        SnowView *view = [[SnowView alloc] initWithScreen:rect seed:arc4random()];
        if (!view) continue;
        view.mischiefDuration = self.mischiefDuration;
        CGFloat backingScale = screen.backingScaleFactor;
        if (MAX(rect.width, rect.height) * backingScale >= 4800) {
            view.particleFrameInterval = 1.0 / 30;
            view.separatesDraggedDrift = YES;
        }
        snow_set_inverted(view.scene, self.australia);
        __weak SnowApp *weakSelf = self;
        view.leaveInteraction = ^{ if (weakSelf.interactive) [weakSelf toggleInteraction:nil]; };
        [self.panels addObject:make_snow_panel(frame, view)];
    }
    [self resetFrameClock];
}

- (void)invalidateWindowGeometry {
    _geometryGeneration++;
    _pendingWindowGeometry = nil;
}

- (void)requestWindowGeometry {
    if (_geometryRequestInFlight) return;
    _geometryRequestInFlight = YES;
    uint64_t generation = _geometryGeneration;
    NSPoint pointer = self.geometryPointer;
    __weak SnowApp *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSArray<NSDictionary *> *windows = copy_window_geometry();
        dispatch_async(dispatch_get_main_queue(), ^{
            SnowApp *app = weakSelf;
            if (!app) return;
            app->_geometryRequestInFlight = NO;
            if (app->_geometryGeneration == generation) {
                app->_pendingWindowGeometry = windows;
                app->_pendingGeometryPointer = pointer;
            }
        });
    });
}

- (void)updateGeometry {
    [self invalidateWindowGeometry];
    [self applyGeometry:copy_window_geometry() fromPointer:self.geometryPointer toPointer:self.geometryPointer];
}

- (void)applyGeometry:(NSArray<NSDictionary *> *)windows fromPointer:(NSPoint)from toPointer:(NSPoint)to {
    if (windows && self.pointerWasDown && !self.interactive && (!_dragAttempted || _drag.window)) {
        [self trackDragInWindows:windows fromPointer:from toPointer:to];
    }
    [self applyWindowGeometry:windows];
}

- (void)applyWindowGeometry:(NSArray<NSDictionary *> *)windows {
    if (!windows) return;
    int ownPID = NSProcessInfo.processInfo.processIdentifier;
    int dockLevel = CGWindowLevelForKey(kCGDockWindowLevelKey);
    float cornerInset = windowCornerInset();
    BOOL showPanels = !self.paused && !self.screensSleeping && !self.sessionInactive;
    BOOL retryDesktopSelection = NO;
    for (NSUInteger i = 0; i < self.panels.count; i++) {
        SnowPanel *panel = self.panels[i];
        SnowView *view = (SnowView *)panel.contentView;
        SnowScene *scene = view.scene;
        SnowSurface surfaces[SNOW_MAX_SURFACES - 1];
        size_t count = 0;
        BOOL fullscreen = NO;
        BOOL foundNormalWindow = NO;
        for (NSDictionary *window in windows) {
            if ([window[(__bridge NSString *)kCGWindowOwnerPID] intValue] == ownPID ||
                [window[(__bridge NSString *)kCGWindowAlpha] floatValue] <= .05f) continue;
            int layer = [window[(__bridge NSString *)kCGWindowLayer] intValue];
            if (layer != 0 && layer != dockLevel) continue;
            CGRect frame;
            NSDictionary *bounds = window[(__bridge NSString *)kCGWindowBounds];
            if (!bounds || !CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)bounds, &frame)) continue;
            SnowRect rect = {frame.origin.x, frame.origin.y, frame.size.width, frame.size.height};
            if (rect.width < 22 || rect.height < 18 || !overlaps(rect, scene->screen)) continue;
            if (layer == dockLevel && coversScreen(rect, scene->screen)) continue;
            if (layer == 0 && !foundNormalWindow) {
                foundNormalWindow = YES;
                fullscreen = coversScreen(rect, scene->screen);
            }
            surfaces[count++] = (SnowSurface){
                .id = [window[(__bridge NSString *)kCGWindowNumber] unsignedLongLongValue],
                .rect = rect, .catches = true, .corner_radius = layer == 0 ? cornerInset : 0,
            };
            if (count == SNOW_MAX_SURFACES - 1) break;
        }
        if (fullscreen) {
            [view hideDetachedDrift];
            [view discardCleanup];
            [view resetHolidayEffects];
            if (panel.isVisible) [panel orderOut:nil];
            scene->pointer_known = false;
            snow_cancel_ball(scene);
            continue;
        }

        BOOL panelOnDesktop = panel.isOnActiveSpace;
        DesktopSelection selection = DesktopSelectionUnchanged;
        if (_desktopSelectionPending || !panelOnDesktop || !panel.isVisible) {
            selection = [view selectDesktopAtSnowfallTime:self.snowfallTime];
            if (selection == DesktopSelectionUnavailable) {
                [panel orderOut:nil];
                retryDesktopSelection = YES;
                continue;
            }
        }
        if (selection == DesktopSelectionChanged || !panelOnDesktop) {
            NSRect frame = panel.frame;
            [panel orderOut:nil];
            panel.contentView = nil;
            [panel close];
            panel = make_snow_panel(frame, view);
            self.panels[i] = panel;
        }

        snow_surfaces(scene, surfaces, count);
        [view restoreDesktopAccumulationAtSnowfallTime:self.snowfallTime];
        if (showPanels && !panel.isVisible) {
            [view render];
            [panel orderFrontRegardless];
            if (!panel.isOnActiveSpace) {
                [panel orderOut:nil];
                retryDesktopSelection = YES;
            }
        }
    }
    _desktopSelectionPending = retryDesktopSelection;
}

- (void)trackDragInWindows:(NSArray<NSDictionary *> *)windows fromPointer:(NSPoint)from toPointer:(NSPoint)to {
    BOOL starting = !_dragAttempted;
    _dragAttempted = YES;
    CGPoint pointer = CGPointMake(from.x, self.primaryTop - from.y);
    CGPoint latest = CGPointMake(to.x, self.primaryTop - to.y);
    int ownPID = NSProcessInfo.processInfo.processIdentifier;
    for (NSDictionary *window in windows) {
        if ([window[(__bridge NSString *)kCGWindowLayer] intValue] != 0 ||
            [window[(__bridge NSString *)kCGWindowOwnerPID] intValue] == ownPID ||
            [window[(__bridge NSString *)kCGWindowAlpha] floatValue] <= .05f) continue;
        uint64_t id = [window[(__bridge NSString *)kCGWindowNumber] unsignedLongLongValue];
        if (!starting && id != _drag.window) continue;
        NSDictionary *bounds = window[(__bridge NSString *)kCGWindowBounds];
        CGRect frame;
        if (!bounds || !CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)bounds, &frame)) continue;
        if (starting) {
            if (!CGRectContainsPoint(frame, pointer)) continue;
            if (id && pointer.y - frame.origin.y <= 48 && frame.size.width >= 22 && frame.size.height >= 18) {
                _drag = (WindowDrag){.window = id, .initialFrame = frame, .frame = frame,
                                     .initialPointer = pointer, .pointer = pointer};
            }
        } else {
            observe_drag(&_drag, frame, drag_pointer_for_frame(_drag, frame, pointer, latest));
        }
        return;
    }
    _drag = (WindowDrag){0};
}

- (void)displayFrame:(CADisplayLink *)link API_AVAILABLE(macos(14.0)) {
    [self frameAt:link.targetTimestamp];
}

- (void)timerFrame:(NSTimer *)timer {
    [self frameAt:NSProcessInfo.processInfo.systemUptime];
}

- (void)frameAt:(NSTimeInterval)now {
    [self renderFrameAt:now pointer:NSEvent.mouseLocation buttonDown:(NSEvent.pressedMouseButtons & 1) != 0
                lowPower:NSProcessInfo.processInfo.lowPowerModeEnabled];
}

- (void)renderFrameAt:(NSTimeInterval)now pointer:(NSPoint)pointer buttonDown:(BOOL)down lowPower:(BOOL)lowPower {
    if (self.paused || self.screensSleeping || self.sessionInactive) return;
    if (self.interactive) [self updateBallClickRoutingAt:pointer];
    BOOL tracking = down || self.pointerWasDown;
    double interval = 1.0 / (tracking ? (lowPower ? 30 : 60) : (lowPower ? 15 : 30));
    if (self.lastFrame && now - self.lastFrame < interval - .003) return;
    float dt = self.lastFrame ? (float)fmin(.10, now - self.lastFrame) : (float)interval;
    self.lastFrame = now;
    BOOL pointerMoved = !NSEqualPoints(pointer, self.geometryPointer);
    if (down && pointerMoved) self.lastDragMotion = now;
    BOOL dragBoundary = !self.interactive && down != self.pointerWasDown;
    BOOL dragGeometry = dragBoundary || (!self.interactive && down &&
        (pointerMoved || (_drag.window && now - self.lastDragMotion < .05)));
    if (!down || self.interactive) {
        _drag = (WindowDrag){0};
        _dragAttempted = NO;
    }
    self.pointerWasDown = down;
    if (!dragBoundary && self.lastGeometry && !_desktopSelectionPending && _pendingWindowGeometry) {
        NSArray<NSDictionary *> *windows = _pendingWindowGeometry;
        _pendingWindowGeometry = nil;
        [self applyGeometry:windows fromPointer:_pendingGeometryPointer toPointer:pointer];
    }
    if (dragGeometry || !self.lastGeometry || now - self.lastGeometry >= (lowPower ? .25 : .11)) {
        self.geometryPointer = pointer;
        if (dragBoundary || !self.lastGeometry || _desktopSelectionPending) {
            [self updateGeometry];
        } else {
            [self requestWindowGeometry];
        }
        self.lastGeometry = now;
    }
    int intensity = lowPower ? MIN(1, self.intensity) : self.intensity;
    int steps = dt > .05f ? 2 : 1;
    BOOL hasRunningScene = NO;
    for (SnowPanel *panel in self.panels) {
        if (!panel.isVisible) continue;
        SnowView *view = (SnowView *)panel.contentView;
        [view isCleaningAtTime:now];
        hasRunningScene = YES;
        if (!self.interactive && NSPointInRect(pointer, panel.frame)) {
            snow_sweep(view.scene, pointer.x, snow_screen_y(view.scene, self.primaryTop - pointer.y));
        } else {
            view.scene->pointer_known = false;
        }
        for (int step = 0; step < steps; step++) {
            snow_step(view.scene, dt / steps, intensity, self.wind);
        }
        [view advanceHolidayAtTime:now targetX:pointer.x];
    }
    if (hasRunningScene) {
        static const double rates[] = {26.0 / 54, 1, 125.0 / 54};
        self.cleanupElapsed += dt;
        self.snowfallTime += dt * rates[intensity];
        if (self.santaEnabled) self.santaElapsed += dt;
    }
    if (self.cleanupInterval && self.cleanupElapsed >= self.cleanupInterval && !_desktopSelectionPending && !self.interactive && !down) {
        [self clearWithStyle:self.nextCleanup atTime:now];
        self.nextCleanup++;
        if (self.nextCleanup == SnowCleanupCount) self.nextCleanup = SnowCleanupPlow;
        return;
    }
    if (hasRunningScene && self.santaEnabled && self.santaInterval > 0 && self.santaElapsed >= self.santaInterval && !down) {
        if (![self beginSantaAtTime:now]) self.santaElapsed = self.santaInterval - 1;
    }
    uint64_t draggedWindow = 0;
    CGFloat offset = 0;
    if (_drag.confirmed && down) {
        draggedWindow = _drag.window;
        offset = drag_offset_x(_drag, pointer.x);
    }
    for (SnowPanel *panel in self.panels) {
        if (!panel.isVisible) continue;
        SnowView *view = (SnowView *)panel.contentView;
        [view renderWithDraggedWindow:draggedWindow horizontalOffset:offset atTime:now];
    }
}
@end

int main(void) {
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        __attribute__((objc_precise_lifetime)) SnowApp *delegate = [SnowApp new];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
