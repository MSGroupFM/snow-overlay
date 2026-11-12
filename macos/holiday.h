#pragma once

static NSImage *holiday_santa_image(void) {
    static NSImage *image;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSURL *url = [NSBundle.mainBundle URLForResource:@"santa-flight" withExtension:@"png"];
        if (url) image = [[NSImage alloc] initWithContentsOfURL:url];
    });
    return image;
}

static NSArray *holiday_santa_frames(void) {
    static NSArray *frames;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSURL *url = [NSBundle.mainBundle URLForResource:@"santa-frames" withExtension:@"png"];
        NSImage *sheet = url ? [[NSImage alloc] initWithContentsOfURL:url] : nil;
        CGImageRef image = [sheet CGImageForProposedRect:NULL context:nil hints:nil];
        if (!image) return;
        size_t width = CGImageGetWidth(image), height = CGImageGetHeight(image);
        NSMutableArray *tiles = [NSMutableArray arrayWithCapacity:4];
        for (NSUInteger i = 0; i < 4; i++) {
            size_t top = height * i / 4, bottom = height * (i + 1) / 4;
            CGImageRef tile = CGImageCreateWithImageInRect(image, CGRectMake(floor(width * .083), top,
                                                           floor(width * .84), bottom - top));
            if (tile) [tiles addObject:CFBridgingRelease(tile)];
        }
        frames = tiles.copy;
    });
    return frames;
}

static CGFloat holiday_flight_duration(CGFloat width) {
    return fmin(12, fmax(7, width / 300 + 3));
}

static CGPoint holiday_flight_position(CGSize size, CGFloat progress, BOOL inverted) {
    CGFloat direction = inverted ? -1 : 1;
    CGFloat height = size.height * (inverted ? .76 : .24);
    CGFloat ripple = sin(progress * 4 * M_PI) * 19 + sin(progress * 11 * M_PI) * 6;
    return CGPointMake(-185 + progress * (size.width + 370), height + ripple * direction);
}

static CALayer *holiday_santa_sprite(void) {
    CALayer *sprite = [CALayer layer];
    sprite.name = @"santa-sleigh";
    sprite.bounds = CGRectMake(0, 0, 330, 110);
    NSImage *image = holiday_santa_image();
    if (image) {
        sprite.contents = (__bridge id)[image CGImageForProposedRect:NULL context:nil hints:nil];
        sprite.contentsGravity = kCAGravityResizeAspect;
    } else {
        // The stand-alone test target has no application resource bundle.
        actor_part(sprite, CGRectMake(13, 57, 116, 35), NSColor.systemRedColor, 15);
        actor_part(sprite, CGRectMake(18, 92, 127, 4), NSColor.systemYellowColor, 2);
        actor_part(sprite, CGRectMake(35, 27, 45, 48), NSColor.systemRedColor, 17);
        actor_part(sprite, CGRectMake(47, 24, 32, 26), NSColor.whiteColor, 12);
        for (int i = 0; i < 2; i++) {
            actor_part(sprite, CGRectMake(153 + i * 79, 40, 67, 28), NSColor.systemBrownColor, 12);
            actor_part(sprite, CGRectMake(195 + i * 79, 30, 20, 23), NSColor.systemBrownColor, 7);
        }
    }
    return sprite;
}

static CALayer *holiday_gift(unsigned variant) {
    NSColor *paper[] = {
        [NSColor colorWithRed:.79 green:.17 blue:.30 alpha:1],
        [NSColor colorWithRed:.14 green:.54 blue:.49 alpha:1],
        [NSColor colorWithRed:.30 green:.40 blue:.73 alpha:1],
        [NSColor colorWithRed:.74 green:.39 blue:.22 alpha:1]
    };
    NSColor *ribbon = [NSColor colorWithRed:1 green:.79 blue:.43 alpha:1];
    CALayer *gift = [CALayer layer];
    gift.bounds = CGRectMake(0, 0, 21, 24);
    gift.name = @"santa-gift";
    actor_part(gift, CGRectMake(2, 9, 17, 14), paper[variant % 4], 3);
    actor_part(gift, CGRectMake(1, 8, 19, 5), [paper[variant % 4] blendedColorWithFraction:.2 ofColor:NSColor.whiteColor], 2);
    actor_part(gift, CGRectMake(9, 8, 3, 15), ribbon, 1);
    actor_part(gift, CGRectMake(4, 4, 7, 5), ribbon, 2);
    actor_part(gift, CGRectMake(11, 3, 6, 6), ribbon, 2);
    return gift;
}

static CGFloat holiday_gift_landing(const SnowScene *scene, CGFloat localX, CGFloat localY) {
    float x = scene->screen.x + localX;
    float y = snow_screen_y(scene, scene->screen.y + localY);
    float landing = scene->screen.y + scene->screen.height - 1;
    for (size_t i = 0; i < scene->drift_count; i++) {
        const SnowDrift *drift = &scene->drifts[i];
        for (size_t j = 0; j < drift->span_count; j++) {
            SnowSpan span = drift->spans[j];
            if (x < span.left || x >= span.right) continue;
            float top = drift->surface.rect.y - snow_depth(drift, span, x);
            if (top >= y + 24 && top < landing) landing = top;
            break;
        }
    }
    landing = fmaxf(y + 12, landing - 11);
    return snow_screen_y(scene, landing) - scene->screen.y;
}

static CALayer *holiday_flight_layer(const SnowScene *scene, CGSize size, CGFloat duration, BOOL reducedMotion) {
    CALayer *flight = [CALayer layer];
    flight.name = @"santa-flight";
    flight.frame = CGRectMake(0, 0, size.width, size.height);
    CALayer *sprite = holiday_santa_sprite();
    sprite.position = holiday_flight_position(size, 0, scene->inverted);
    [flight addSublayer:sprite];
    NSMutableArray *positions = [NSMutableArray array], *times = [NSMutableArray array], *angles = [NSMutableArray array];
    for (int i = 0; i <= 48; i++) {
        CGFloat progress = i / 48.0;
        CGPoint point = holiday_flight_position(size, progress, scene->inverted);
        if (reducedMotion) point.y = size.height * (scene->inverted ? .76 : .24);
        [positions addObject:[NSValue valueWithPoint:point]];
        [times addObject:@(progress)];
        [angles addObject:@(sin(progress * 4 * M_PI + .7) * .038)];
    }
    [sprite addAnimation:cleanup_keyframes(@"position", positions, times, duration) forKey:@"flight"];
    if (!reducedMotion) {
        [sprite addAnimation:cleanup_keyframes(@"transform.rotation.z", angles, times, duration) forKey:@"rocking"];
        NSArray *frames = holiday_santa_frames();
        if (frames.count == 4) {
            sprite.contents = frames.firstObject;
            CAKeyframeAnimation *gallop = cleanup_keyframes(@"contents", [frames arrayByAddingObject:frames.firstObject],
                                                           @[@0, @.25, @.5, @.75, @1], .52);
            gallop.calculationMode = kCAAnimationDiscrete;
            gallop.repeatDuration = duration;
            [sprite addAnimation:gallop forKey:@"gallop"];
        }
    }

    CGFloat direction = scene->inverted ? -1 : 1;
    for (int i = 0; i < 7; i++) {
        CGFloat drop = .18 + i * .091;
        CGPoint point = holiday_flight_position(size, drop, scene->inverted);
        if (reducedMotion) point.y = size.height * (scene->inverted ? .76 : .24);
        point.x -= 88;
        point.y += direction * 28;
        if (point.x < 0 || point.x >= size.width) continue;
        CGFloat endX = fmin(size.width - 14, point.x + 45 + (i % 3) * 19);
        CGFloat endY = holiday_gift_landing(scene, endX, point.y);
        CGFloat land = drop + fmin(.14, .11 + fabs(endY - point.y) / 8500);
        CGFloat bounce = land + .026, rest = land + .05;
        CGFloat fade = fmin(.99, rest + .09);
        CALayer *gift = holiday_gift(i);
        gift.position = point;
        gift.opacity = 0;
        [flight insertSublayer:gift below:sprite];
        NSArray *phases = @[@0, @(drop), @(drop + .02), @(land), @(bounce), @(rest), @(fade), @1];
        CGPoint halfway = CGPointMake(point.x + (endX - point.x) * .45, point.y + direction * 17);
        CGPoint floor = CGPointMake(endX, endY);
        animate_cleanup(gift, @[[NSValue valueWithPoint:point], [NSValue valueWithPoint:point],
                                 [NSValue valueWithPoint:halfway], [NSValue valueWithPoint:floor],
                                 [NSValue valueWithPoint:CGPointMake(endX + 4, endY - direction * 9)],
                                 [NSValue valueWithPoint:CGPointMake(endX + 8, endY)],
                                 [NSValue valueWithPoint:CGPointMake(endX + 8, endY)],
                                 [NSValue valueWithPoint:CGPointMake(endX + 8, endY)]],
                        @[@0, @0, @1, @1, @1, @1, @0, @0], phases, duration);
        if (!reducedMotion) {
            [gift addAnimation:cleanup_keyframes(@"transform.rotation.z",
                @[@0, @0, @.19, @1.12, @.82, @.61, @.61, @.61], phases, duration) forKey:@"tumble"];
        }
    }
    if (!reducedMotion) {
        NSColor *gold = [NSColor colorWithRed:1 green:.81 blue:.52 alpha:.85];
        for (int i = 0; i < 20; i++) {
            CGFloat start = .14 + i * .038;
            CGPoint p = holiday_flight_position(size, start, scene->inverted);
            p.x -= 154;
            p.y += ((i % 5) - 2) * 6;
            cleanup_particle(flight, CGSizeMake(3 + i % 3, 3 + i % 3), gold.CGColor, p,
                CGPointMake(p.x - 13, p.y + direction * 5), CGPointMake(p.x - 32, p.y + direction * 12),
                start, start + .064, duration);
        }
    }
    return flight;
}

static void holiday_shape(CGContextRef ctx, const CGPoint *points, size_t count) {
    CGContextAddLines(ctx, points, count);
    CGContextClosePath(ctx);
    CGContextFillPath(ctx);
}

static CGImageRef snowball_texture(void) {
    static CGImageRef image;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(NULL, 128, 128, 8, 0, color,
                                                  (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
        if (ctx) {
            CGRect circle = CGRectMake(1, 1, 126, 126);
            CGContextAddEllipseInRect(ctx, circle);
            CGContextClip(ctx);
            const CGFloat shades[] = {1, 1, 1, 1, .95, .98, 1, 1, .80, .89, .97, 1, .58, .72, .85, 1};
            const CGFloat stops[] = {0, .38, .76, 1};
            CGGradientRef light = CGGradientCreateWithColorComponents(color, shades, stops, 4);
            CGContextDrawRadialGradient(ctx, light, CGPointMake(40, 88), 3, CGPointMake(65, 65), 70,
                                        kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
            CGGradientRelease(light);
            for (int i = 0; i < 94; i++) {
                CGFloat x = (i * 53) % 127, y = (i * 71) % 127;
                CGContextSetRGBFillColor(ctx, .44, .62, .80, i % 3 == 0 ? .23 : .12);
                CGContextFillEllipseInRect(ctx, CGRectMake(x, y, 1.7 + i % 3, 1.5 + i % 2));
                CGContextSetRGBFillColor(ctx, 1, 1, 1, .53);
                CGContextFillEllipseInRect(ctx, CGRectMake(x + 1, y + 1, 1.5, 1.5));
            }
            image = CGBitmapContextCreateImage(ctx);
            CGContextRelease(ctx);
        }
        CGColorSpaceRelease(color);
    });
    return image;
}

static void holiday_coat(CGContextRef ctx, unsigned appearance) {
    static const CGFloat colors[SNOWMAN_APPEARANCES][3] = {
        {.31, .45, .54}, {.78, .17, .29}, {.14, .46, .37},
        {.18, .45, .57}, {.81, .47, .15}, {.49, .31, .63}
    };
    const CGFloat *rgb = colors[appearance % SNOWMAN_APPEARANCES];
    CGContextSetRGBFillColor(ctx, rgb[0], rgb[1], rgb[2], 1);
}

static void snowman_hat(CGContextRef ctx, unsigned appearance) {
    holiday_coat(ctx, appearance);
    if (appearance == 0) {
        const CGPoint bucket[] = {{38, 12}, {89, 12}, {82, 42}, {44, 42}};
        holiday_shape(ctx, bucket, 4);
        CGContextSetRGBFillColor(ctx, .49, .65, .74, 1);
        CGContextFillEllipseInRect(ctx, CGRectMake(37, 9, 53, 8));
        CGContextFillRect(ctx, CGRectMake(48, 18, 6, 17));
        CGContextSetRGBStrokeColor(ctx, .30, .37, .44, 1);
        CGContextSetLineWidth(ctx, 2.5);
        CGContextMoveToPoint(ctx, 43, 24);
        CGContextAddQuadCurveToPoint(ctx, 24, 32, 45, 42);
        CGContextStrokePath(ctx);
        CGContextSetRGBFillColor(ctx, .80, .89, .95, 1);
        CGContextFillEllipseInRect(ctx, CGRectMake(40, 22, 5, 5));
        CGContextFillEllipseInRect(ctx, CGRectMake(81, 22, 4, 5));
    } else if (appearance == 3) {
        CGContextSetRGBFillColor(ctx, .13, .20, .27, 1);
        CGContextFillRect(ctx, CGRectMake(44, 12, 40, 29));
        CGContextFillEllipseInRect(ctx, CGRectMake(35, 35, 58, 11));
        holiday_coat(ctx, appearance);
        CGContextFillRect(ctx, CGRectMake(44, 31, 40, 6));
        CGContextSetRGBFillColor(ctx, .84, .68, .34, 1);
        CGContextFillRect(ctx, CGRectMake(71, 31, 8, 6));
        holiday_coat(ctx, appearance);
        CGContextFillRect(ctx, CGRectMake(73, 32, 4, 4));
    } else if (appearance == 4) {
        CGContextSetRGBStrokeColor(ctx, .24, .31, .39, 1);
        CGContextSetLineWidth(ctx, 5);
        CGContextAddArc(ctx, 64, 51, 30, M_PI, 2 * M_PI, 0);
        CGContextStrokePath(ctx);
        holiday_coat(ctx, appearance);
        CGContextFillEllipseInRect(ctx, CGRectMake(29, 47, 14, 20));
        CGContextFillEllipseInRect(ctx, CGRectMake(85, 47, 14, 20));
        CGContextSetRGBFillColor(ctx, 1, .87, .61, .60);
        CGContextFillEllipseInRect(ctx, CGRectMake(31, 50, 8, 12));
        CGContextFillEllipseInRect(ctx, CGRectMake(87, 50, 8, 12));
    } else {
        CGContextFillEllipseInRect(ctx, CGRectMake(39, 15, 50, 39));
        CGContextFillRect(ctx, CGRectMake(37, 36, 54, 10));
        CGContextSetRGBFillColor(ctx, 1, .97, .89, 1);
        CGContextFillEllipseInRect(ctx, CGRectMake(58, 9, 13, 13));
        CGContextSetRGBFillColor(ctx, 1, 1, 1, .43);
        for (int i = 0; i < 7; i++) CGContextFillRect(ctx, CGRectMake(42 + i * 7, 37, 2, 7));
        for (int i = 0; i < 5; i++) CGContextFillEllipseInRect(ctx, CGRectMake(48 + i * 7, 27 + abs(i - 2), 3, 2));
    }
}

static CGImageRef snowman_details(SnowmanPart part, unsigned appearance, BOOL mischievous) {
    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, 128, 128, 8, 0, color,
                                              (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(color);
    if (!ctx) return NULL;
    CGContextTranslateCTM(ctx, 0, 128);
    CGContextScaleCTM(ctx, 1, -1);
    if (part == SNOWMAN_HEAD) {
        snowman_hat(ctx, appearance);
        CGContextSetLineCap(ctx, kCGLineCapRound);
        if (mischievous) {
            for (int i = 0; i < 2; i++) {
                CGFloat eye = 44 + i * 26;
                CGContextSetRGBFillColor(ctx, .97, .10, .16, .30);
                CGContextFillEllipseInRect(ctx, CGRectMake(eye - 2, 49, 21, 18));
                CGContextSetRGBFillColor(ctx, .37, .06, .10, 1);
                CGContextFillEllipseInRect(ctx, CGRectMake(eye, 50, 16, 13));
                CGContextSetRGBFillColor(ctx, 1, .06, .12, 1);
                CGContextFillEllipseInRect(ctx, CGRectMake(eye + 2, 52, 12, 9));
                CGContextSetRGBFillColor(ctx, 1, .80, .67, 1);
                CGContextFillEllipseInRect(ctx, CGRectMake(eye + 5, 53, 4, 3));
            }
            CGContextSetRGBStrokeColor(ctx, .22, .08, .14, 1);
            CGContextSetLineWidth(ctx, 4);
            CGContextMoveToPoint(ctx, 44, 46);
            CGContextAddLineToPoint(ctx, 58, 53);
            CGContextMoveToPoint(ctx, 71, 53);
            CGContextAddLineToPoint(ctx, 85, 46);
            CGContextStrokePath(ctx);
            CGContextSetRGBFillColor(ctx, .24, .09, .16, 1);
            holiday_shape(ctx, (CGPoint[]){{51,73},{59,75},{70,75},{81,71},{74,81},{62,84},{54,79}}, 7);
            CGContextSetRGBFillColor(ctx, .97, .98, 1, 1);
            holiday_shape(ctx, (CGPoint[]){{56,75},{62,76},{61,79}}, 3);
            holiday_shape(ctx, (CGPoint[]){{71,75},{77,73},{73,79}}, 3);
        } else {
            CGContextSetRGBFillColor(ctx, .94, .54, .60, .52);
            CGContextFillEllipseInRect(ctx, CGRectMake(43, 61, 10, 5));
            CGContextFillEllipseInRect(ctx, CGRectMake(77, 61, 10, 5));
            CGContextSetRGBFillColor(ctx, .13, .19, .25, 1);
            CGContextFillEllipseInRect(ctx, CGRectMake(49, 51, 7, 9));
            CGContextFillEllipseInRect(ctx, CGRectMake(72, 51, 7, 9));
            CGContextSetRGBFillColor(ctx, 1, 1, 1, .9);
            CGContextFillEllipseInRect(ctx, CGRectMake(50, 52, 2.5, 2.5));
            CGContextFillEllipseInRect(ctx, CGRectMake(73, 52, 2.5, 2.5));
            CGContextSetLineWidth(ctx, 2.5);
            CGContextSetRGBStrokeColor(ctx, .18, .24, .32, .92);
            CGContextMoveToPoint(ctx, 53, 73);
            CGContextAddQuadCurveToPoint(ctx, 64, 85, 77, 72);
            CGContextStrokePath(ctx);
        }
        const CGPoint carrot[] = {{62, 60}, {100, 66}, {62, 72}};
        CGContextSetRGBFillColor(ctx, .96, .43, .13, 1);
        holiday_shape(ctx, carrot, 3);
        CGContextSetRGBFillColor(ctx, 1, .64, .28, 1);
        holiday_shape(ctx, (CGPoint[]){{63,60},{96,66},{63,65}}, 3);
        CGContextSetRGBStrokeColor(ctx, .78, .29, .10, .7);
        CGContextSetLineWidth(ctx, 1.2);
        CGContextMoveToPoint(ctx, 73, 63);
        CGContextAddLineToPoint(ctx, 75, 68);
        CGContextMoveToPoint(ctx, 81, 64);
        CGContextAddLineToPoint(ctx, 83, 68);
        CGContextStrokePath(ctx);
    } else {
        CGContextSetLineCap(ctx, kCGLineCapRound);
        CGContextSetLineWidth(ctx, 3.7);
        CGContextSetRGBStrokeColor(ctx, .36, .24, .17, 1);
        CGContextMoveToPoint(ctx, 40, 57);
        CGContextAddLineToPoint(ctx, 18, mischievous ? 40 : 49);
        CGContextAddLineToPoint(ctx, 12, mischievous ? 28 : 38);
        CGContextMoveToPoint(ctx, 88, 57);
        CGContextAddLineToPoint(ctx, 110, mischievous ? 38 : 48);
        CGContextAddLineToPoint(ctx, 116, mischievous ? 27 : 37);
        CGContextMoveToPoint(ctx, 18, mischievous ? 40 : 49);
        CGContextAddLineToPoint(ctx, 7, mischievous ? 38 : 47);
        CGContextMoveToPoint(ctx, 110, mischievous ? 38 : 48);
        CGContextAddLineToPoint(ctx, 121, mischievous ? 35 : 45);
        CGContextStrokePath(ctx);
        CGContextSetRGBFillColor(ctx, .18, .26, .33, 1);
        for (int i = 0; i < 3; i++) {
            CGContextFillEllipseInRect(ctx, CGRectMake(60, 51 + i * 12, 8, 8));
        }
        CGContextSetRGBFillColor(ctx, .52, .61, .68, .9);
        for (int i = 0; i < 3; i++) {
            CGContextFillEllipseInRect(ctx, CGRectMake(62, 52.5 + i * 12, 1.4, 1.4));
            CGContextFillEllipseInRect(ctx, CGRectMake(64.5, 53 + i * 12, 1.4, 1.4));
        }
        if (appearance != 0) {
            holiday_coat(ctx, appearance);
            CGContextFillEllipseInRect(ctx, CGRectMake(40, 33, 48, 15));
            const CGPoint scarf[] = {{68, 39}, {81, 40}, {87, 67}, {72, 63}};
            holiday_shape(ctx, scarf, 4);
            CGContextSetRGBFillColor(ctx, 1, .99, .91, .45);
            CGContextFillEllipseInRect(ctx, CGRectMake(43, 35, 35, 3));
            CGContextFillRect(ctx, CGRectMake(74, 54, 10, 2));
            CGContextFillRect(ctx, CGRectMake(75, 59, 10, 2));
            for (int i = 0; i < 4; i++) CGContextFillRect(ctx, CGRectMake(74 + i * 3, 63 + i, 1.3, 4));
        }
    }
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return image;
}
