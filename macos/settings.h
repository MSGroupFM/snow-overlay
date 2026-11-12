#pragma once

typedef struct {
    NSInteger intensity, wind;
    BOOL australia, santa;
    NSInteger cleanupMinutes, santaMinutes, mischiefSeconds, mischiefThreshold;
} SnowPreferences;

enum { SnowMischiefMaxThreshold = 20 };

static NSString *snow_version_label(void) {
    NSBundle *bundle = NSBundle.mainBundle;
    NSString *version = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    NSString *build = [bundle objectForInfoDictionaryKey:@"CFBundleVersion"];
    if (!version.length) return @"Версия недоступна";
    if (!build.length) return [NSString stringWithFormat:@"Версия %@", version];
    return [NSString stringWithFormat:@"Версия %@ (сборка %@)", version, build];
}

static NSString *const snow_repository_url = @"https://github.com/MSGroupFM/snow-overlay";

static NSArray<NSString *> *snow_cleanup_titles(void) {
    return @[@"Мгновенно", @"Снегоуборочная машина", @"Порыв ветра", @"Уборка лопатой", @"Лавина", @"Оттепель", @"Удар и погоня"];
}

static SnowPreferences snow_read_preferences(NSUserDefaults *defaults) {
    [defaults registerDefaults:@{@"SnowIntensity": @1, @"SnowWind": @0, @"SnowAustralia": @NO,
        @"SnowCleanupMinutes": @15, @"SnowSantaEnabled": @YES, @"SnowSantaMinutes": @5,
        @"SnowMischiefSeconds": @10, @"SnowMischiefThreshold": @1}];
    NSInteger cleanup = [defaults integerForKey:@"SnowCleanupMinutes"];
    if (cleanup != 0 && cleanup != 5 && cleanup != 15 && cleanup != 30) cleanup = 15;
    return (SnowPreferences){
        .intensity = MAX(0, MIN(2, [defaults integerForKey:@"SnowIntensity"])),
        .wind = MAX(-1, MIN(1, [defaults integerForKey:@"SnowWind"])),
        .australia = [defaults boolForKey:@"SnowAustralia"],
        .santa = [defaults boolForKey:@"SnowSantaEnabled"],
        .cleanupMinutes = cleanup,
        .santaMinutes = MAX(1, MIN(120, [defaults integerForKey:@"SnowSantaMinutes"])),
        .mischiefSeconds = MAX(0, MIN(60, [defaults integerForKey:@"SnowMischiefSeconds"])),
        .mischiefThreshold = MAX(1, MIN(SnowMischiefMaxThreshold, [defaults integerForKey:@"SnowMischiefThreshold"]))
    };
}

static void snow_write_preferences(NSUserDefaults *defaults, SnowPreferences settings) {
    [defaults setInteger:settings.intensity forKey:@"SnowIntensity"];
    [defaults setInteger:settings.wind forKey:@"SnowWind"];
    [defaults setBool:settings.australia forKey:@"SnowAustralia"];
    [defaults setBool:settings.santa forKey:@"SnowSantaEnabled"];
    [defaults setInteger:settings.cleanupMinutes forKey:@"SnowCleanupMinutes"];
    [defaults setInteger:settings.santaMinutes forKey:@"SnowSantaMinutes"];
    [defaults setInteger:settings.mischiefSeconds forKey:@"SnowMischiefSeconds"];
    [defaults setInteger:settings.mischiefThreshold forKey:@"SnowMischiefThreshold"];
}

@interface SnowSettingsController : NSWindowController <NSTextFieldDelegate>
@property(nonatomic, copy) void (^onChange)(SnowPreferences);
@property(nonatomic, copy) void (^onFlight)(void);
@property(nonatomic, copy) void (^onCleanup)(SnowCleanup);
- (void)showPreferences:(SnowPreferences)preferences paused:(BOOL)paused;
- (void)refresh:(SnowPreferences)preferences paused:(BOOL)paused;
@end

@implementation SnowSettingsController {
    NSPopUpButton *_intensity, *_wind, *_cleanup, *_effect;
    NSButton *_australia, *_santa, *_flight;
    NSTextField *_santaMinutes, *_mischiefSeconds, *_mischiefThreshold;
    NSStepper *_santaStepper, *_mischiefStepper, *_thresholdStepper;
    NSButton *_mischiefDisclosure;
    NSStackView *_mischiefOptions;
}

static NSTextField *settings_label(NSString *title, NSFont *font, NSColor *color) {
    NSTextField *label = [NSTextField labelWithString:title];
    label.font = font;
    label.textColor = color;
    return label;
}

static NSStackView *settings_row(NSString *title, NSView *control) {
    NSTextField *label = settings_label(title, [NSFont systemFontOfSize:13], NSColor.labelColor);
    [label.widthAnchor constraintEqualToConstant:200].active = YES;
    NSStackView *row = [NSStackView stackViewWithViews:@[label, control]];
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 10;
    return row;
}

static NSView *settings_rule(void) {
    NSBox *rule = [NSBox new];
    rule.boxType = NSBoxSeparator;
    [rule.widthAnchor constraintEqualToConstant:532].active = YES;
    return rule;
}

- (NSPopUpButton *)popup:(NSArray<NSString *> *)titles {
    NSPopUpButton *popup = [NSPopUpButton new];
    [popup addItemsWithTitles:titles];
    [popup.widthAnchor constraintEqualToConstant:205].active = YES;
    popup.target = self;
    popup.action = @selector(preferencesChanged:);
    return popup;
}

- (NSStackView *)numberRow:(NSString *)label field:(NSTextField *)field stepper:(NSStepper *)stepper
                      min:(NSInteger)minimum max:(NSInteger)maximum unit:(NSString *)unit {
    NSNumberFormatter *formatter = [NSNumberFormatter new];
    formatter.allowsFloats = NO;
    formatter.minimum = @(minimum);
    formatter.maximum = @(maximum);
    field.formatter = formatter;
    field.delegate = self;
    field.alignment = NSTextAlignmentRight;
    field.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
    [field.widthAnchor constraintEqualToConstant:55].active = YES;
    [field setAccessibilityLabel:label];
    stepper.minValue = minimum;
    stepper.maxValue = maximum;
    stepper.increment = 1;
    stepper.valueWraps = NO;
    stepper.target = self;
    stepper.action = @selector(preferencesChanged:);
    [stepper setAccessibilityLabel:label];
    NSStackView *value = [NSStackView stackViewWithViews:@[field, stepper,
        settings_label(unit, [NSFont systemFontOfSize:12], NSColor.secondaryLabelColor)]];
    value.alignment = NSLayoutAttributeCenterY;
    value.spacing = 7;
    return settings_row(label, value);
}

- (void)loadWindow {
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 580, 530)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Настройки «Снегопада»";
    window.releasedWhenClosed = NO;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    self.window = window;

    NSView *banner = [NSView new];
    banner.wantsLayer = YES;
    banner.layer.cornerRadius = 12;
    banner.layer.backgroundColor = [NSColor colorWithRed:.08 green:.15 blue:.24 alpha:1].CGColor;
    [banner.widthAnchor constraintEqualToConstant:532].active = YES;
    [banner.heightAnchor constraintEqualToConstant:78].active = YES;
    NSTextField *title = settings_label(@"Снегопад", [NSFont systemFontOfSize:25 weight:NSFontWeightSemibold], NSColor.whiteColor);
    NSTextField *version = settings_label(snow_version_label(), [NSFont systemFontOfSize:12],
        [NSColor colorWithRed:.80 green:.86 blue:.93 alpha:1]);
    NSButton *repo = [NSButton buttonWithTitle:@"GitHub" target:self action:@selector(openRepository:)];
    repo.bordered = NO;
    repo.toolTip = snow_repository_url;
    [repo setAccessibilityLabel:@"Репозиторий «Снегопада» на GitHub"];
    repo.attributedTitle = [[NSAttributedString alloc] initWithString:@"GitHub" attributes:@{
        NSFontAttributeName: [NSFont systemFontOfSize:12],
        NSForegroundColorAttributeName: [NSColor colorWithRed:.62 green:.82 blue:1 alpha:1],
        NSUnderlineStyleAttributeName: @(NSUnderlineStyleSingle)
    }];
    NSStackView *details = [NSStackView stackViewWithViews:@[version, repo]];
    details.alignment = NSLayoutAttributeCenterY;
    details.spacing = 12;
    NSStackView *identity = [NSStackView stackViewWithViews:@[title, details]];
    identity.orientation = NSUserInterfaceLayoutOrientationVertical;
    identity.alignment = NSLayoutAttributeLeading;
    identity.spacing = 4;
    identity.translatesAutoresizingMaskIntoConstraints = NO;
    [banner addSubview:identity];
    [NSLayoutConstraint activateConstraints:@[[identity.leadingAnchor constraintEqualToAnchor:banner.leadingAnchor constant:18],
        [identity.centerYAnchor constraintEqualToAnchor:banner.centerYAnchor]]];
    NSImage *sleigh = holiday_santa_image();
    if (sleigh) {
        NSImageView *preview = [NSImageView imageViewWithImage:sleigh];
        preview.imageScaling = NSImageScaleProportionallyUpOrDown;
        preview.translatesAutoresizingMaskIntoConstraints = NO;
        [banner addSubview:preview];
        [NSLayoutConstraint activateConstraints:@[[preview.trailingAnchor constraintEqualToAnchor:banner.trailingAnchor constant:-18],
            [preview.centerYAnchor constraintEqualToAnchor:banner.centerYAnchor], [preview.widthAnchor constraintEqualToConstant:192],
            [preview.heightAnchor constraintEqualToConstant:64]]];
    }

    NSFont *sectionFont = [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    NSTextField *snowTitle = settings_label(@"Снег и очистка", sectionFont, NSColor.labelColor);
    NSTextField *holidayTitle = settings_label(@"Праздничные события", sectionFont, NSColor.labelColor);
    _intensity = [self popup:@[@"Лёгкий", @"Ровный", @"Метель"]];
    _wind = [self popup:@[@"Влево", @"Штиль", @"Вправо"]];
    _cleanup = [self popup:@[@"Выключена", @"Через 5 минут", @"Через 15 минут", @"Через 30 минут"]];
    _effect = [self popup:snow_cleanup_titles()];
    _effect.target = nil;
    _effect.action = NULL;
    _australia = [NSButton checkboxWithTitle:@"Австралия" target:self action:@selector(preferencesChanged:)];
    _santa = [NSButton checkboxWithTitle:@"Санта с подарками" target:self action:@selector(preferencesChanged:)];
    _santaMinutes = [NSTextField new];
    _mischiefSeconds = [NSTextField new];
    _mischiefThreshold = [NSTextField new];
    _santaStepper = [NSStepper new];
    _mischiefStepper = [NSStepper new];
    _thresholdStepper = [NSStepper new];
    _flight = [NSButton buttonWithTitle:@"Запустить Санту" target:self action:@selector(runFlight:)];
    NSButton *clear = [NSButton buttonWithTitle:@"Очистить" target:self action:@selector(runCleanup:)];
    NSStackView *clearActions = [NSStackView stackViewWithViews:@[_effect, clear]];
    clearActions.spacing = 7;

    _mischiefDisclosure = [NSButton buttonWithTitle:@"Злой режим" target:self action:@selector(toggleMischiefOptions:)];
    [_mischiefDisclosure setButtonType:NSButtonTypePushOnPushOff];
    _mischiefDisclosure.bordered = NO;
    _mischiefDisclosure.image = [NSImage imageWithSystemSymbolName:@"chevron.right" accessibilityDescription:nil];
    _mischiefDisclosure.alternateImage = [NSImage imageWithSystemSymbolName:@"chevron.down" accessibilityDescription:nil];
    _mischiefDisclosure.imagePosition = NSImageLeading;
    _mischiefOptions = [NSStackView stackViewWithViews:@[
        [self numberRow:@"Снеговиков до включения" field:_mischiefThreshold stepper:_thresholdStepper
                     min:1 max:SnowMischiefMaxThreshold unit:@"шт."],
        [self numberRow:@"Длительность злого режима" field:_mischiefSeconds stepper:_mischiefStepper
                     min:0 max:60 unit:@"с · 0 — выкл."]]];
    _mischiefOptions.orientation = NSUserInterfaceLayoutOrientationVertical;
    _mischiefOptions.alignment = NSLayoutAttributeLeading;
    _mischiefOptions.spacing = 8;
    _mischiefOptions.hidden = YES;

    NSStackView *stack = [NSStackView stackViewWithViews:@[banner, snowTitle,
        settings_row(@"Интенсивность снегопада", _intensity), settings_row(@"Направление ветра", _wind),
        settings_row(@"Направление снега", _australia), settings_row(@"Автоочистка", _cleanup),
        settings_row(@"Очистить сейчас", clearActions), settings_rule(), holidayTitle, _santa,
        [self numberRow:@"Интервал пролёта" field:_santaMinutes stepper:_santaStepper min:1 max:120 unit:@"мин"],
        _flight, _mischiefDisclosure, _mischiefOptions]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 9;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [window.contentView addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:24],
        [stack.topAnchor constraintEqualToAnchor:window.contentView.topAnchor constant:20]]];
    [window center];
}

- (void)refresh:(SnowPreferences)preferences paused:(BOOL)paused {
    if (!self.isWindowLoaded) return;
    [_intensity selectItemAtIndex:preferences.intensity];
    [_wind selectItemAtIndex:preferences.wind + 1];
    NSInteger cleanup = [@[@0, @5, @15, @30] indexOfObject:@(preferences.cleanupMinutes)];
    [_cleanup selectItemAtIndex:cleanup == NSNotFound ? 2 : cleanup];
    _australia.state = preferences.australia;
    _santa.state = preferences.santa;
    _santaMinutes.integerValue = _santaStepper.integerValue = preferences.santaMinutes;
    _mischiefSeconds.integerValue = _mischiefStepper.integerValue = preferences.mischiefSeconds;
    _mischiefThreshold.integerValue = _thresholdStepper.integerValue = preferences.mischiefThreshold;
    _santaMinutes.enabled = _santaStepper.enabled = preferences.santa;
    _flight.enabled = !paused;
    _flight.toolTip = paused ? @"Сначала возобновите снегопад" : @"Запустить один пролёт сейчас";
}

- (void)showPreferences:(SnowPreferences)preferences paused:(BOOL)paused {
    if (!self.window) [self loadWindow];
    if (!self.window.isVisible) {
        _mischiefDisclosure.state = NSControlStateValueOff;
        [self toggleMischiefOptions:_mischiefDisclosure];
    }
    [self refresh:preferences paused:paused];
    [self showWindow:nil];
    if (@available(macOS 14.0, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
    [self.window makeKeyAndOrderFront:nil];
}

- (void)toggleMischiefOptions:(NSButton *)sender {
    _mischiefOptions.hidden = sender.state != NSControlStateValueOn;
}

- (void)preferencesChanged:(id)sender {
    if (sender == _santaStepper) _santaMinutes.integerValue = _santaStepper.integerValue;
    if (sender == _mischiefStepper) _mischiefSeconds.integerValue = _mischiefStepper.integerValue;
    if (sender == _thresholdStepper) _mischiefThreshold.integerValue = _thresholdStepper.integerValue;
    if (self.onChange) self.onChange((SnowPreferences){
        .intensity = _intensity.indexOfSelectedItem, .wind = _wind.indexOfSelectedItem - 1,
        .australia = _australia.state == NSControlStateValueOn, .santa = _santa.state == NSControlStateValueOn,
        .cleanupMinutes = [@[@0, @5, @15, @30][_cleanup.indexOfSelectedItem] integerValue],
        .santaMinutes = MAX(1, MIN(120, _santaMinutes.integerValue)),
        .mischiefSeconds = MAX(0, MIN(60, _mischiefSeconds.integerValue)),
        .mischiefThreshold = MAX(1, MIN(SnowMischiefMaxThreshold, _mischiefThreshold.integerValue))
    });
}

- (void)controlTextDidEndEditing:(NSNotification *)note { [self preferencesChanged:note.object]; }
- (void)runFlight:(id)sender { if (self.onFlight) self.onFlight(); }
- (void)runCleanup:(id)sender { if (self.onCleanup) self.onCleanup(_effect.indexOfSelectedItem); }
- (void)openRepository:(id)sender { [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:snow_repository_url]]; }
@end
