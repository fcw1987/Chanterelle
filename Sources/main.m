#import <AppKit/AppKit.h>
#import "CHAudioEngine.h"
#import "CHPresets.h"
#include <math.h>
#include <unistd.h>
#include <string.h>

// Keep the original defaults keys and bundle domain so upgrades retain every saved setting.
static NSString * const CHPreferencesKey = @"toneControls";
static NSString * const CHMyPresetKey = @"myPreset";
static NSString * const CHDeviceKey = @"selectedOutputUID";

// A template silhouette stays crisp and adapts to both menu bar appearances.
static NSImage *CHMenuBarIcon(void) {
    NSImage *image = [NSImage imageWithSize:NSMakeSize(18, 18) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        (void)rect;
        [NSColor.blackColor setFill];
        NSBezierPath *mushroom = [NSBezierPath bezierPath];
        [mushroom moveToPoint:NSMakePoint(1.5, 10)];
        [mushroom curveToPoint:NSMakePoint(9, 16) controlPoint1:NSMakePoint(2.5, 14) controlPoint2:NSMakePoint(5.5, 16)];
        [mushroom curveToPoint:NSMakePoint(16.5, 10) controlPoint1:NSMakePoint(12.5, 16) controlPoint2:NSMakePoint(15.5, 14)];
        [mushroom curveToPoint:NSMakePoint(10.25, 8) controlPoint1:NSMakePoint(15.5, 8.5) controlPoint2:NSMakePoint(12, 8)];
        [mushroom curveToPoint:NSMakePoint(11, 2.25) controlPoint1:NSMakePoint(10, 6) controlPoint2:NSMakePoint(10.25, 3.5)];
        [mushroom curveToPoint:NSMakePoint(7, 2.25) controlPoint1:NSMakePoint(10, 1.5) controlPoint2:NSMakePoint(8, 1.5)];
        [mushroom curveToPoint:NSMakePoint(7.75, 8) controlPoint1:NSMakePoint(7.75, 3.5) controlPoint2:NSMakePoint(8, 6)];
        [mushroom curveToPoint:NSMakePoint(1.5, 10) controlPoint1:NSMakePoint(6, 8) controlPoint2:NSMakePoint(2.5, 8.5)];
        [mushroom closePath];
        [mushroom fill];
        return YES;
    }];
    image.template = YES;
    image.accessibilityDescription = @"Chanterelle";
    return image;
}

static NSTextField *CHLabel(NSString *text, CGFloat size, NSFontWeight weight) {
    NSTextField *label = [NSTextField labelWithString:text];
    label.font = [NSFont systemFontOfSize:size weight:weight];
    return label;
}

static NSStackView *CHStack(NSArray<NSView *> *views, NSUserInterfaceLayoutOrientation orientation, CGFloat spacing) {
    NSStackView *stack = [NSStackView stackViewWithViews:views];
    stack.orientation = orientation;
    stack.alignment = orientation == NSUserInterfaceLayoutOrientationVertical ? NSLayoutAttributeLeading : NSLayoutAttributeCenterY;
    stack.spacing = spacing;
    return stack;
}

static NSView *CHSpacer(void) {
    NSView *view = [NSView new];
    [view setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    return view;
}

static float CHSafeControl(id value, float minimum, float maximum) {
    if (![value isKindOfClass:NSNumber.class]) return 0;
    float result = [value floatValue];
    return isfinite(result) ? fmaxf(minimum, fminf(maximum, result)) : 0;
}

@interface CHFlippedView : NSView
@end
@implementation CHFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface CHAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) CHAudioEngine *engine;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSPopUpButton *devicePicker;
@property(nonatomic, strong) NSPopUpButton *presetPicker;
@property(nonatomic, strong) NSButton *refreshButton;
@property(nonatomic, strong) NSButton *startButton;
@property(nonatomic, strong) NSButton *bypassButton;
@property(nonatomic, strong) NSTextField *stateLabel;
@property(nonatomic, strong) NSTextField *detailLabel;
@property(nonatomic, strong) NSProgressIndicator *levelMeter;
@property(nonatomic, strong) NSMutableArray<NSSlider *> *sliders;
@property(nonatomic, strong) NSMutableArray<NSTextField *> *valueLabels;
@property(nonatomic, strong) NSArray<CHAudioDevice *> *devices;
@property(nonatomic, strong) NSTimer *meterTimer;
@property(nonatomic, copy) NSString *idleNote;
@property(nonatomic) BOOL heardAudio;
@property(nonatomic) NSUInteger idleTicks;
@property(nonatomic, copy) NSString *smokeDirectory;
@property(nonatomic, strong) NSTextField *technicalLabel;
@property(nonatomic, strong) NSButton *resetButton;
@property(nonatomic) BOOL smokeFailed;
@property(nonatomic, strong) NSMenu *statusMenu;
@property(nonatomic, strong) NSMenuItem *menuState;
@property(nonatomic, strong) NSMenuItem *menuToggle;
@property(nonatomic, strong) NSMenuItem *menuBypass;
@property(nonatomic, strong) NSTextField *presetNote;
@property(nonatomic, strong) NSTextField *meterValue;
@property(nonatomic, copy) NSString *lastPresetName;
@end

@implementation CHAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    self.engine = [CHAudioEngine new];
    self.sliders = [NSMutableArray new];
    self.valueLabels = [NSMutableArray new];
    self.idleNote = @"Choose your output in Sound Settings, then start tuning. System audio access lets this app process playback locally in memory.";
    __weak CHAppDelegate *weakSelf = self;
    self.engine.stateChanged = ^(NSError *error) {
        CHAppDelegate *strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf.idleNote = error.localizedDescription ?: @"Tuning stopped. Normal playback has been restored.";
        [strongSelf refreshDevices:nil];
        [strongSelf updateStatus];
    };
    self.statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    NSImage *icon = CHMenuBarIcon();
    self.statusItem.button.image = icon;
    [self.statusItem.button setAccessibilityLabel:@"Chanterelle"];
    self.statusItem.button.toolTip = @"Chanterelle";
    [self buildStatusMenu];
    NSMenu *mainMenu = [NSMenu new];
    NSMenuItem *appMenuItem = [NSMenuItem new];
    NSMenu *appMenu = [NSMenu new];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit Chanterelle" action:@selector(quit:) keyEquivalent:@"q"];
    quitItem.target = self;
    NSMenuItem *openItem = [[NSMenuItem alloc] initWithTitle:@"Open Window" action:@selector(showWindow:) keyEquivalent:@"o"];
    openItem.target = self;
    [appMenu addItem:openItem];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItem:quitItem];
    appMenuItem.submenu = appMenu;
    [mainMenu addItem:appMenuItem];
    NSApp.mainMenu = mainMenu;
    [self buildWindow];
    [self restoreControls];
    [self refreshDevices:nil];
    [self applyControls];
    [self updateStatus];
    self.meterTimer = [NSTimer scheduledTimerWithTimeInterval:0.2 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(willSleep:) name:NSWorkspaceWillSleepNotification object:nil];
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(didWake:) name:NSWorkspaceDidWakeNotification object:nil];
    [self showWindow:nil];
    if (self.smokeDirectory) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [self runUISmokeTest]; });
    }
}

- (void)buildWindow {
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 540, 840)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"Chanterelle";
    self.window.delegate = self;
    self.window.releasedWhenClosed = NO;
    [self.window center];
    self.window.contentMinSize = NSMakeSize(480, 560);
    NSView *container = self.window.contentView;
    NSScrollView *scroll = [NSScrollView new];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    [container addSubview:scroll];
    [NSLayoutConstraint activateConstraints:@[[scroll.leadingAnchor constraintEqualToAnchor:container.leadingAnchor], [scroll.trailingAnchor constraintEqualToAnchor:container.trailingAnchor], [scroll.topAnchor constraintEqualToAnchor:container.topAnchor], [scroll.bottomAnchor constraintEqualToAnchor:container.bottomAnchor]]];
    NSView *content = [CHFlippedView new];
    content.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = content;
    [content.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    NSStackView *root = CHStack(@[], NSUserInterfaceLayoutOrientationVertical, 14);
    root.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:root];
    [NSLayoutConstraint activateConstraints:@[
        [root.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:24],
        [root.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [root.topAnchor constraintEqualToAnchor:content.topAnchor constant:22],
        [root.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-24]
    ]];
    NSTextField *title = CHLabel(@"Chanterelle", 25, NSFontWeightSemibold);
    NSTextField *subtitle = CHLabel(@"1.1.0  •  macOS system audio equalizer", 12, NSFontWeightRegular);
    subtitle.textColor = NSColor.secondaryLabelColor;
    [root addArrangedSubview:CHStack(@[title, subtitle], NSUserInterfaceLayoutOrientationVertical, 5)];

    self.devicePicker = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.devicePicker.target = self;
    self.devicePicker.action = @selector(deviceChanged:);
    [self.devicePicker setAccessibilityLabel:@"Audio output"];
    [self.devicePicker setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    self.refreshButton = [NSButton buttonWithTitle:@"Refresh" target:self action:@selector(refreshDevices:)];
    NSStackView *deviceRow = CHStack(@[self.devicePicker, self.refreshButton], NSUserInterfaceLayoutOrientationHorizontal, 8);
    NSStackView *outputGroup = CHStack(@[CHLabel(@"Output", 12, NSFontWeightSemibold), deviceRow], NSUserInterfaceLayoutOrientationVertical, 6);
    [root addArrangedSubview:outputGroup];
    [outputGroup.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [deviceRow.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;

    self.presetPicker = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    for (size_t i = 0; i < CH_PRESET_COUNT; i++) [self.presetPicker addItemWithTitle:@(CH_PRESETS[i].name)];
    [self.presetPicker addItemsWithTitles:@[@"My preset", @"Custom / modified"]];
    self.presetPicker.target = self;
    self.presetPicker.action = @selector(presetChanged:);
    [self.presetPicker setAccessibilityLabel:@"Tone preset"];
    self.presetPicker.autoenablesItems = NO;
    [self.presetPicker itemWithTitle:@"Custom / modified"].enabled = NO;
    [self.presetPicker itemWithTitle:@"My preset"].enabled = [NSUserDefaults.standardUserDefaults arrayForKey:CHMyPresetKey].count == 4;
    NSButton *save = [NSButton buttonWithTitle:@"Save my preset" target:self action:@selector(savePreset:)];
    NSStackView *presetRow = CHStack(@[self.presetPicker, save], NSUserInterfaceLayoutOrientationHorizontal, 8);
    NSStackView *presetGroup = CHStack(@[CHLabel(@"Listening preset", 12, NSFontWeightSemibold), presetRow], NSUserInterfaceLayoutOrientationVertical, 6);
    [root addArrangedSubview:presetGroup];
    [presetGroup.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [presetRow.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    self.presetNote = [NSTextField wrappingLabelWithString:@"Presets are gentle starting points, not calibrated speaker correction."];
    self.presetNote.font = [NSFont systemFontOfSize:11];
    self.presetNote.textColor = NSColor.secondaryLabelColor;
    [root addArrangedSubview:self.presetNote];
    [self.presetNote.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [root addArrangedSubview:CHLabel(@"Tone · −6 to +6 dB", 12, NSFontWeightSemibold)];

    [self addSliderTo:root title:@"Bass" note:@"150 Hz shelf · left: lighter bass / right: more warmth" minimum:-6 maximum:6 index:0];
    [self addSliderTo:root title:@"Mids" note:@"900 Hz · left: less body / right: more voice presence" minimum:-6 maximum:6 index:1];
    [self addSliderTo:root title:@"Treble" note:@"4.5 kHz shelf · left: softer / right: brighter" minimum:-6 maximum:6 index:2];
    [self addSliderTo:root title:@"Output trim" note:@"−12 to 0 dB · left: quieter with more peak room / right: less attenuation" minimum:-12 maximum:0 index:3];

    self.bypassButton = [NSButton checkboxWithTitle:@"Compare original tone (keep trim & headroom)" target:self action:@selector(bypassChanged:)];
    self.bypassButton.toolTip = @"Compare with the original tone at the same trim and reserved headroom. Stop tuning restores direct playback.";
    [root addArrangedSubview:self.bypassButton];
    NSTextField *comparisonNote = [NSTextField wrappingLabelWithString:@"Comparison changes tone only. Flat EQ clears tone and trim. Stop tuning restores direct playback; speaker volume stays in macOS."];
    comparisonNote.font = [NSFont systemFontOfSize:11];
    comparisonNote.textColor = NSColor.secondaryLabelColor;
    [root addArrangedSubview:comparisonNote];
    [comparisonNote.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    self.technicalLabel = CHLabel(@"Start is always manual. No microphone or screen capture.", 10, NSFontWeightRegular);
    self.technicalLabel.textColor = NSColor.secondaryLabelColor;
    NSStackView *advanced = CHStack(@[self.technicalLabel], NSUserInterfaceLayoutOrientationVertical, 4);
    NSButton *details = [NSButton checkboxWithTitle:@"Show audio details" target:self action:@selector(toggleDetails:)];
    details.state = NSControlStateValueOff;
    advanced.hidden = YES;
    details.tag = 101;
    advanced.identifier = @"audioDetails";
    [root addArrangedSubview:details];
    [root addArrangedSubview:advanced];

    self.levelMeter = [NSProgressIndicator new];
    self.levelMeter.style = NSProgressIndicatorStyleBar;
    self.levelMeter.indeterminate = NO;
    self.levelMeter.minValue = 0;
    self.levelMeter.maxValue = 1;
    [self.levelMeter setAccessibilityLabel:@"Output signal level"];
    NSTextField *levelLabel = CHLabel(@"Signal", 11, NSFontWeightRegular);
    levelLabel.textColor = NSColor.secondaryLabelColor;
    self.meterValue = CHLabel(@"No signal", 11, NSFontWeightRegular);
    [self.meterValue.widthAnchor constraintEqualToConstant:88].active = YES;
    NSStackView *meterRow = CHStack(@[levelLabel, self.levelMeter, self.meterValue], NSUserInterfaceLayoutOrientationHorizontal, 10);
    [root addArrangedSubview:meterRow];
    [meterRow.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;

    self.stateLabel = CHLabel(@"Ready when you are", 13, NSFontWeightSemibold);
    self.detailLabel = [NSTextField wrappingLabelWithString:self.idleNote];
    self.detailLabel.font = [NSFont systemFontOfSize:11];
    self.detailLabel.textColor = NSColor.secondaryLabelColor;
    [root addArrangedSubview:self.stateLabel];
    [root setCustomSpacing:4 afterView:self.stateLabel];
    [root addArrangedSubview:self.detailLabel];
    [self.detailLabel.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [self.detailLabel.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;

    NSButton *soundSettings = [NSButton buttonWithTitle:@"Sound Settings" target:self action:@selector(openSoundSettings:)];
    self.startButton = [NSButton buttonWithTitle:@"Start tuning" target:self action:@selector(toggleTuning:)];
    self.startButton.bezelStyle = NSBezelStyleRounded;
    self.startButton.keyEquivalent = @"\r";
    self.resetButton = [NSButton buttonWithTitle:@"Flat EQ" target:self action:@selector(resetSettings:)];
    self.resetButton.toolTip = @"Set all tone gains and trim to zero. Keeps your saved preset and selected output; processing stays in its current state.";
    NSStackView *actions = CHStack(@[soundSettings, self.resetButton, CHSpacer(), self.startButton], NSUserInterfaceLayoutOrientationHorizontal, 8);
    [root addArrangedSubview:actions];
    [actions.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;

    // Size from the arranged content so system font metrics cannot clip the footer.
    [content layoutSubtreeIfNeeded];
    NSSize fitting = root.fittingSize;
    [self.window setContentSize:NSMakeSize(540, MIN(880, MAX(740, fitting.height + 44)))];
}

- (void)addSliderTo:(NSStackView *)root title:(NSString *)title note:(NSString *)note minimum:(double)minimum maximum:(double)maximum index:(NSInteger)index {
    NSTextField *name = CHLabel(title, 13, NSFontWeightMedium);
    NSTextField *value = CHLabel(@"0.0 dB", 12, NSFontWeightRegular);
    value.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    value.alignment = NSTextAlignmentRight;
    [value.widthAnchor constraintEqualToConstant:64].active = YES;
    NSStackView *row = CHStack(@[name, CHSpacer(), value], NSUserInterfaceLayoutOrientationHorizontal, 8);
    NSSlider *slider = [NSSlider sliderWithValue:0 minValue:minimum maxValue:maximum target:self action:@selector(sliderChanged:)];
    slider.continuous = YES;
    slider.tag = index;
    slider.numberOfTickMarks = 13;
    slider.allowsTickMarkValuesOnly = NO;
    [slider setAccessibilityLabel:[title stringByAppendingString:@" in decibels"]];
    slider.toolTip = note;
    [slider setAccessibilityHelp:note];
    NSTextField *description = [NSTextField wrappingLabelWithString:note];
    description.font = [NSFont systemFontOfSize:10];
    description.textColor = NSColor.secondaryLabelColor;
    NSStackView *group = CHStack(@[row, slider, description], NSUserInterfaceLayoutOrientationVertical, 3);
    [root addArrangedSubview:group];
    [group.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [description.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [row.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [slider.widthAnchor constraintEqualToAnchor:root.widthAnchor].active = YES;
    [self.sliders addObject:slider];
    [self.valueLabels addObject:value];
}

- (void)restoreControls {
    NSArray *saved = [NSUserDefaults.standardUserDefaults arrayForKey:CHPreferencesKey];
    if (saved.count == 4) {
        for (NSUInteger i = 0; i < 4; i++) self.sliders[i].floatValue = CHSafeControl(saved[i], i == 3 ? -12 : -6, i == 3 ? 0 : 6);
    }
    // Starting is always explicit, and bypass does not persist across launches.
    self.bypassButton.state = NSControlStateValueOff;
    [self updatePresetTitle];
}

- (NSArray<NSNumber *> *)controlValues {
    return @[@(self.sliders[0].floatValue), @(self.sliders[1].floatValue), @(self.sliders[2].floatValue), @(self.sliders[3].floatValue)];
}

- (NSDictionary<NSString *, NSArray<NSNumber *> *> *)presets {
    NSMutableDictionary *result = [NSMutableDictionary new];
    for (size_t i = 0; i < CH_PRESET_COUNT; i++) {
        const CHPreset *preset = &CH_PRESETS[i];
        result[@(preset->name)] = @[@(preset->bass), @(preset->mid), @(preset->treble), @(preset->trim)];
    }
    NSArray *custom = [NSUserDefaults.standardUserDefaults arrayForKey:CHMyPresetKey];
    if (custom.count == 4) result[@"My preset"] = custom;
    return result;
}

- (void)updatePresetTitle {
    NSArray *current = [self controlValues];
    NSDictionary *presets = [self presets];
    for (NSString *name in self.presetPicker.itemTitles) {
        NSArray *values = presets[name];
        if (values.count != 4) continue;
        BOOL match = YES;
        for (NSUInteger i = 0; i < 4; i++) {
            float expected = CHSafeControl(values[i], i == 3 ? -12 : -6, i == 3 ? 0 : 6);
            if (fabsf([current[i] floatValue] - expected) > 0.025f) match = NO;
        }
        if (match) { [self.presetPicker selectItemWithTitle:name]; self.lastPresetName = name; [self updatePresetNote]; return; }
    }
    [self.presetPicker selectItemWithTitle:@"Custom / modified"];
    [self updatePresetNote];
}

- (void)applyControls {
    [self.engine setBass:self.sliders[0].floatValue mid:self.sliders[1].floatValue treble:self.sliders[2].floatValue
        trim:self.sliders[3].floatValue bypass:self.bypassButton.state == NSControlStateValueOn];
    for (NSUInteger i = 0; i < 4; i++) {
        float value = self.sliders[i].floatValue;
        self.valueLabels[i].stringValue = fabsf(value) < 0.05f ? @"0.0 dB" : [NSString stringWithFormat:@"%+.1f dB", value];
    }
    if (!self.smokeDirectory) [NSUserDefaults.standardUserDefaults setObject:[self controlValues] forKey:CHPreferencesKey];
}

- (void)sliderChanged:(NSSlider *)sender { [self applyControls]; [self updatePresetTitle]; [self updateStatus]; }
- (void)bypassChanged:(NSButton *)sender { [self applyControls]; [self updateStatus]; }
- (void)presetChanged:(NSPopUpButton *)sender {
    NSArray *values = [self presets][sender.titleOfSelectedItem];
    if (values.count != 4) return;
    for (NSUInteger i = 0; i < 4; i++) self.sliders[i].floatValue = CHSafeControl(values[i], i == 3 ? -12 : -6, i == 3 ? 0 : 6);
    self.lastPresetName = sender.titleOfSelectedItem;
    [self applyControls]; [self updatePresetTitle]; [self updateStatus];
}
- (void)savePreset:(id)sender {
    if (!self.smokeDirectory) [NSUserDefaults.standardUserDefaults setObject:[self controlValues] forKey:CHMyPresetKey];
    [self.presetPicker itemWithTitle:@"My preset"].enabled = YES;
    [self.presetPicker selectItemWithTitle:@"My preset"];
}

- (void)refreshDevices:(id)sender {
    if (self.engine.running) return;
    NSString *preferredUID = self.devicePicker.selectedItem.representedObject ?: [NSUserDefaults.standardUserDefaults stringForKey:CHDeviceKey];
    self.devices = [CHAudioEngine outputDevices];
    [self.devicePicker removeAllItems];
    NSInteger chosen = -1;
    NSInteger likelyLG = -1;
    NSInteger currentDefault = -1;
    for (NSUInteger i = 0; i < self.devices.count; i++) {
        CHAudioDevice *device = self.devices[i];
        NSString *title = device.isDefaultOutput ? [device.name stringByAppendingString:@" (current output)"] : device.name;
        if (!device.supportsStereo) title = [title stringByAppendingString:@" — unsupported layout"];
        [self.devicePicker addItemWithTitle:title];
        self.devicePicker.lastItem.representedObject = device.uid;
        self.devicePicker.lastItem.enabled = device.supportsStereo;
        if (device.supportsStereo && [device.uid isEqualToString:preferredUID]) chosen = (NSInteger)i;
        // UltraFine is the real LG hardware name, required to preserve output selection.
        if (device.supportsStereo && ([device.name localizedCaseInsensitiveContainsString:@"UltraFine"] || [device.name localizedCaseInsensitiveContainsString:@"LG "])) likelyLG = (NSInteger)i;
        if (device.supportsStereo && device.isDefaultOutput) currentDefault = (NSInteger)i;
    }
    self.devicePicker.autoenablesItems = NO;
    if (self.devices.count == 0) {
        [self.devicePicker addItemWithTitle:@"No supported audio output found"];
        self.devicePicker.enabled = NO;
    } else {
        if (chosen < 0) chosen = likelyLG >= 0 ? likelyLG : currentDefault;
        if (chosen < 0) {
            for (NSUInteger i = 0; i < self.devices.count; i++) if (self.devices[i].supportsStereo) { chosen = (NSInteger)i; break; }
        }
        if (chosen < 0) chosen = 0;
        [self.devicePicker selectItemAtIndex:chosen];
        self.devicePicker.enabled = YES;
    }
    self.startButton.enabled = self.devices.count > 0;
}

- (void)deviceChanged:(id)sender {
    NSString *uid = self.devicePicker.selectedItem.representedObject;
    if (uid && !self.smokeDirectory) [NSUserDefaults.standardUserDefaults setObject:uid forKey:CHDeviceKey];
    [self updateStatus];
}

- (void)toggleTuning:(id)sender {
    if (self.smokeDirectory) return;
    if (self.engine.running) {
        [self.engine stop];
        self.idleNote = self.engine.lastError.localizedDescription ?: @"Normal playback is restored. Your tone settings are saved for next time.";
        [self refreshDevices:nil];
    } else {
        NSString *uid = self.devicePicker.selectedItem.representedObject;
        if (!uid) return;
        [self deviceChanged:nil];
        [self applyControls];
        self.heardAudio = NO;
        NSError *error = nil;
        if (![self.engine startWithDeviceUID:uid error:&error]) {
            self.idleNote = error.localizedDescription ?: @"Could not start audio processing. Open Sound Settings and check the selected output.";
            NSAlert *alert = [NSAlert new];
            alert.messageText = @"Tuning could not start";
            alert.informativeText = error.localizedFailureReason.length
                ? [NSString stringWithFormat:@"%@\n\n%@", self.idleNote, error.localizedFailureReason]
                : self.idleNote;
            [alert addButtonWithTitle:@"OK"];
            [alert beginSheetModalForWindow:self.window completionHandler:nil];
        }
    }
    [self updateStatus];
}

- (void)tick:(NSTimer *)timer {
    if (!self.engine.running && (++self.idleTicks % 15 == 0) && !self.devicePicker.isHighlighted) [self refreshDevices:nil];
    [self updateStatus];
}

- (void)updateStatus {
    BOOL running = self.engine.running;
    self.startButton.title = running ? @"Stop tuning" : @"Start tuning";
    NSInteger selectedIndex = self.devicePicker.indexOfSelectedItem;
    CHAudioDevice *selected = selectedIndex >= 0 && (NSUInteger)selectedIndex < self.devices.count ? self.devices[(NSUInteger)selectedIndex] : nil;
    self.startButton.enabled = running || (selected && selected.supportsStereo && selected.isDefaultOutput);
    self.startButton.toolTip = self.startButton.enabled ? @"Start requires system playback capture permission." : @"Select this output in macOS Sound Settings first.";
    self.devicePicker.enabled = !running && self.devices.count > 0;
    self.refreshButton.enabled = !running;
    float peak = running ? self.engine.peak : 0;
    self.levelMeter.doubleValue = peak;
    self.meterValue.stringValue = peak > 0.00001f ? [NSString stringWithFormat:@"%.1f dBFS", 20 * log10f(peak)] : @"No signal";
    if (peak > 0.00001f) self.heardAudio = YES;
    if (running) {
        self.stateLabel.stringValue = !self.engine.processing ? @"Waiting for audio" : (self.bypassButton.state == NSControlStateValueOn ? @"Tone controls bypassed" : @"Tuning your audio");
        self.stateLabel.textColor = NSColor.controlAccentColor;
        if (!self.engine.processing) {
            self.detailLabel.stringValue = self.engine.statusText.length ? self.engine.statusText : @"Play some audio and allow system audio access if macOS asks. Your original audio keeps playing while capture is prepared.";
        } else if (self.engine.clipCount > 0) {
            self.detailLabel.stringValue = @"Some peaks reached the safety ceiling. Lower Output trim or reduce the boosts for more room.";
        } else if (!self.heardAudio) {
            self.detailLabel.stringValue = @"Play some audio and allow system audio access if macOS asks. If playback is silent, stop tuning and check the audio permission in System Settings.";
        } else {
            self.detailLabel.stringValue = @"Audio stays on your Mac. Boosts reserve headroom automatically. Bypass keeps that headroom and trim for comparing the tone.";
        }
    } else {
        self.stateLabel.stringValue = @"Tuning is off";
        self.stateLabel.textColor = NSColor.labelColor;
        self.detailLabel.stringValue = self.devices.count == 0 ? @"No physical audio output is visible. Check the monitor connection, then refresh. Tuning is off." :
            (selected && !selected.supportsStereo ? @"This output needs a single stereo PCM stream. Choose a supported output." :
            (selected && !selected.isDefaultOutput ? @"Choose this output in macOS Sound Settings first. This app never changes the Mac’s output or speaker volume." : self.idleNote));
    }
    self.technicalLabel.stringValue = running ? [NSString stringWithFormat:@"%.0f Hz  •  Buffer target %.1f ms  •  Headroom %.1f dB", self.engine.sampleRate, self.engine.bufferDurationMS, self.engine.headroomDB] : @"Start is always manual. No microphone or screen capture.";
    self.menuState.title = [@"Status: " stringByAppendingString:self.stateLabel.stringValue];
    self.menuToggle.title = self.startButton.title;
    self.menuToggle.enabled = self.startButton.enabled;
    self.menuBypass.state = self.bypassButton.state;
    for (NSMenuItem *item in [self.statusMenu itemWithTitle:@"Listening presets"].submenu.itemArray) item.state = [item.title isEqualToString:self.presetPicker.titleOfSelectedItem] ? NSControlStateValueOn : NSControlStateValueOff;
    self.statusItem.button.toolTip = self.engine.processing ? @"Chanterelle is processing audio" : (running ? @"Chanterelle is waiting for audio" : @"Chanterelle is off");
}

- (void)showWindow:(id)sender {
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activate];
}
- (void)buildStatusMenu {
    self.statusMenu = [NSMenu new];
    self.statusMenu.autoenablesItems = NO;
    self.menuState = [[NSMenuItem alloc] initWithTitle:@"Status: Tuning is off" action:nil keyEquivalent:@""];
    self.menuState.enabled = NO;
    [self.statusMenu addItem:self.menuState];
    for (NSArray *entry in @[@[@"Open Window", NSStringFromSelector(@selector(showWindow:)), @"o"], @[@"Start tuning", NSStringFromSelector(@selector(toggleTuning:)), @""], @[@"Compare original tone", NSStringFromSelector(@selector(menuCompare:)), @""], @[@"Flat EQ", NSStringFromSelector(@selector(resetSettings:)), @""]]) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:entry[0] action:NSSelectorFromString(entry[1]) keyEquivalent:entry[2]];
        item.target = self;
        [self.statusMenu addItem:item];
        if (item.action == @selector(toggleTuning:)) self.menuToggle = item;
        if (item.action == @selector(menuCompare:)) self.menuBypass = item;
    }
    NSMenuItem *presets = [[NSMenuItem alloc] initWithTitle:@"Listening presets" action:nil keyEquivalent:@""];
    presets.submenu = [NSMenu new];
    for (size_t i = 0; i < CH_PRESET_COUNT; i++) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@(CH_PRESETS[i].name) action:@selector(menuPreset:) keyEquivalent:@""];
        item.target = self;
        [presets.submenu addItem:item];
    }
    [self.statusMenu addItem:presets];
    [self.statusMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit Chanterelle" action:@selector(quit:) keyEquivalent:@"q"];
    quit.target = self;
    [self.statusMenu addItem:quit];
    // AppKit owns click handling for both mouse buttons and keyboard activation.
    self.statusItem.menu = self.statusMenu;
}
- (void)menuCompare:(id)sender {
    self.bypassButton.state = self.bypassButton.state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn;
    [self bypassChanged:nil];
}
- (void)menuPreset:(NSMenuItem *)sender {
    [self.presetPicker selectItemWithTitle:sender.title];
    [self presetChanged:self.presetPicker];
}
- (void)toggleDetails:(NSButton *)sender {
    NSStackView *root = (NSStackView *)sender.superview;
    for (NSView *view in root.arrangedSubviews) if ([view.identifier isEqualToString:@"audioDetails"]) view.hidden = sender.state != NSControlStateValueOn;
}
- (void)updatePresetNote {
    NSString *name = self.presetPicker.titleOfSelectedItem;
    for (size_t i = 0; i < CH_PRESET_COUNT; i++) if ([name isEqualToString:@(CH_PRESETS[i].name)]) {
        self.presetNote.stringValue = [@(CH_PRESETS[i].note) stringByAppendingString:@" · A starting point, not speaker calibration."];
        return;
    }
    self.presetNote.stringValue = [name isEqualToString:@"My preset"] ? @"Your saved tone and trim settings." : [NSString stringWithFormat:@"Modified from %@ · Save my preset to keep this curve.", self.lastPresetName ?: @"Neutral"];
}
- (void)openSoundSettings:(id)sender {
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.Sound-Settings.extension"];
    [NSWorkspace.sharedWorkspace openURL:url];
}
- (void)willSleep:(NSNotification *)notification {
    if (!self.engine.running) return;
    [self.engine stop];
    self.idleNote = self.engine.lastError.localizedDescription ?: @"Tuning stopped for sleep. Start it again when your monitor is ready.";
    [self updateStatus];
}

- (void)didWake:(NSNotification *)notification {
    (void)notification;
    if (!self.engine.running) {
        self.idleNote = @"Tuning is off after wake. Check your output, then start when ready.";
        [self refreshDevices:nil]; [self updateStatus];
    }
}

- (void)resetSettings:(id)sender {
    (void)sender;
    for (NSSlider *slider in self.sliders) slider.floatValue = 0;
    self.bypassButton.state = NSControlStateValueOff;
    [self applyControls]; [self updatePresetTitle]; [self updateStatus];
}

- (void)runUISmokeTest {
    [self resetSettings:nil];
    [self.window setContentSize:NSMakeSize(540, 880)];
    [self.window.contentView layoutSubtreeIfNeeded];
    NSView *content = self.window.contentView;
    NSBitmapImageRep *bitmap = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
    [content cacheDisplayInRect:content.bounds toBitmapImageRep:bitmap];
    // Cache display omits the window's backing fill; render it into an opaque
    // bitmap so the exported native controls remain legible on the website.
    NSBitmapImageRep *snapshot = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
        pixelsWide:bitmap.pixelsWide pixelsHigh:bitmap.pixelsHigh bitsPerSample:8
        samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace
        bytesPerRow:0 bitsPerPixel:0];
    snapshot.size = content.bounds.size;
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:snapshot];
    [[NSColor.windowBackgroundColor colorWithAlphaComponent:1.0] setFill];
    NSRectFillUsingOperation(content.bounds, NSCompositingOperationCopy);
    NSImage *cachedImage = [[NSImage alloc] initWithSize:content.bounds.size];
    [cachedImage addRepresentation:bitmap];
    [cachedImage drawInRect:content.bounds fromRect:NSZeroRect
        operation:NSCompositingOperationSourceOver fraction:1.0 respectFlipped:NO hints:nil];
    [NSGraphicsContext restoreGraphicsState];
    NSData *png = [snapshot representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    NSError *failure = nil;
    BOOL directoryOK = [NSFileManager.defaultManager createDirectoryAtPath:self.smokeDirectory withIntermediateDirectories:YES attributes:nil error:&failure];
    BOOL imageOK = directoryOK && [png writeToFile:[self.smokeDirectory stringByAppendingPathComponent:@"off-state-ui.png"] options:NSDataWritingAtomic error:&failure];
    BOOL menuWired = self.statusItem.menu == self.statusMenu && self.menuToggle.target == self && self.menuToggle.action == @selector(toggleTuning:);
    [self.window close];
    [self.statusMenu performActionForItemAtIndex:1];
    BOOL reopened = self.window.isVisible;
    NSMenuItem *speech = [self.statusMenu itemWithTitle:@"Listening presets"].submenu.itemArray[2];
    [NSApp sendAction:speech.action to:speech.target from:speech];
    BOOL presetApplied = fabsf(self.sliders[0].floatValue + 2.5f) < 0.01f && fabsf(self.sliders[1].floatValue - 1.5f) < 0.01f;
    [self.statusMenu performActionForItemAtIndex:3];
    BOOL comparisonWorks = self.bypassButton.state == NSControlStateValueOn && self.menuBypass.state == NSControlStateValueOn;
    [self.statusMenu performActionForItemAtIndex:4];
    BOOL flatWorks = self.sliders[0].floatValue == 0 && self.sliders[1].floatValue == 0 && self.bypassButton.state == NSControlStateValueOff;
    [self.window setContentSize:NSMakeSize(480, 560)];
    [self.window.contentView layoutSubtreeIfNeeded];
    BOOL smallWindowWorks = self.window.contentView.bounds.size.height == 560;
    [self.window setContentSize:NSMakeSize(540, 880)];
    [self.window.contentView layoutSubtreeIfNeeded];
    NSRect footer = [self.startButton convertRect:self.startButton.bounds toView:content];
    NSRect detail = [self.detailLabel convertRect:self.detailLabel.bounds toView:content];
    BOOL footerVisible = NSContainsRect(content.bounds, footer) && NSContainsRect(content.bounds, detail);
    BOOL off = !self.engine.running && !self.engine.processing;
    NSDictionary *result = @{@"passed": @(imageOK && footerVisible && off && menuWired && reopened && presetApplied && comparisonWorks && flatWorks && smallWindowWorks), @"imageWritten": @(imageOK),
        @"menuWired": @(menuWired), @"menuOpenReopensClosedWindow": @(reopened), @"menuPresetApplied": @(presetApplied), @"menuComparisonWorks": @(comparisonWorks), @"flatEQWorks": @(flatWorks), @"smallWindowWorks": @(smallWindowWorks), @"footerAndStatusVisible": @(footerVisible), @"tuningOff": @(off), @"captureRequested": @NO,
        @"windowNumber": @(self.window.windowNumber), @"contentWidth": @(content.bounds.size.width), @"contentHeight": @(content.bounds.size.height),
        @"error": failure.localizedDescription ?: @""};
    NSData *json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
    [json writeToFile:[self.smokeDirectory stringByAppendingPathComponent:@"ui-smoke.json"] atomically:YES];
    printf("%s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
    self.smokeFailed = !imageOK || !footerVisible || !off || !menuWired || !reopened || !presetApplied || !comparisonWorks || !flatWorks || !smallWindowWorks;
    if (!getenv("CH_UI_SMOKE_HOLD")) [NSApp terminate:nil];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag { [self showWindow:nil]; return YES; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return NO; }
- (void)quit:(id)sender { [NSApp terminate:nil]; }
- (void)applicationWillTerminate:(NSNotification *)notification {
    [self.meterTimer invalidate];
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
    self.engine.stateChanged = nil;
    [self.engine stop];
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc == 2 && (strcmp(argv[1], "--diagnostics") == 0 || strcmp(argv[1], "--self-test") == 0)) {
            BOOL testing = strcmp(argv[1], "--self-test") == 0;
            NSDictionary *result = testing ? [CHAudioEngine offStateSelfTest] : [CHAudioEngine hardwareSnapshot];
            NSMutableDictionary *report = [result mutableCopy];
            report[@"version"] = @"1.1.0"; report[@"build"] = @"5"; report[@"releaseChannel"] = @"stable";
            NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nil];
            printf("%s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
            return testing && ![result[@"passed"] boolValue] ? 1 : 0;
        }
        NSString *smokeDirectory = argc == 3 && strcmp(argv[1], "--ui-smoke-test") == 0 ? [NSString stringWithUTF8String:argv[2]] : nil;
        if (argc > 1 && !smokeDirectory) { fprintf(stderr, "Usage: Chanterelle [--diagnostics | --self-test | --ui-smoke-test directory]\n"); return 64; }
        NSApplication *app = NSApplication.sharedApplication;
        NSString *bundleID = NSBundle.mainBundle.bundleIdentifier;
        if (bundleID && !smokeDirectory) {
            for (NSRunningApplication *other in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID]) {
                if (other.processIdentifier != getpid()) {
                    [other activateWithOptions:0];
                    return 0;
                }
            }
        }
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        __attribute__((objc_precise_lifetime)) CHAppDelegate *delegate = [CHAppDelegate new];
        delegate.smokeDirectory = smokeDirectory;
        app.delegate = delegate;
        [app run];
        if (delegate.smokeFailed) return 1;
    }
    return 0;
}
