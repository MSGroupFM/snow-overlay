#define main snow_application_main
#import "../macos/main.m"
#undef main
#include <assert.h>

@interface TrackingTestApp : SnowApp
@property(nonatomic) NSUInteger geometryUpdates;
@property(nonatomic) NSUInteger backgroundGeometryRequests;
@end

@implementation TrackingTestApp
- (void)updateGeometry { self.geometryUpdates++; }
- (void)requestWindowGeometry { self.backgroundGeometryRequests++; }
@end

@interface TestDesktopMarker : NSWindow
@property(nonatomic) BOOL active;
@end

@implementation TestDesktopMarker
- (BOOL)isOnActiveSpace { return self.active; }
@end

@interface DesktopTestView : SnowView
@property(nonatomic) TestDesktopMarker *lastMarker;
@end

@implementation DesktopTestView
- (NSWindow *)createDesktopMarker {
    self.lastMarker = [[TestDesktopMarker alloc] initWithContentRect:NSMakeRect(0, 0, 2, 2)
                                                         styleMask:NSWindowStyleMaskBorderless
                                                           backing:NSBackingStoreBuffered defer:YES];
    self.lastMarker.releasedWhenClosed = NO;
    self.lastMarker.active = YES;
    return self.lastMarker;
}
@end

static void check_drag_timing(void) {
    TrackingTestApp *app = [TrackingTestApp new];
    const NSPoint start = {20, 25}, moved = {38, 40};
    const double frame = 1.0 / 60;
    [app renderFrameAt:10 pointer:start buttonDown:NO lowPower:NO];
    assert(app.geometryUpdates == 1);
    [app renderFrameAt:10 + frame pointer:start buttonDown:YES lowPower:NO];
    [app renderFrameAt:10 + frame * 2 pointer:moved buttonDown:YES lowPower:NO];
    assert(app.geometryUpdates == 2 && app.backgroundGeometryRequests == 1);
    [app renderFrameAt:10 + frame * 3 pointer:moved buttonDown:YES lowPower:NO];
    assert(app.geometryUpdates == 2 && app.backgroundGeometryRequests == 1);
    [app renderFrameAt:10 + frame * 4 pointer:moved buttonDown:NO lowPower:NO];
    assert(app.geometryUpdates == 3 && app.backgroundGeometryRequests == 1);
    [app renderFrameAt:10 + frame * 6 pointer:moved buttonDown:NO lowPower:NO];
    assert(app.geometryUpdates == 3 && app.backgroundGeometryRequests == 1);
    [app renderFrameAt:10 + frame * 13 pointer:moved buttonDown:NO lowPower:NO];
    assert(app.geometryUpdates == 3 && app.backgroundGeometryRequests == 2);
}

static unsigned alpha_at(CGContextRef ctx, int x, int y) {
    const uint8_t *pixels = CGBitmapContextGetData(ctx);
    return pixels[y * CGBitmapContextGetBytesPerRow(ctx) + x * 4 + 3];
}

static void check_drag_alignment(void) {
    CGRect start = CGRectMake(100, 200, 185, 60);
    CGPoint anchor = CGPointMake(125, 215);
    WindowDrag drag = {.window = 1, .initialFrame = start, .frame = start,
                       .initialPointer = anchor, .pointer = anchor};
    observe_drag(&drag, start, CGPointMake(142, 215));
    assert(drag_offset_x(drag, 146) == 0);

    CGRect moved = CGRectOffset(start, 26, -15);
    observe_drag(&drag, moved, CGPointMake(166, 193));
    assert(drag_offset_x(drag, 175) == 24);
    assert(drag_offset_x(drag, 143) == -8);
    assert(drag_offset_x(drag, 400) == 0);

    WindowDrag resized = drag;
    observe_drag(&resized, CGRectMake(126, 185, 200, 60), CGPointMake(170, 191));
    assert(!resized.confirmed && !resized.window);
    observe_drag(&drag, moved, CGPointMake(175, 187));
    observe_drag(&drag, moved, CGPointMake(185, 180));
    assert(!drag.confirmed && !drag.window);

    WindowDrag delayed = {.window = 1, .initialFrame = start, .frame = start,
                          .initialPointer = anchor, .pointer = anchor};
    CGPoint from = CGPointMake(142, 206), to = CGPointMake(221, 154);
    observe_drag(&delayed, moved, drag_pointer_for_frame(delayed, moved, from, to));
    assert(delayed.confirmed && drag_offset_x(delayed, 175) == 24);
}

static void check_scene(SnowView *view, bool inverted) {
    SnowScene *scene = view.scene;
    const SnowSurface edge = {1, {50, 130, 185, 60}, true, 26};
    snow_set_inverted(scene, inverted);
    snow_surfaces(scene, &edge, 1);
    for (size_t i = 0; i < SNOW_BINS; i++) scene->drifts[0].height[i] = 10;
    scene->drifts[0].revision = ++scene->revision;
    scene->flakes[0] = (SnowFlake){.x = -16, .y = 52, .radius = 4, .vy = 60};
    scene->flake_count = 1;
    [view render];

    CALayer *flake = view.layer.sublayers[0].sublayers[0];
    CAShapeLayer *bank = (CAShapeLayer *)view.layer.sublayers[1].sublayers[0];
    assert(!flake.hidden && !bank.hidden);
    assert(fabs(flake.position.y - (inverted ? 283 : 37)) < .01);
    CGRect bounds = CGPathGetPathBoundingBox(bank.path);
    double top = CGRectGetMinY(bank.frame) + CGRectGetMinY(bounds);
    double bottom = CGRectGetMinY(bank.frame) + CGRectGetMaxY(bounds);
    assert(fabs(top - (inverted ? 175 : 105)) < .01);
    assert(fabs(bottom - (inverted ? 185 : 115)) < .01);
    assert(CGRectGetMinX(bank.frame) == 116);
    assert(CGRectGetMaxX(bank.frame) == 249);

    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, 420, 320, 8, 0, color,
                                             (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(color);
    assert(ctx);
    CGContextTranslateCTM(ctx, 0, 320);
    CGContextScaleCTM(ctx, 1, -1);
    [view.layer renderInContext:ctx];
    assert(alpha_at(ctx, 24, inverted ? 283 : 37) > 100);
    assert(alpha_at(ctx, 150, inverted ? 179 : 110) > 225);
    assert(alpha_at(ctx, 150, inverted ? 171 : 119) == 0);
    assert(alpha_at(ctx, 102, inverted ? 179 : 110) == 0);
    assert(alpha_at(ctx, 285, inverted ? 179 : 110) == 0);
    CGContextRelease(ctx);

    CGRect bankFrame = bank.frame;
    CGPathRef bankPath = bank.path;
    [view renderWithDraggedWindow:edge.id horizontalOffset:25];
    assert(CGRectEqualToRect(bank.frame, CGRectOffset(bankFrame, 25, 0)));
    assert(bank.path == bankPath);
    for (int i = 0; i < 12; i++) {
        CGFloat x = i % 2 ? 30 : -30;
        [view renderWithDraggedWindow:edge.id horizontalOffset:x];
        assert(CGRectGetMinY(bank.frame) == CGRectGetMinY(bankFrame));
        assert(CGRectGetMinX(bank.frame) == CGRectGetMinX(bankFrame) + x);
    }
    assert(scene->drifts[0].surface.rect.x == edge.rect.x);
    [view renderWithDraggedWindow:edge.id horizontalOffset:-120];
    assert(CGRectEqualToRect(bank.frame, bankFrame));
    [view renderWithDraggedWindow:edge.id horizontalOffset:15];
    [view render];
    assert(CGRectEqualToRect(bank.frame, bankFrame));

    SnowDrift *drift = &scene->drifts[0];
    drift->height[60] += .1f;
    drift->surface.rect.x += 20;
    drift->spans[0].left += 20;
    drift->spans[0].right += 20;
    drift->revision = ++scene->revision;
    [view render];
    assert(bank.path == bankPath);
    assert(CGRectEqualToRect(bank.frame, CGRectOffset(bankFrame, 20, 0)));
    bankFrame = bank.frame;

    view.particleFrameInterval = 1.0 / 30;
    [view renderWithDraggedWindow:0 horizontalOffset:0 atTime:1];
    CGPoint initialFlake = flake.position;
    scene->flakes[0].x += 4;
    [view renderWithDraggedWindow:edge.id horizontalOffset:12 atTime:1 + 1.0 / 60];
    assert(CGPointEqualToPoint(flake.position, initialFlake));
    assert(CGRectEqualToRect(bank.frame, CGRectOffset(bankFrame, 12, 0)));
    [view renderWithDraggedWindow:edge.id horizontalOffset:20 atTime:1 + 1.0 / 30];
    assert(flake.position.x == initialFlake.x + 4);
    assert(CGRectEqualToRect(bank.frame, CGRectOffset(bankFrame, 20, 0)));
    [view render];

    snow_step(scene, .04f, 0, 0);
    [view render];
    if (inverted) assert(flake.position.y < 283);
    else assert(flake.position.y > 37);
}

static void check_detached_drift(bool inverted) {
    SnowPanel *parent = [[SnowPanel alloc] initWithContentRect:NSMakeRect(90, 140, 420, 320)
                                         styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    parent.releasedWhenClosed = NO;
    SnowView *view = [[SnowView alloc] initWithScreen:(SnowRect){-40, 15, 420, 320} seed:7];
    view.separatesDraggedDrift = YES;
    parent.contentView = view;
    [parent orderFrontRegardless];

    SnowScene *scene = view.scene;
    const SnowSurface edge = {1, {50, 130, 185, 60}, true, 26};
    snow_set_inverted(scene, inverted);
    snow_surfaces(scene, &edge, 1);
    for (size_t i = 0; i < SNOW_BINS; i++) scene->drifts[0].height[i] = 10;
    scene->drifts[0].revision = ++scene->revision;
    [view render];
    CAShapeLayer *bank = (CAShapeLayer *)view.layer.sublayers[1].sublayers[0];
    CGRect initial = bank.frame;
    CGRect pathBounds = CGPathGetPathBoundingBox(bank.path);

    for (int i = 0; i < 24; i++) {
        CGFloat dx = i % 2 ? -39.5 : 44.5;
        [view renderWithDraggedWindow:edge.id horizontalOffset:dx atTime:1 + i / 60.0];
        assert(parent.childWindows.count == 1 && bank.hidden);
        NSWindow *narrow = parent.childWindows.firstObject;
        assert(narrow.isVisible && narrow.ignoresMouseEvents);
        assert(narrow.frame.size.height == SNOW_MAX_DEPTH);
        CAShapeLayer *shape = (CAShapeLayer *)narrow.contentView.layer.sublayers.firstObject;
        assert(CGRectEqualToRect(CGPathGetPathBoundingBox(shape.path), pathBounds));
        assert(fabs(NSMinX(narrow.frame) + CGRectGetMinX(shape.frame) - NSMinX(parent.frame) -
                    CGRectGetMinX(initial) - dx) < .01);
        assert(fabs(NSMaxY(parent.frame) - NSMaxY(narrow.frame) + CGRectGetMinY(shape.frame) -
                    CGRectGetMinY(initial)) < .01);
    }

    NSWindow *narrow = parent.childWindows.firstObject;
    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, 134, 24, 8, 0, color,
                                             (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(color);
    assert(ctx);
    CGContextTranslateCTM(ctx, 0, 24);
    CGContextScaleCTM(ctx, 1, -1);
    [narrow.contentView.layer renderInContext:ctx];
    assert(alpha_at(ctx, 50, inverted ? 5 : 19) > 225);
    assert(alpha_at(ctx, 50, inverted ? 19 : 5) == 0);
    CGContextRelease(ctx);

    [view render];
    assert(!narrow.isVisible && !parent.childWindows.count && !bank.hidden);
    assert(CGRectEqualToRect(bank.frame, initial));
    [parent close];
}

static void check_desktop_accumulation(void) {
    DesktopTestView *view = [[DesktopTestView alloc] initWithScreen:(SnowRect){0, 0, 600, 400} seed:123];
    SnowScene *scene = view.scene;
    SnowSurface surface = {27, {90, 180, 250, 120}, true, 16};
    [view selectDesktopAtSnowfallTime:0];
    TestDesktopMarker *first = view.lastMarker;
    snow_surfaces(scene, &surface, 1);
    scene->drifts[0].height[40] = 18;
    scene->balls[scene->ball_count++] = (SnowBall){27, .4f, 0, 10, 0};

    first.active = NO;
    [view selectDesktopAtSnowfallTime:120];
    TestDesktopMarker *second = view.lastMarker;
    assert(second != first);
    snow_surfaces(scene, &surface, 1);
    [view restoreDesktopAccumulationAtSnowfallTime:120];
    assert(scene->drifts[0].height[40] > 1 && scene->drifts[0].height[40] < 12);
    assert(scene->ball_count == 0 && scene->flake_count == 0);
    scene->drifts[0].height[40] = 8;
    scene->balls[scene->ball_count++] = (SnowBall){27, .6f, 0, 8, 0};

    second.active = NO;
    first.active = YES;
    [view selectDesktopAtSnowfallTime:120];
    snow_surfaces(scene, &surface, 1);
    [view restoreDesktopAccumulationAtSnowfallTime:120];
    assert(scene->drifts[0].height[40] == 18 && scene->ball_count == 1);
    SnowPanel *panel = [[SnowPanel alloc] initWithContentRect:NSMakeRect(0, 0, 600, 400)
                                                  styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:YES];
    panel.releasedWhenClosed = NO;
    panel.contentView = view;
    SnowApp *app = [SnowApp new];
    app.paused = YES;
    app.panels = [NSMutableArray arrayWithObject:panel];
    app.snowfallTime = 120;
    [app clearWithStyle:SnowCleanupImmediate atTime:1];
    assert(scene->drifts[0].height[40] == 0 && scene->ball_count == 0);
    assert(app.snowfallTime == 0);

    first.active = NO;
    second.active = YES;
    [view selectDesktopAtSnowfallTime:app.snowfallTime];
    snow_surfaces(scene, &surface, 1);
    [view restoreDesktopAccumulationAtSnowfallTime:app.snowfallTime];
    assert(scene->drifts[0].height[40] == 0 && scene->ball_count == 0);

    second.active = NO;
    [view selectDesktopAtSnowfallTime:app.snowfallTime];
    snow_surfaces(scene, &surface, 1);
    [view restoreDesktopAccumulationAtSnowfallTime:app.snowfallTime];
    assert(scene->drifts[0].height[40] == 0 && scene->ball_count == 0);
    [panel close];
}

static void check_snow_panel_stays_on_its_desktop(void) {
    SnowApp *app = [SnowApp new];
    app.paused = YES;
    [app configureScreens:nil];
    assert(app.panels.count > 0);
    for (SnowPanel *panel in app.panels) {
        assert(panel.collectionBehavior & NSWindowCollectionBehaviorManaged);
        assert(!(panel.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces));
        assert(!(panel.collectionBehavior & NSWindowCollectionBehaviorMoveToActiveSpace));
        [panel close];
    }
}

static void check_desktop_change_replaces_snow_panel(void) {
    // Keep window discovery from finding unrelated windows in the test scene.
    DesktopTestView *view = [[DesktopTestView alloc] initWithScreen:(SnowRect){-30000, -30000, 300, 200} seed:123];
    SnowApp *app = [SnowApp new];
    app.paused = YES;
    app.panels = [NSMutableArray arrayWithObject:make_snow_panel(NSMakeRect(90, 140, 300, 200), view)];
    [app updateGeometry];
    SnowPanel *outgoing = app.panels.firstObject;
    TestDesktopMarker *first = view.lastMarker;
    view.scene->drifts[0].height[40] = 18;

    first.active = NO;
    [app updateGeometry];
    SnowPanel *incoming = app.panels.firstObject;
    assert(incoming != outgoing && incoming.contentView == view && view.window == incoming);
    assert(outgoing.contentView != view && !outgoing.isVisible);
    assert(view.scene->drifts[0].height[40] == 0);

    view.lastMarker.active = NO;
    first.active = YES;
    [app updateGeometry];
    assert(app.panels.firstObject != incoming && view.scene->drifts[0].height[40] == 18);
    [app.panels.firstObject close];
}

static CALayer *named_layer(CALayer *parent, NSString *name) {
    for (CALayer *layer in parent.sublayers) if ([layer.name isEqualToString:name]) return layer;
    return nil;
}

static void check_cleanup_effects(bool inverted) {
    SnowPanel *panel = [[SnowPanel alloc] initWithContentRect:NSMakeRect(90, 140, 420, 320)
                                                  styleMask:NSWindowStyleMaskBorderless
                                                    backing:NSBackingStoreBuffered defer:NO];
    panel.releasedWhenClosed = NO;
    SnowView *view = [[SnowView alloc] initWithScreen:(SnowRect){0, 0, 420, 320} seed:7];
    panel.contentView = view;
    [panel orderFrontRegardless];
    TrackingTestApp *app = [TrackingTestApp new];
    app.panels = [NSMutableArray arrayWithObject:panel];
    app.intensity = 1;
    SnowScene *scene = view.scene;
    snow_set_inverted(scene, inverted);
    SnowSurface surface = {11, {50, 130, 240, 60}, true, 18};
    assert(panel.isVisible);
    for (SnowCleanup effect = SnowCleanupImmediate; effect < SnowCleanupCount; effect++) {
        snow_surfaces(scene, &surface, 1);
        for (size_t i = 0; i < scene->drift_count; i++) {
            for (size_t j = 0; j < SNOW_BINS; j++) scene->drifts[i].height[j] = 8;
            scene->drifts[i].revision = ++scene->revision;
        }
        scene->flakes[scene->flake_count++] = (SnowFlake){.x = 180, .y = 140, .radius = 4};
        scene->balls[scene->ball_count++] = (SnowBall){11, .4f, 0, 10, 0};
        scene->budget = .5;
        scene->previous[0].height[42] = 18;
        double start = 10 + effect * 10;
        [view beginCleanup:effect atTime:start];
        BOOL animated = effect != SnowCleanupImmediate;
        assert([view isCleaningAtTime:start + .5] == animated);
        assert(view.layer.sublayers.count == (animated ? 4 : 3));
        if (animated) assert(view.layer.sublayers.lastObject.sublayers.count >= 2);
        CALayer *visual = view.layer.sublayers.lastObject;
        if (effect == SnowCleanupPlow) {
            CALayer *wheelSpray = named_layer(visual, @"cleanup-wheel-spray");
            assert(wheelSpray.sublayers.count > 0 && wheelSpray.sublayers.count <= 72);
            CAAnimationGroup *group = (CAAnimationGroup *)[wheelSpray.sublayers.firstObject animationForKey:@"cleanup"];
            CAKeyframeAnimation *motion = (CAKeyframeAnimation *)group.animations.firstObject;
            NSPoint tire = [motion.values[1] pointValue], apex = [motion.values[2] pointValue];
            assert(apex.x < tire.x && (inverted ? apex.y > tire.y : apex.y < tire.y));
        }
        if (effect == SnowCleanupBoulder || effect == SnowCleanupShovel) {
            BOOL chase = effect == SnowCleanupBoulder;
            assert((named_layer(visual, @"cleanup-impact") != nil) == chase);
            CALayer *windowBank = named_layer(visual, @"cleanup-banks").sublayers.lastObject;
            CAAnimationGroup *group = (CAAnimationGroup *)[windowBank animationForKey:@"cleanup"];
            CAKeyframeAnimation *motion = (CAKeyframeAnimation *)group.animations.firstObject;
            NSPoint original = [motion.values[0] pointValue], hold = [motion.values[1] pointValue];
            NSPoint next = [motion.values[2] pointValue];
            assert(NSEqualPoints(original, hold));
            assert(chase == (inverted ? next.y > original.y : next.y < original.y));
            if (chase) {
                CALayer *explorer = named_layer(visual, @"cleanup-explorer");
                CALayer *boulder = named_layer(visual, @"cleanup-boulder");
                assert([explorer animationForKey:@"run"] && [boulder animationForKey:@"roll"]);
                assert(explorer.position.x > boulder.position.x);
            }
        }
        assert(!scene->flake_count && !scene->ball_count && !scene->budget && !scene->previous[0].height[42]);
        assert(scene->drift_count == 1 && scene->drifts[0].height[42] == 0 && scene->inverted == inverted);
        app.lastFrame = 0;
        [app renderFrameAt:start + .03 pointer:NSZeroPoint buttonDown:NO lowPower:NO];
        [app renderFrameAt:start + .07 pointer:NSZeroPoint buttonDown:NO lowPower:NO];
        assert(scene->flake_count > 0);
        assert([view isCleaningAtTime:start + .08] == animated);
        [app renderFrameAt:start + 7 pointer:NSZeroPoint buttonDown:NO lowPower:NO];
        assert(![view isCleaningAtTime:start + 7] && view.layer.sublayers.count == 3);
        assert(scene->flake_count > 0 && panel.isVisible);
    }
    [panel close];
}

static void check_ball_click_routing(bool inverted) {
    SnowPanel *panel = [[SnowPanel alloc] initWithContentRect:NSMakeRect(90, 140, 420, 320)
                                                  styleMask:NSWindowStyleMaskBorderless
                                                    backing:NSBackingStoreBuffered defer:NO];
    panel.releasedWhenClosed = NO;
    SnowView *view = [[SnowView alloc] initWithScreen:(SnowRect){0, 0, 420, 320} seed:7];
    panel.contentView = view;
    [panel orderFrontRegardless];
    SnowScene *scene = view.scene;
    snow_set_inverted(scene, inverted);
    SnowSurface window = {9, {50, 130, 240, 60}, true, 18};
    snow_surfaces(scene, &window, 1);
    NSPoint inside = [panel convertPointToScreen:[view convertPoint:NSMakePoint(170, 160) toView:nil]];
    NSPoint outside = [panel convertPointToScreen:[view convertPoint:NSMakePoint(26, 160) toView:nil]];
    assert([view shouldPassClickAt:inside] && ![view shouldPassClickAt:outside]);
    scene->balls[scene->ball_count++] = (SnowBall){window.id, .5f, 0, 11, 0};
    float y = snow_ball_height(scene, &scene->balls[0], 170);
    NSPoint ball = [panel convertPointToScreen:[view convertPoint:NSMakePoint(170, snow_screen_y(scene, y)) toView:nil]];
    assert(![view shouldPassClickAt:ball] && snow_pick_ball(scene, 170, y));
    assert(![view shouldPassClickAt:inside]);
    snow_cancel_ball(scene);
    assert([view shouldPassClickAt:inside] && scene->ball_count == 1);
    NSEvent *rightClick = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                                         location:[panel convertPointFromScreen:ball] modifierFlags:0 timestamp:1
                                     windowNumber:panel.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
    [view rightMouseDown:rightClick];
    assert(scene->ball_count == 0 && scene->flake_count > 1);
    assert(scene->flakes[0].deposit > 0);
    size_t singlePieces = scene->flake_count;
    scene->balls[scene->ball_count++] = (SnowBall){window.id, .5f, 0, 11, 0};
    scene->balls[scene->ball_count++] = (SnowBall){window.id, .5f, 22, 9, 0};
    scene->balls[scene->ball_count++] = (SnowBall){window.id, .5f, 40, 7, 0};
    [view render];
    CALayer *ballLayer = view.layer.sublayers[2];
    assert(ballLayer.sublayers.count == 3);
    assert(ballLayer.sublayers[1].sublayers.lastObject.contents && ballLayer.sublayers[2].sublayers.lastObject.contents);
    assert(ballLayer.sublayers[1].sublayers.lastObject.affineTransform.d == (inverted ? -1 : 1));
    [view rightMouseDown:rightClick];
    assert(scene->ball_count == 0 && scene->flake_count > 2 * singlePieces);
    [panel close];
}

static void check_ball_exit_button(void) {
    TrackingTestApp *app = [TrackingTestApp new];
    SnowView *view = [[SnowView alloc] initWithScreen:(SnowRect){0, 0, 420, 320} seed:7];
    SnowPanel *panel = make_snow_panel(NSMakeRect(90, 140, 420, 320), view);
    [panel orderFrontRegardless];
    app.panels = [NSMutableArray arrayWithObject:panel];
    __weak TrackingTestApp *weakApp = app;
    view.leaveInteraction = ^{ if (weakApp.interactive) [weakApp toggleInteraction:nil]; };
    assert(!panel.childWindows.count);
    [app toggleInteraction:nil];
    NSWindow *exitPanel = panel.childWindows.firstObject;
    assert(app.interactive && exitPanel.isVisible && !exitPanel.ignoresMouseEvents);
    assert(NSContainsRect(panel.frame, exitPanel.frame));
    NSButton *exit = (NSButton *)exitPanel.contentView.subviews.firstObject;
    assert([exit isKindOfClass:NSButton.class] && [exit.title isEqualToString:@"Выйти из режима снежков"]);

    SnowScene *scene = view.scene;
    const SnowSurface edge = {7, {50, 180, 250, 60}, true, 10};
    snow_surfaces(scene, &edge, 1);
    scene->balls[scene->ball_count++] = (SnowBall){7, .5f, 0, 10, 0};
    assert(snow_pick_ball(scene, 175, 170));
    panel.ignoresMouseEvents = YES;
    assert(!exitPanel.ignoresMouseEvents);
    [exit performClick:nil];
    assert(!app.interactive && !exitPanel.isVisible && !panel.childWindows.count);
    assert(panel.ignoresMouseEvents && !scene->holding && scene->ball_count == 1);
    [panel close];
}

static void check_ball_menu_exit(void) {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    TrackingTestApp *app = [TrackingTestApp new];
    [app makeMenu];
    NSMenu *menu = app.status.menu;
    assert([app.ballItem.title isEqualToString:@"Режим снежков"]);
    assert([NSApp sendAction:app.ballItem.action to:app.ballItem.target from:app.ballItem]);
    assert(app.interactive && [app.ballItem.title hasPrefix:@"Выйти из режима снежков"]);

    NSStatusBarButton *icon = app.status.button;
    NSRect frame = [icon.window convertRectToScreen:[icon convertRect:icon.bounds toView:nil]];
    assert(!NSIsEmptyRect(frame));
    [app externalBallMouseDownAt:NSMakePoint(NSMidX(frame), NSMidY(frame))];
    assert(app.interactive);
    [app menuWillOpen:menu];
    [app externalBallMouseDownAt:NSMakePoint(NSMidX(frame), NSMinY(frame) - 50)];
    assert(app.interactive && [app.ballItem.title hasPrefix:@"Выйти из режима снежков"]);
    [app menuDidClose:menu];
    assert([NSApp sendAction:app.ballItem.action to:app.ballItem.target from:app.ballItem]);
    assert(!app.interactive && [app.ballItem.title isEqualToString:@"Режим снежков"]);

    [app toggleInteraction:nil];
    [app externalBallMouseDownAt:NSMakePoint(NSMidX(frame), NSMinY(frame) - 50)];
    assert(!app.interactive);
    [NSStatusBar.systemStatusBar removeStatusItem:app.status];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
}

static void check_holiday_effects(bool inverted) {
    SnowView *view = [[SnowView alloc] initWithScreen:(SnowRect){0, 0, 620, 400} seed:123];
    SnowPanel *panel = make_snow_panel(NSMakeRect(90, 140, 620, 400), view);
    [panel orderFrontRegardless];
    SnowScene *scene = view.scene;
    snow_set_inverted(scene, inverted);
    SnowSurface surface = {11, {60, 170, 490, 60}, true, 18};
    snow_surfaces(scene, &surface, 1);
    SnowBall unused;
    assert(!snow_grow_snowman(scene, 300, 400, &unused));
    for (size_t i = 0; i < scene->drift_count; i++) {
        for (size_t bin = 0; bin < SNOW_BINS; bin++) scene->drifts[i].height[bin] = 8;
    }
    const CGFloat duration = holiday_flight_duration(NSWidth(view.bounds));
    assert([view startSantaAtTime:10] && ![view startSantaAtTime:10]);
    CALayer *flight = view.layer.sublayers.lastObject;
    assert([flight.name isEqualToString:@"santa-flight"]);
    NSUInteger gifts = 0;
    for (CALayer *layer in flight.sublayers) {
        if (![layer.name isEqualToString:@"santa-gift"]) continue;
        gifts++;
        CAAnimationGroup *group = (CAAnimationGroup *)[layer animationForKey:@"cleanup"];
        CAKeyframeAnimation *motion = (CAKeyframeAnimation *)group.animations.firstObject;
        CGPoint drop = [motion.values[0] pointValue], land = [motion.values[3] pointValue];
        assert(inverted ? land.y < drop.y : land.y > drop.y);
    }
    assert(gifts >= 3);
    [view advanceHolidayAtTime:10 + duration * .85 targetX:0 targetY:100];
    assert(scene->ball_count >= 3 && scene->ball_count <= 9);
    SnowBall head = scene->balls[2];
    unsigned appearance;
    assert(snow_snowman_details(scene, &head, &appearance) == SNOWMAN_HEAD && appearance == head.appearance);
    const SnowDrift *drift = snow_ball_surface(scene, &head);
    float x = drift->surface.rect.x + head.offset * drift->surface.rect.width;
    assert(snow_depth(drift, drift->spans[0], x) < 8);
    [view advanceHolidayAtTime:11 + duration targetX:0 targetY:100];
    assert(!flight.superlayer);
    [view configureMischiefDuration:10 threshold:2];
    [view renderWithDraggedWindow:0 horizontalOffset:0 atTime:29];
    CALayer *headDetails = view.layer.sublayers[2].sublayers[2].sublayers.lastObject;
    id normalHead = headDetails.contents;
    assert(normalHead);
    [view recordBrokenSnowmanAtTime:30 nearX:x];
    size_t flakes = scene->flake_count;
    [view advanceHolidayAtTime:30.5 targetX:0 targetY:100];
    assert(scene->flake_count == flakes);
    [view renderWithDraggedWindow:0 horizontalOffset:0 atTime:30.5];
    assert(headDetails.contents == normalHead);
    [view recordBrokenSnowmanAtTime:31 nearX:x];
    [view advanceHolidayAtTime:31.5 targetX:0 targetY:100];
    assert(scene->flake_count > flakes);
    [view renderWithDraggedWindow:0 horizontalOffset:0 atTime:31.5];
    assert(headDetails.contents != normalHead);
    flakes = scene->flake_count;
    [view advanceHolidayAtTime:41 targetX:0 targetY:100];
    assert(scene->flake_count == flakes);
    [view renderWithDraggedWindow:0 horizontalOffset:0 atTime:41];
    assert(headDetails.contents == normalHead);
    [view beginCleanup:SnowCleanupImmediate atTime:42];
    assert(!scene->ball_count && !scene->flake_count);
    [panel close];
}

static NSControl *find_settings_control(NSView *view, NSString *label) {
    if (view.hidden) return nil;
    if ([view isKindOfClass:NSButton.class] && [((NSButton *)view).title isEqualToString:label]) return (NSControl *)view;
    if ([view isKindOfClass:NSTextField.class] && ((NSTextField *)view).isEditable &&
        [view.accessibilityLabel isEqualToString:label]) return (NSControl *)view;
    for (NSView *child in view.subviews) {
        NSControl *control = find_settings_control(child, label);
        if (control) return control;
    }
    return nil;
}

static void check_settings_window(void) {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    SnowSettingsController *settings = [SnowSettingsController new];
    SnowPreferences preferences = {1, 0, NO, YES, 15, 5, 10, 3};
    [settings showPreferences:preferences paused:NO];
    assert(settings.isWindowLoaded && settings.window.isVisible);
    NSView *content = settings.window.contentView;
    NSControl *disclosure = find_settings_control(content, @"Злой режим");
    assert(disclosure && !find_settings_control(content, @"Снеговиков до включения"));
    [disclosure performClick:nil];
    [content layoutSubtreeIfNeeded];
    NSControl *threshold = find_settings_control(content, @"Снеговиков до включения");
    NSControl *duration = find_settings_control(content, @"Длительность злого режима");
    assert(threshold && threshold.integerValue == 3 && duration && duration.integerValue == 10);
    assert(NSContainsRect(content.bounds, [duration convertRect:duration.bounds toView:content]));
    __block NSInteger updatedThreshold = 0;
    settings.onChange = ^(SnowPreferences updated) { updatedThreshold = updated.mischiefThreshold; };
    threshold.integerValue = 4;
    [settings controlTextDidEndEditing:[NSNotification notificationWithName:NSControlTextDidEndEditingNotification object:threshold]];
    assert(updatedThreshold == 4);
    [settings.window close];
    [settings showPreferences:preferences paused:YES];
    assert(settings.window.isVisible && !find_settings_control(content, @"Снеговиков до включения"));
    [settings.window close];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
}

int main(void) {
    @autoreleasepool {
        [NSApplication.sharedApplication setActivationPolicy:NSApplicationActivationPolicyProhibited];
        SnowView *view = [[SnowView alloc] initWithScreen:(SnowRect){-40, 15, 420, 320} seed:7];
        check_scene(view, false);
        check_scene(view, true);
        check_drag_timing();
        check_drag_alignment();
        check_detached_drift(false);
        check_detached_drift(true);
        check_desktop_accumulation();
        check_cleanup_effects(false);
        check_cleanup_effects(true);
        check_ball_click_routing(false);
        check_ball_click_routing(true);
        check_ball_exit_button();
        check_ball_menu_exit();
        check_holiday_effects(false);
        check_holiday_effects(true);
        check_settings_window();
        check_snow_panel_stays_on_its_desktop();
        check_desktop_change_replaces_snow_panel();
        puts("macOS rendering, cadence, desktop accumulation, cleanup, ball routing and holiday effects passed");
    }
}
