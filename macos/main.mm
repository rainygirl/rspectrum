/*
 * R Spectrum for macOS -- a real-time audio spectrum analyzer.
 *
 * The analysis is in ../common; this file is only the two things that cannot
 * be shared: getting audio out of the system and putting bars on screen.
 *
 * AVAudioEngine rather than the older AudioQueue: it hands over float
 * samples at the device's own rate with no format wrangling, and the input
 * tap runs on its own thread, which is exactly what the analyzer's ring
 * buffer expects.
 *
 * Distributed under the terms of the MIT License.
 */

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>

#include "../common/Analyzer.h"

using namespace rspectrum;


// 33 ms, matching the other platforms.
static const NSTimeInterval kFrameInterval = 1.0 / 30.0;


// #pragma mark - SpectrumView


@interface SpectrumView : NSView
@property (nonatomic, assign) Analyzer* analyzer;
@end


@implementation SpectrumView

- (BOOL)isFlipped
{
	return NO;
}

- (NSColor*)colorForValue:(float)value
{
	// Green through amber to red with height, so a glance tells you how close
	// a band is to clipping without having to read a scale.
	CGFloat r, g, b;
	if (value < 0.6f) {
		float t = value / 0.6f;
		r = (80 + t * 140) / 255.0;
		g = 220 / 255.0;
		b = (120 - t * 60) / 255.0;
	} else {
		float t = (value - 0.6f) / 0.4f;
		if (t > 1)
			t = 1;
		r = (220 + t * 30) / 255.0;
		g = (220 - t * 150) / 255.0;
		b = (60 - t * 30) / 255.0;
	}
	return [NSColor colorWithSRGBRed:r green:g blue:b alpha:1.0];
}

- (void)drawRect:(NSRect)dirty
{
	NSRect bounds = [self bounds];

	[[NSColor colorWithSRGBRed:18/255.0 green:18/255.0 blue:22/255.0
		alpha:1.0] setFill];
	NSRectFill(bounds);

	Analyzer* analyzer = self.analyzer;
	if (analyzer == NULL || !analyzer->IsValid())
		return;

	// Horizontal rules every 12 dB down from the ceiling, behind the bars.
	float span = kCeilingDb - kFloorDb;
	[[NSColor colorWithSRGBRed:38/255.0 green:38/255.0 blue:46/255.0
		alpha:1.0] setFill];
	for (float db = 12; db < span; db += 12) {
		CGFloat y = bounds.size.height * (1.0 - db / span);
		NSRectFill(NSMakeRect(0, y, bounds.size.width, 1));
	}

	int count = analyzer->BarCount();
	const float* bars = analyzer->Bars();
	const float* peaks = analyzer->Peaks();

	CGFloat width = bounds.size.width / count;
	CGFloat gap = width > 6 ? 2 : 1;

	for (int i = 0; i < count; i++) {
		CGFloat left = i * width;
		CGFloat w = width - gap;
		if (w < 1)
			w = 1;

		float value = bars[i];
		CGFloat height = bounds.size.height * value;

		if (height >= 1) {
			[[self colorForValue:value] setFill];
			NSRectFill(NSMakeRect(left, 0, w, height));
		}

		// The peak marker is what makes a transient readable: the bar itself
		// has already fallen by the time the eye gets there.
		float peak = peaks[i];
		if (peak > 0.01f) {
			CGFloat y = bounds.size.height * peak;
			[[NSColor colorWithSRGBRed:235/255.0 green:235/255.0
				blue:245/255.0 alpha:1.0] setFill];
			NSRectFill(NSMakeRect(left, y, w, 1));
		}
	}
}

@end


// #pragma mark - Controller


@interface Controller : NSObject <NSApplicationDelegate>
{
	Analyzer		fAnalyzer;
	AVAudioEngine*	fEngine;
	NSWindow*		fWindow;
	SpectrumView*	fView;
	NSTextField*	fStatus;
	NSTimer*		fTimer;
	int				fTick;
}
@end


@implementation Controller

- (void)buildWindow
{
	NSRect frame = NSMakeRect(0, 0, 560, 360);
	fWindow = [[NSWindow alloc]
		initWithContentRect:frame
		styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
			| NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
		backing:NSBackingStoreBuffered
		defer:NO];
	[fWindow setTitle:@"R Spectrum"];
	[fWindow center];
	[NSApp activateIgnoringOtherApps:YES];

	NSView* content = [fWindow contentView];

	NSRect viewFrame = NSMakeRect(0, 22, frame.size.width,
		frame.size.height - 22);
	fView = [[SpectrumView alloc] initWithFrame:viewFrame];
	[fView setAnalyzer:&fAnalyzer];
	[fView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
	[content addSubview:fView];

	fStatus = [[NSTextField alloc]
		initWithFrame:NSMakeRect(8, 3, frame.size.width - 16, 16)];
	[fStatus setBezeled:NO];
	[fStatus setDrawsBackground:NO];
	[fStatus setEditable:NO];
	[fStatus setSelectable:NO];
	[fStatus setFont:[NSFont systemFontOfSize:11]];
	[fStatus setStringValue:@"listening"];
	[fStatus setAutoresizingMask:NSViewWidthSizable];
	[content addSubview:fStatus];

	[fWindow makeKeyAndOrderFront:nil];
}

- (void)startCapture
{
	fEngine = [[AVAudioEngine alloc] init];
	AVAudioInputNode* input = [fEngine inputNode];
	AVAudioFormat* format = [input outputFormatForBus:0];

	if ([format sampleRate] <= 0) {
		[fStatus setStringValue:@"No audio input device"];
		return;
	}

	fAnalyzer.SetSampleRate((float)[format sampleRate]);

	Analyzer* analyzer = &fAnalyzer;
	[input installTapOnBus:0 bufferSize:1024 format:format
		block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
			// Non-interleaved float is what AVAudioEngine gives here, so the
			// channels arrive as separate planes rather than woven together;
			// Feed() expects interleaved, so a single channel is passed and
			// the rest ignored. Mixing the planes by hand would need a
			// scratch buffer on the audio thread for no real gain.
			float* const* channels = [buffer floatChannelData];
			if (channels == NULL)
				return;
			analyzer->Feed(channels[0], (int)[buffer frameLength], 1);
		}];

	NSError* error = nil;
	if (![fEngine startAndReturnError:&error]) {
		[fStatus setStringValue:[NSString stringWithFormat:@"%@",
			[error localizedDescription]]];
		return;
	}
}

- (void)tick:(NSTimer*)timer
{
	fAnalyzer.Update();
	[fView setNeedsDisplay:YES];

	// Once a second is enough for a numeric readout, and it keeps the string
	// formatting off the 30 Hz path.
	if (++fTick >= 30) {
		fTick = 0;
		if (fEngine != nil && [fEngine isRunning]) {
			[fStatus setStringValue:[NSString stringWithFormat:
				@"%.0f Hz   %d bands   peak %.1f dBFS",
				fAnalyzer.SampleRate(), fAnalyzer.BarCount(),
				fAnalyzer.LevelDb()]];
		}
	}
}

// Cmd-Q is the Quit item's key equivalent, nothing more. An application with
// no main menu therefore cannot be quit from the keyboard at all -- the
// window closes but the process stays, and the only way out is Force Quit.
// Building the menu is the whole fix; there is no separate "handle Cmd-Q".
- (void)buildMenu
{
	NSMenu* menubar = [[NSMenu alloc] init];
	NSMenuItem* appItem = [[NSMenuItem alloc] init];
	[menubar addItem:appItem];
	[NSApp setMainMenu:menubar];

	NSMenu* appMenu = [[NSMenu alloc] init];
	[appMenu addItemWithTitle:@"About R Spectrum"
		action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
	[appMenu addItem:[NSMenuItem separatorItem]];

	NSMenuItem* hide = [appMenu addItemWithTitle:@"Hide R Spectrum"
		action:@selector(hide:) keyEquivalent:@"h"];
	[hide setTarget:NSApp];

	[appMenu addItem:[NSMenuItem separatorItem]];

	NSMenuItem* quit = [appMenu addItemWithTitle:@"Quit R Spectrum"
		action:@selector(terminate:) keyEquivalent:@"q"];
	[quit setTarget:NSApp];

	[appItem setSubmenu:appMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification*)note
{
	[self buildMenu];
	[self buildWindow];
	[self startCapture];

	fTimer = [NSTimer scheduledTimerWithTimeInterval:kFrameInterval
		target:self selector:@selector(tick:) userInfo:nil repeats:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app
{
	return YES;
}

- (void)applicationWillTerminate:(NSNotification*)note
{
	[fTimer invalidate];
	if (fEngine != nil) {
		[[fEngine inputNode] removeTapOnBus:0];
		[fEngine stop];
	}
}

@end


int
main(int argc, const char* argv[])
{
	@autoreleasepool {
		NSApplication* app = [NSApplication sharedApplication];
		[app setActivationPolicy:NSApplicationActivationPolicyRegular];

		Controller* controller = [[Controller alloc] init];
		[app setDelegate:controller];

		[app run];
	}
	return 0;
}
