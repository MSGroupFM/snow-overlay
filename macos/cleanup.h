#pragma once

typedef NS_ENUM(NSInteger, SnowCleanup) {
    SnowCleanupImmediate,
    SnowCleanupPlow,
    SnowCleanupWind,
    SnowCleanupShovel,
    SnowCleanupAvalanche,
    SnowCleanupThaw,
    SnowCleanupCount
};

static CAKeyframeAnimation *cleanup_keyframes(NSString *key, NSArray *values, NSArray<NSNumber *> *times,
                                             CFTimeInterval duration) {
    CAKeyframeAnimation *animation = [CAKeyframeAnimation animationWithKeyPath:key];
    animation.values = values;
    animation.keyTimes = times;
    animation.duration = duration;
    animation.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    animation.fillMode = kCAFillModeBoth;
    animation.removedOnCompletion = NO;
    return animation;
}

static void animate_cleanup(CALayer *layer, NSArray<NSValue *> *positions, NSArray<NSNumber *> *opacity,
                            NSArray<NSNumber *> *times, CFTimeInterval duration) {
    CAAnimationGroup *animation = [CAAnimationGroup animation];
    animation.animations = @[cleanup_keyframes(@"position", positions, times, duration),
                             cleanup_keyframes(@"opacity", opacity, times, duration)];
    animation.duration = duration;
    animation.fillMode = kCAFillModeBoth;
    animation.removedOnCompletion = NO;
    [layer addAnimation:animation forKey:@"cleanup"];
}

static void actor_part(CALayer *actor, CGRect frame, NSColor *color, CGFloat radius) {
    CALayer *part = [CALayer layer];
    part.frame = frame;
    part.backgroundColor = color.CGColor;
    part.cornerRadius = radius;
    [actor addSublayer:part];
}

static void cleanup_ice_impact(CALayer *parent, CGFloat x, CGFloat ground, CGFloat direction,
                               CFTimeInterval duration) {
    CAShapeLayer *ripple = [CAShapeLayer layer];
    ripple.name = @"cleanup-impact-ripple";
    ripple.bounds = CGRectMake(0, 0, 150, 26);
    ripple.position = CGPointMake(x, ground - direction * 4);
    CGPathRef ellipse = CGPathCreateWithEllipseInRect(CGRectMake(2, 2, 146, 22), NULL);
    ripple.path = ellipse;
    CGPathRelease(ellipse);
    ripple.fillColor = NSColor.clearColor.CGColor;
    ripple.strokeColor = [NSColor colorWithRed:.75 green:.93 blue:1 alpha:.85].CGColor;
    ripple.lineWidth = 2;
    [parent addSublayer:ripple];
    NSArray *rippleTimes = @[@0, @.10, @.12, @.23, @1];
    [ripple addAnimation:cleanup_keyframes(@"opacity", @[@0, @0, @.8, @0, @0], rippleTimes, duration) forKey:@"fade"];
    [ripple addAnimation:cleanup_keyframes(@"transform.scale", @[@.12, @.12, @.28, @1.45, @1.45], rippleTimes,
                                           duration) forKey:@"spread"];

    CALayer *ice = [CALayer layer];
    ice.name = @"cleanup-impact";
    ice.bounds = CGRectMake(0, 0, 34, 33);
    NSArray<NSColor *> *colors = @[[NSColor colorWithRed:.56 green:.80 blue:.91 alpha:1],
        [NSColor colorWithRed:.85 green:.96 blue:1 alpha:1], [NSColor colorWithRed:.29 green:.61 blue:.77 alpha:1]];
    const CGPoint faces[][6] = {
        {{2,13},{12,3},{27,5},{33,18},{23,31},{7,28}},
        {{2,13},{12,3},{27,5},{21,15},{8,18},{2,13}},
        {{21,15},{27,5},{33,18},{23,31},{15,26},{21,15}}
    };
    for (NSUInteger i = 0; i < 3; i++) {
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathAddLines(path, NULL, faces[i], 6);
        CGPathCloseSubpath(path);
        CAShapeLayer *face = [CAShapeLayer layer];
        face.path = path;
        CGPathRelease(path);
        face.fillColor = colors[i].CGColor;
        [ice addSublayer:face];
    }
    [parent addSublayer:ice];
    NSArray *times = @[@0, @.10, @.16, @.29];
    animate_cleanup(ice, @[[NSValue valueWithPoint:CGPointMake(x + 25, ground - direction * 340)],
                          [NSValue valueWithPoint:CGPointMake(x, ground - direction * 16)],
                          [NSValue valueWithPoint:CGPointMake(x - 11, ground - direction * 62)],
                          [NSValue valueWithPoint:CGPointMake(x - 27, ground - direction * 14)]],
                    @[@1, @1, @.9, @0], times, duration);
    [ice addAnimation:cleanup_keyframes(@"transform.rotation.z", @[@.35, @0, @(-.55), @(-1.15)], times,
                                        duration) forKey:@"tumble"];
}

static CALayer *cleanup_wheel(CGFloat x) {
    CALayer *wheel = [CALayer layer];
    wheel.name = @"plow-wheel";
    wheel.frame = CGRectMake(x, 42, 18, 18);
    wheel.cornerRadius = 9;
    wheel.backgroundColor = [NSColor colorWithWhite:.13 alpha:1].CGColor;
    actor_part(wheel, CGRectMake(3, 3, 12, 12), [NSColor colorWithWhite:.31 alpha:1], 6);
    actor_part(wheel, CGRectMake(7, 3, 4, 12), [NSColor colorWithWhite:.60 alpha:1], 2);
    actor_part(wheel, CGRectMake(3, 7, 12, 4), [NSColor colorWithWhite:.60 alpha:1], 2);
    actor_part(wheel, CGRectMake(7, 7, 4, 4), NSColor.systemGrayColor, 2);
    return wheel;
}

static CALayer *cleanup_boot(CGFloat x, NSColor *cloth) {
    CALayer *leg = [CALayer layer];
    leg.name = @"worker-leg";
    leg.bounds = CGRectMake(0, 0, 12, 20);
    leg.anchorPoint = CGPointMake(.45, .12);
    leg.position = CGPointMake(x, 41);
    actor_part(leg, CGRectMake(2, 0, 7, 15), cloth, 3);
    actor_part(leg, CGRectMake(1, 13, 11, 7), [NSColor colorWithWhite:.18 alpha:1], 3);
    actor_part(leg, CGRectMake(1, 18, 11, 2), [NSColor colorWithWhite:.43 alpha:1], 1);
    return leg;
}

static CALayer *cleanup_actor(SnowCleanup style, BOOL inverted) {
    CALayer *actor = [CALayer layer];
    actor.bounds = CGRectMake(0, 0, 96, 62);
    NSColor *glass = [NSColor colorWithRed:.65 green:.89 blue:1 alpha:1];
    if (style == SnowCleanupPlow) {
        NSColor *orange = [NSColor colorWithRed:.89 green:.41 blue:.15 alpha:1];
        NSColor *trim = [NSColor colorWithRed:.99 green:.63 blue:.25 alpha:1];
        actor_part(actor, CGRectMake(4, 29, 72, 23), [NSColor colorWithWhite:.20 alpha:1], 5);
        actor_part(actor, CGRectMake(7, 24, 68, 21), orange, 5);
        actor_part(actor, CGRectMake(9, 26, 35, 5), trim, 2);
        actor_part(actor, CGRectMake(44, 11, 28, 30), orange, 5);
        actor_part(actor, CGRectMake(45, 10, 28, 4), trim, 2);
        actor_part(actor, CGRectMake(49, 16, 19, 14), [NSColor colorWithRed:.20 green:.38 blue:.48 alpha:1], 3);
        actor_part(actor, CGRectMake(51, 17, 15, 9), glass, 2);
        actor_part(actor, CGRectMake(56, 16, 2, 13), [NSColor colorWithRed:.24 green:.46 blue:.55 alpha:1], 1);
        actor_part(actor, CGRectMake(63, 33, 5, 2), [NSColor colorWithWhite:.24 alpha:1], 1);
        actor_part(actor, CGRectMake(53, 6, 9, 5), NSColor.systemYellowColor, 2);
        actor_part(actor, CGRectMake(69, 33, 7, 5), [NSColor colorWithRed:1 green:.88 blue:.52 alpha:1], 2);
        actor_part(actor, CGRectMake(6, 36, 4, 5), NSColor.systemRedColor, 1);
        [actor addSublayer:cleanup_wheel(15)];
        [actor addSublayer:cleanup_wheel(55)];
        actor_part(actor, CGRectMake(72, 44, 11, 5), [NSColor colorWithWhite:.28 alpha:1], 2);
        actor_part(actor, CGRectMake(78, 36, 15, 23), [NSColor colorWithRed:.69 green:.19 blue:.23 alpha:1], 4);
        actor_part(actor, CGRectMake(80, 39, 9, 3), [NSColor colorWithRed:.94 green:.40 blue:.42 alpha:1], 1);
        actor_part(actor, CGRectMake(79, 55, 14, 4), [NSColor colorWithWhite:.64 alpha:1], 1);
    } else {
        NSColor *jacket = [NSColor colorWithRed:.18 green:.47 blue:.62 alpha:1];
        NSColor *wool = [NSColor colorWithRed:.77 green:.19 blue:.32 alpha:1];
        NSColor *pants = [NSColor colorWithRed:.18 green:.24 blue:.33 alpha:1];
        [actor addSublayer:cleanup_boot(39, pants)];
        [actor addSublayer:cleanup_boot(49, pants)];
        actor_part(actor, CGRectMake(30, 23, 25, 24), jacket, 7);
        actor_part(actor, CGRectMake(35, 27, 2, 15), [NSColor colorWithRed:.50 green:.71 blue:.79 alpha:1], 1);
        actor_part(actor, CGRectMake(33, 9, 20, 19), [NSColor colorWithRed:1 green:.78 blue:.62 alpha:1], 9);
        actor_part(actor, CGRectMake(48, 17, 6, 5), [NSColor colorWithRed:.99 green:.70 blue:.53 alpha:1], 2);
        actor_part(actor, CGRectMake(47, 15, 2, 3), NSColor.blackColor, 1);
        actor_part(actor, CGRectMake(30, 5, 24, 10), wool, 5);
        actor_part(actor, CGRectMake(39, 0, 8, 8), NSColor.whiteColor, 4);
        actor_part(actor, CGRectMake(33, 23, 23, 6), wool, 3);
        actor_part(actor, CGRectMake(48, 26, 6, 10), wool, 2);
        actor_part(actor, CGRectMake(49, 29, 16, 7), jacket, 3);
        actor_part(actor, CGRectMake(60, 29, 7, 7), wool, 3);
        CALayer *shovel = [CALayer layer];
        shovel.bounds = CGRectMake(0, 0, 26, 28);
        shovel.anchorPoint = CGPointZero;
        shovel.position = CGPointMake(59, 33);
        [actor addSublayer:shovel];
        actor_part(shovel, CGRectMake(0, 0, 21, 4), NSColor.systemBrownColor, 2);
        actor_part(shovel, CGRectMake(16, 0, 11, 8), [NSColor colorWithRed:.25 green:.47 blue:.56 alpha:1], 3);
        actor_part(shovel, CGRectMake(19, 1, 7, 2), glass, 1);
    }
    if (inverted) actor.affineTransform = CGAffineTransformMakeScale(1, -1);
    return actor;
}

enum { CLEANUP_SNOW_SAMPLES = 96 };

static void cleanup_snow_profile(const SnowScene *scene, CGFloat profile[CLEANUP_SNOW_SAMPLES]) {
    for (size_t i = 0; i < scene->drift_count; i++) {
        const SnowDrift *drift = &scene->drifts[i];
        for (size_t s = 0; s < drift->span_count; s++) {
            SnowSpan span = drift->spans[s];
            for (size_t j = 0; j < CLEANUP_SNOW_SAMPLES; j++) {
                float x = scene->screen.x + (j + .5f) * scene->screen.width / CLEANUP_SNOW_SAMPLES;
                if (x >= span.left && x < span.right) {
                    profile[j] = MIN(36, profile[j] + snow_depth(drift, span, x));
                }
            }
        }
    }
}

static CGFloat cleanup_snow_load(const CGFloat *profile, CGFloat width, CGFloat x) {
    if (x < 0 || x >= width || width <= 0) return 0;
    CGFloat index = MAX(0, MIN(CLEANUP_SNOW_SAMPLES - 1, x / width * CLEANUP_SNOW_SAMPLES - .5));
    size_t left = (size_t)index, right = MIN(left + 1, CLEANUP_SNOW_SAMPLES - 1);
    return profile[left] + (profile[right] - profile[left]) * (index - left);
}

static void cleanup_particle(CALayer *group, CGSize size, CGColorRef color, CGPoint from, CGPoint apex, CGPoint to,
                              CGFloat start, CGFloat end, CFTimeInterval duration) {
    CALayer *particle = [CALayer layer];
    particle.bounds = CGRectMake(0, 0, size.width, size.height);
    particle.cornerRadius = size.height * .5;
    particle.backgroundColor = color;
    if (size.width > 2 * size.height) {
        particle.affineTransform = CGAffineTransformMakeRotation(atan2(apex.y - from.y, apex.x - from.x));
    }
    particle.position = from;
    particle.opacity = 0;
    [group addSublayer:particle];
    animate_cleanup(particle, @[[NSValue valueWithPoint:from], [NSValue valueWithPoint:from],
                                [NSValue valueWithPoint:apex], [NSValue valueWithPoint:to],
                                [NSValue valueWithPoint:to]],
                    @[@0, @0, @.93, @0, @0], @[@0, @(start), @((start + end) * .5), @(end), @1], duration);
}

static CGFloat sweep_tool_x(CGFloat width, CGFloat progress, BOOL plow) {
    if (!plow) {
        CGFloat step = progress * 9;
        progress = (floor(step) + MIN(1, (step - floor(step)) / .68)) / 9;
    }
    return -18 + (width + 115) * progress;
}

static void cleanup_ground_pass(CALayer *effect, CALayer *banks, SnowCleanup style, const CGFloat *profile,
                                CGFloat width, CGFloat ground, CGFloat direction, CFTimeInterval duration) {
    BOOL plow = style == SnowCleanupPlow;
    CGFloat passStart = .39;
    CALayer *actor = cleanup_actor(style, direction < 0);
    actor.name = @"cleanup-actor";
    CAShapeLayer *pile = [CAShapeLayer layer];
    pile.name = @"cleanup-pile";
    pile.fillColor = [NSColor colorWithRed:.91 green:.96 blue:1 alpha:1].CGColor;
    CALayer *spray = [CALayer layer];
    spray.name = @"cleanup-spray";
    [effect addSublayer:pile];
    [effect addSublayer:spray];
    [effect addSublayer:actor];

    NSMutableArray<NSNumber *> *times = [NSMutableArray arrayWithObject:@0];
    NSMutableArray<NSNumber *> *edges = [NSMutableArray array];
    NSMutableArray<NSValue *> *bankPositions = [NSMutableArray array];
    NSMutableArray<NSValue *> *actorPositions = [NSMutableArray array];
    NSMutableArray<NSValue *> *pilePositions = [NSMutableArray array];
    NSMutableArray *pilePaths = [NSMutableArray array];
    NSMutableArray<NSNumber *> *angles = [NSMutableArray array];
    NSMutableArray<NSNumber *> *gait = [NSMutableArray array];
    NSMutableArray<NSNumber *> *oppositeGait = [NSMutableArray array];
    for (int i = -1; i <= 108; i++) {
        CGFloat progress = MAX(0, i / 108.0);
        if (i >= 0) [times addObject:@(passStart + (1 - passStart) * progress)];
        CGFloat tool = sweep_tool_x(width, progress, plow);
        CGFloat load = cleanup_snow_load(profile, width, tool);
        // Use the same leading edge for the cleared area, the tool and the pile.
        [edges addObject:@(tool)];
        [bankPositions addObject:[NSValue valueWithPoint:CGPointMake(tool, 0)]];
        [actorPositions addObject:[NSValue valueWithPoint:CGPointMake(tool - (plow ? 45 : 26), ground - direction * 30)]];
        [pilePositions addObject:[NSValue valueWithPoint:CGPointMake(tool, ground)]];
        CGFloat height = load > .12 ? MIN(plow ? 39 : 19, load * (plow ? 1.8 : .85)) : 0;
        CGFloat length = height > 0 ? (plow ? 15 : 8) + height * 1.7 : 0;
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathMoveToPoint(path, NULL, 0, 0);
        CGPathAddCurveToPoint(path, NULL, 1, -height * .94 * direction, length * .26, -height * 1.17 * direction,
                              length * .44, -height * .53 * direction);
        CGPathAddQuadCurveToPoint(path, NULL, length * .81, -height * .22 * direction, length, 0);
        CGPathCloseSubpath(path);
        [pilePaths addObject:CFBridgingRelease(path)];
        if (!plow) {
            CGFloat step = progress * 9;
            CGFloat cycle = step - floor(step);
            CGFloat lift = sin(M_PI * MIN(1, MAX(0, (cycle - .54) / .46)));
            [angles addObject:@(.98 - 1.86 * lift)];
            CGFloat stride = cycle < .68 ? sin(cycle / .68 * 2 * M_PI) * .44 : 0;
            [gait addObject:@(stride)];
            [oppositeGait addObject:@(-stride)];
        }
    }
    banks.anchorPoint = CGPointZero;
    banks.position = bankPositions.firstObject.pointValue;
    banks.bounds = CGRectMake(edges.firstObject.doubleValue, 0, width + 20, CGRectGetHeight(banks.bounds));
    banks.masksToBounds = YES;
    actor.position = actorPositions.firstObject.pointValue;
    pile.position = pilePositions.firstObject.pointValue;
    pile.path = (__bridge CGPathRef)pilePaths.firstObject;
    [banks addAnimation:cleanup_keyframes(@"position", bankPositions, times, duration) forKey:@"position"];
    [banks addAnimation:cleanup_keyframes(@"bounds.origin.x", edges, times, duration) forKey:@"sweep"];
    [actor addAnimation:cleanup_keyframes(@"position", actorPositions, times, duration) forKey:@"position"];
    [pile addAnimation:cleanup_keyframes(@"position", pilePositions, times, duration) forKey:@"position"];
    [pile addAnimation:cleanup_keyframes(@"path", pilePaths, times, duration) forKey:@"pile-shape"];
    if (plow) {
        for (CALayer *wheel in actor.sublayers) {
            if (![wheel.name isEqualToString:@"plow-wheel"]) continue;
            [wheel addAnimation:cleanup_keyframes(@"transform.rotation.z", @[@0, @(width / 9)], @[@.0, @1],
                                                   duration) forKey:@"wheel-roll"];
        }
    } else {
        [actor.sublayers.lastObject addAnimation:cleanup_keyframes(@"transform.rotation.z", angles, times, duration)
                                         forKey:@"shovel-swing"];
        NSUInteger leg = 0;
        for (CALayer *part in actor.sublayers) {
            if (![part.name isEqualToString:@"worker-leg"]) continue;
            [part addAnimation:cleanup_keyframes(@"transform.rotation.z", leg++ ? gait : oppositeGait, times, duration)
                        forKey:@"walking"];
        }
    }

    NSColor *snowColor = [NSColor colorWithRed:.91 green:.97 blue:1 alpha:1];
    int bursts = plow ? 21 : 9;
    for (int i = 0; i < bursts; i++) {
        CGFloat progress = (i + (plow ? .38 : .77)) / bursts;
        CGFloat tool = sweep_tool_x(width, progress, plow);
        CGFloat load = cleanup_snow_load(profile, width, tool);
        if (load < .6) continue;
        CGFloat start = passStart + progress * (1 - passStart);
        for (int j = 0; j < (plow ? 4 : 5); j++) {
            CGFloat size = 2.2 + (j % 3) * 1.4;
            CGFloat forward = 16 + j * 10 + (i % 3) * 4;
            CGFloat loft = MIN(58, 14 + j * 5 + load * 1.15);
            CGFloat end = MIN(.999, start + (.28 + j * .027) / duration);
            if (end <= start) continue;
            // Snow pushed out by the plow keeps its forward speed as it leaves the blade.
            CGFloat travel = plow ? sweep_tool_x(width, (end - passStart) / (1 - passStart), YES) - tool : 0;
            cleanup_particle(spray, CGSizeMake(size, size), snowColor.CGColor,
                             CGPointMake(tool + 5 + j * 2, ground - direction * 8),
                             CGPointMake(tool + travel * .5 + forward, ground - direction * loft),
                             CGPointMake(tool + travel + forward * 1.62, ground - direction * 3),
                             start, end, duration);
        }
    }
}

static void cleanup_drift_particles(CALayer *particles, SnowCleanup style, CGRect drift, CGFloat y, CGFloat fall,
                                    CGFloat direction, CGFloat width, CFTimeInterval duration) {
    NSColor *tint = style == SnowCleanupThaw ? [NSColor colorWithRed:.62 green:.85 blue:1 alpha:.85] : NSColor.whiteColor;
    int count = style == SnowCleanupWind ? MAX(3, MIN(24, (int)(CGRectGetWidth(drift) / 85))) :
        MAX(3, MIN(9, (int)(CGRectGetWidth(drift) / 37)));
    for (int i = 0; i < count && particles.sublayers.count < 48; i++) {
        CGFloat x = CGRectGetMinX(drift) + (i + .5) / count * CGRectGetWidth(drift);
        CGFloat start = .10 + (i % 3) * .055, end = .78 + (i % 3) * .055;
        CGPoint from = CGPointMake(x, y);
        CGPoint apex, to;
        CGSize size = CGSizeMake(3 + i % 3, 3 + i % 3);
        if (style == SnowCleanupWind) {
            start = .08 + .53 * MAX(0, MIN(1, x / MAX(1, width)));
            end = MIN(.98, start + .38);
            size.width *= 3;
            apex = CGPointMake(x + width * .22, y - direction * (17 + (i % 3) * 7));
            to = CGPointMake(x + width * .65, y - direction * (34 + (i % 3) * 12));
        } else if (style == SnowCleanupAvalanche) {
            size = CGSizeMake(4 + (i % 4) * 1.8, 3 + i % 4);
            start = .10 + (i % 5) * .052;
            end = .62 + (i % 5) * .062;
            apex = CGPointMake(x + 14 + i * 7, y + fall * .22);
            to = CGPointMake(x + 39 + i * 9, y + fall);
        } else {
            size.width = 2.2;
            size.height = 4 + i % 4;
            apex = CGPointMake(x, y + direction * 5);
            to = CGPointMake(x + i * 3, y + direction * (14 + i * 8));
        }
        cleanup_particle(particles, size, tint.CGColor, from, apex, to, start, end, duration);
    }
}

static void cleanup_wind_erosion(CAShapeLayer *bank, const SnowScene *scene, const SnowDrift *drift,
                                  CGFloat baseline, CGFloat direction, CFTimeInterval duration) {
    NSMutableArray *paths = [NSMutableArray array];
    NSMutableArray<NSNumber *> *times = [NSMutableArray array];
    CGFloat origin = drift->spans[0].left;
    CGFloat step = MAX(3, drift->surface.rect.width / 104);
    for (int frame = 0; frame <= 10; frame++) {
        CGFloat progress = frame / 10.0;
        [times addObject:@(progress)];
        CGMutablePathRef path = CGPathCreateMutable();
        for (size_t s = 0; s < drift->span_count; s++) {
            SnowSpan span = drift->spans[s];
            CGPathMoveToPoint(path, NULL, span.left - origin, baseline);
            for (CGFloat x = span.left + step; x < span.right; x += step) {
                CGFloat screenFraction = (x - scene->screen.x) / MAX(1, scene->screen.width);
                CGFloat arrives = .07 + .57 * screenFraction + .055 * sin(x * .14);
                CGFloat remaining = 1 - MAX(0, MIN(1, (progress - arrives) / .25));
                CGFloat depth = snow_depth(drift, span, x) * remaining;
                CGPathAddLineToPoint(path, NULL, x - origin, baseline - direction * depth);
            }
            CGPathAddLineToPoint(path, NULL, span.right - origin, baseline);
            CGPathCloseSubpath(path);
        }
        [paths addObject:CFBridgingRelease(path)];
    }
    [bank addAnimation:cleanup_keyframes(@"path", paths, times, duration) forKey:@"wind-erosion"];
}
