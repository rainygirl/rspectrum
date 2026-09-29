/*
 * R Spectrum for Linux -- a real-time audio spectrum analyzer.
 *
 * The analysis is in ../common; this file is only the two things that cannot
 * be shared: getting audio out of the system and putting bars on screen.
 *
 * ALSA and Xlib directly, with no toolkit. A spectrum display is one widget
 * and a timer; pulling in GTK or Qt for that would cost more in dependencies
 * than it saves in code, and this way the binary runs on anything with an X
 * server.
 *
 * Distributed under the terms of the MIT License.
 */

#include "../common/Analyzer.h"

#include <alsa/asoundlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace rspectrum;


static const int kWidth = 560;
static const int kHeight = 360;
static const int kStatusHeight = 22;

// 33 ms, matching the other platforms.
static const long kFrameIntervalUs = 33000;

static const int kPeriodFrames = 512;


struct Context {
	Analyzer*			analyzer;
	snd_pcm_t*			pcm;
	volatile bool		quit;
	unsigned int		rate;
	unsigned int		channels;
};


// #pragma mark - capture


// ALSA has no callback model worth using here: snd_async_add_pcm_handler
// runs in a signal context, which is no place to touch a ring buffer. A
// thread doing blocking reads is simpler and has the same effect.
static void*
capture_thread(void* data)
{
	Context* context = (Context*)data;

	int frames = kPeriodFrames;
	short* buffer = (short*)malloc(sizeof(short) * frames * context->channels);
	if (buffer == NULL)
		return NULL;

	while (!context->quit) {
		snd_pcm_sframes_t got = snd_pcm_readi(context->pcm, buffer, frames);

		if (got == -EPIPE) {
			// Overrun: the reader fell behind. Recover and carry on rather
			// than dying -- a stall while the machine is busy is normal.
			snd_pcm_prepare(context->pcm);
			continue;
		}
		if (got == -EAGAIN)
			continue;
		if (got < 0) {
			if (snd_pcm_recover(context->pcm, got, 1) < 0)
				break;
			continue;
		}

		context->analyzer->FeedInt16(buffer, (int)got, context->channels);
	}

	free(buffer);
	return NULL;
}


static snd_pcm_t*
open_capture(const char* device, unsigned int* rate, unsigned int* channels)
{
	snd_pcm_t* pcm = NULL;
	if (snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0) < 0)
		return NULL;

	snd_pcm_hw_params_t* params;
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_any(pcm, params);

	snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);

	// Ask, then read back what was actually granted. A device that cannot do
	// 48 kHz stereo will quietly be given something else, and using the
	// requested value would put every bar in the wrong place.
	unsigned int wantRate = 48000;
	snd_pcm_hw_params_set_rate_near(pcm, params, &wantRate, 0);

	unsigned int wantChannels = 2;
	if (snd_pcm_hw_params_set_channels(pcm, params, wantChannels) < 0) {
		wantChannels = 1;
		snd_pcm_hw_params_set_channels(pcm, params, wantChannels);
	}

	snd_pcm_uframes_t period = kPeriodFrames;
	snd_pcm_hw_params_set_period_size_near(pcm, params, &period, 0);

	if (snd_pcm_hw_params(pcm, params) < 0) {
		snd_pcm_close(pcm);
		return NULL;
	}

	snd_pcm_hw_params_get_rate(params, &wantRate, 0);
	snd_pcm_hw_params_get_channels(params, &wantChannels);

	*rate = wantRate;
	*channels = wantChannels;

	snd_pcm_prepare(pcm);
	return pcm;
}


// #pragma mark - drawing


static unsigned long
bar_pixel(Display* display, int screen, float value)
{
	// Green through amber to red with height, so a glance tells you how close
	// a band is to clipping without having to read a scale.
	int r, g, b;
	if (value < 0.6f) {
		float t = value / 0.6f;
		r = (int)(80 + t * 140);
		g = 220;
		b = (int)(120 - t * 60);
	} else {
		float t = (value - 0.6f) / 0.4f;
		if (t > 1)
			t = 1;
		r = (int)(220 + t * 30);
		g = (int)(220 - t * 150);
		b = (int)(60 - t * 30);
	}

	XColor color;
	color.red = r * 257;
	color.green = g * 257;
	color.blue = b * 257;
	color.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(display, DefaultColormap(display, screen), &color) == 0)
		return WhitePixel(display, screen);
	return color.pixel;
}


static unsigned long
solid_pixel(Display* display, int screen, int r, int g, int b)
{
	XColor color;
	color.red = r * 257;
	color.green = g * 257;
	color.blue = b * 257;
	color.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(display, DefaultColormap(display, screen), &color) == 0)
		return BlackPixel(display, screen);
	return color.pixel;
}


int
main(int argc, char** argv)
{
	const char* device = argc > 1 ? argv[1] : "default";

	Analyzer analyzer;
	if (!analyzer.IsValid()) {
		fprintf(stderr, "could not set up the analyzer\n");
		return 1;
	}

	Context context;
	context.analyzer = &analyzer;
	context.quit = false;
	context.rate = 48000;
	context.channels = 2;

	context.pcm = open_capture(device, &context.rate, &context.channels);
	if (context.pcm == NULL) {
		fprintf(stderr, "could not open capture device \"%s\": %s\n", device,
			snd_strerror(errno));
		fprintf(stderr, "pass a device name as the first argument, for "
			"example: rspectrum hw:0\n");
		return 1;
	}
	analyzer.SetSampleRate((float)context.rate);

	Display* display = XOpenDisplay(NULL);
	if (display == NULL) {
		fprintf(stderr, "could not open the display\n");
		snd_pcm_close(context.pcm);
		return 1;
	}

	int screen = DefaultScreen(display);
	unsigned long background = solid_pixel(display, screen, 18, 18, 22);

	Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
		0, 0, kWidth, kHeight, 0, background, background);

	XStoreName(display, window, "R Spectrum");
	XSelectInput(display, window, ExposureMask | StructureNotifyMask
		| KeyPressMask);

	Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &deleteWindow, 1);
	XMapWindow(display, window);

	GC gc = XCreateGC(display, window, 0, NULL);

	unsigned long gridColor = solid_pixel(display, screen, 38, 38, 46);
	unsigned long peakColor = solid_pixel(display, screen, 235, 235, 245);
	unsigned long textColor = solid_pixel(display, screen, 190, 190, 200);

	// One pixel value per bar, allocated once: XAllocColor on every frame
	// would hammer the colormap for no reason.
	const int kShades = 32;
	unsigned long shades[kShades];
	for (int i = 0; i < kShades; i++)
		shades[i] = bar_pixel(display, screen, (float)i / (kShades - 1));

	pthread_t thread;
	if (pthread_create(&thread, NULL, capture_thread, &context) != 0) {
		fprintf(stderr, "could not start the capture thread\n");
		XCloseDisplay(display);
		snd_pcm_close(context.pcm);
		return 1;
	}

	int width = kWidth;
	int height = kHeight;
	int tick = 0;
	char status[192];
	snprintf(status, sizeof(status), "listening");

	// Drawn into a pixmap and blitted, so the display never shows a
	// half-painted frame.
	Pixmap buffer = XCreatePixmap(display, window, width, height,
		DefaultDepth(display, screen));

	while (!context.quit) {
		while (XPending(display) > 0) {
			XEvent event;
			XNextEvent(display, &event);

			if (event.type == ConfigureNotify) {
				if (event.xconfigure.width != width
					|| event.xconfigure.height != height) {
					width = event.xconfigure.width;
					height = event.xconfigure.height;
					XFreePixmap(display, buffer);
					buffer = XCreatePixmap(display, window, width, height,
						DefaultDepth(display, screen));
				}
			} else if (event.type == ClientMessage) {
				if ((Atom)event.xclient.data.l[0] == deleteWindow)
					context.quit = true;
			} else if (event.type == KeyPress) {
				KeySym key = XLookupKeysym(&event.xkey, 0);
				if (key == XK_Escape || key == XK_q)
					context.quit = true;
			}
		}

		analyzer.Update();

		int plotHeight = height - kStatusHeight;
		if (plotHeight < 1)
			plotHeight = 1;

		XSetForeground(display, gc, background);
		XFillRectangle(display, buffer, gc, 0, 0, width, height);

		float span = kCeilingDb - kFloorDb;
		XSetForeground(display, gc, gridColor);
		for (float db = 12; db < span; db += 12) {
			int y = (int)(plotHeight * db / span);
			XDrawLine(display, buffer, gc, 0, y, width, y);
		}

		int count = analyzer.BarCount();
		const float* bars = analyzer.Bars();
		const float* peaks = analyzer.Peaks();

		float barWidth = (float)width / count;
		int gap = barWidth > 6 ? 2 : 1;

		for (int i = 0; i < count; i++) {
			int left = (int)(i * barWidth);
			int w = (int)barWidth - gap;
			if (w < 1)
				w = 1;

			float value = bars[i];
			int barHeight = (int)(plotHeight * value);

			if (barHeight >= 1) {
				int shade = (int)(value * (kShades - 1));
				if (shade < 0)
					shade = 0;
				if (shade >= kShades)
					shade = kShades - 1;
				XSetForeground(display, gc, shades[shade]);
				XFillRectangle(display, buffer, gc, left,
					plotHeight - barHeight, w, barHeight);
			}

			// The peak marker is what makes a transient readable: the bar
			// itself has already fallen by the time the eye gets there.
			if (peaks[i] > 0.01f) {
				int y = plotHeight - (int)(plotHeight * peaks[i]);
				XSetForeground(display, gc, peakColor);
				XFillRectangle(display, buffer, gc, left, y, w, 1);
			}
		}

		// Once a second is enough for a numeric readout, and it keeps the
		// string formatting off the 30 Hz path.
		if (++tick >= 30) {
			tick = 0;
			snprintf(status, sizeof(status),
				"%u Hz   %d bands   peak %.1f dBFS", context.rate,
				analyzer.BarCount(), analyzer.LevelDb());
		}

		XSetForeground(display, gc, textColor);
		XDrawString(display, buffer, gc, 8, height - 7, status,
			(int)strlen(status));

		XCopyArea(display, buffer, window, gc, 0, 0, width, height, 0, 0);
		XFlush(display);

		usleep(kFrameIntervalUs);
	}

	context.quit = true;
	pthread_join(thread, NULL);

	XFreePixmap(display, buffer);
	XFreeGC(display, gc);
	XDestroyWindow(display, window);
	XCloseDisplay(display);
	snd_pcm_close(context.pcm);
	return 0;
}
