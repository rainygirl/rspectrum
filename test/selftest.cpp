/*
 * Silent verification of the shared analysis code.
 *
 * Synthesised sine waves are fed straight into the Analyzer and the resulting
 * bars are checked: no audio device is opened and nothing is played. This is
 * also a stricter check than listening would be -- it asserts *which* band a
 * known frequency lands in, which the ear cannot do.
 *
 * Distributed under the terms of the MIT License.
 */

#include "../common/Analyzer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

using namespace rspectrum;

static int sFailures = 0;

static void
check(bool ok, const char* what)
{
	printf("  %-52s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok)
		sFailures++;
}


// Which bar covers a given frequency, from the same log spacing the Analyzer
// uses. Computed here independently so a mistake in one is not mirrored.
static int
expected_bar(float hz, int bars, float rate)
{
	float low = kMinFrequency;
	float high = kMaxFrequency;
	if (high > rate / 2)
		high = rate / 2;
	double ratio = log((double)high / low) / bars;
	int index = (int)(log((double)hz / low) / ratio);
	if (index < 0)
		index = 0;
	if (index >= bars)
		index = bars - 1;
	return index;
}


static int
loudest_bar(const Analyzer& a)
{
	const float* bars = a.Bars();
	int best = 0;
	for (int i = 1; i < a.BarCount(); i++) {
		if (bars[i] > bars[best])
			best = i;
	}
	return best;
}


static void
feed_sine(Analyzer& a, float hz, float rate, float amplitude, int frames)
{
	static double phase = 0;
	float* buffer = (float*)malloc(sizeof(float) * frames);
	double step = 2.0 * M_PI * hz / rate;
	for (int i = 0; i < frames; i++) {
		buffer[i] = (float)(sin(phase) * amplitude);
		phase += step;
	}
	a.Feed(buffer, frames, 1);
	free(buffer);
}


int
main(void)
{
	const float rate = 48000.0f;

	printf("FFT\n");
	{
		FFT fft(1024);
		check(fft.IsValid(), "1024 is accepted");
		FFT odd(1000);
		check(!odd.IsValid(), "a non-power-of-two is rejected");

		// A sine at exactly bin 64 must put all its energy in bin 64.
		float* input = (float*)malloc(sizeof(float) * 1024);
		float* mag = (float*)malloc(sizeof(float) * 512);
		for (int i = 0; i < 1024; i++)
			input[i] = (float)sin(2.0 * M_PI * 64 * i / 1024);
		fft.Magnitude(input, mag);
		int peak = 0;
		for (int i = 1; i < 512; i++) {
			if (mag[i] > mag[peak])
				peak = i;
		}
		check(peak == 64, "a bin-aligned sine peaks in that bin");
		free(input);
		free(mag);
	}

	printf("Analyzer\n");
	{
		Analyzer a;
		check(a.IsValid(), "constructs");
		a.SetSampleRate(rate);
		check(a.BarCount() == kDefaultBars, "reports its bar count");

		// Silence must read as silence, not as a floor of noise.
		for (int i = 0; i < 40; i++) {
			feed_sine(a, 1000, rate, 0.0f, 512);
			a.Update();
		}
		check(a.LevelDb() <= kFloorDb + 1.0f, "silence reads at the floor");
	}

	const float tones[] = { 100, 250, 440, 1000, 2500, 5000, 9000 };
	for (int t = 0; t < 7; t++) {
		Analyzer a;
		a.SetSampleRate(rate);

		// Long enough for the smoothing to settle on the steady value.
		for (int i = 0; i < 60; i++) {
			feed_sine(a, tones[t], rate, 0.5f, 512);
			a.Update();
		}

		int want = expected_bar(tones[t], a.BarCount(), rate);
		int got = loudest_bar(a);
		char label[96];
		snprintf(label, sizeof(label), "%.0f Hz peaks in band %d (got %d)",
			tones[t], want, got);
		// One band either side: a log axis puts band edges between bins, so a
		// tone near an edge can legitimately land on the neighbour.
		check(abs(got - want) <= 1, label);
	}

	printf("Level\n");
	{
		Analyzer a;
		a.SetSampleRate(rate);
		for (int i = 0; i < 60; i++) {
			feed_sine(a, 1000, rate, 1.0f, 512);
			a.Update();
		}
		float full = a.LevelDb();
		char label[96];
		snprintf(label, sizeof(label), "full-scale sine reads near 0 dBFS "
			"(got %.1f)", full);
		check(full > -6.0f && full < 3.0f, label);

		Analyzer b;
		b.SetSampleRate(rate);
		for (int i = 0; i < 60; i++) {
			feed_sine(b, 1000, rate, 0.05f, 512);
			b.Update();
		}
		float quiet = b.LevelDb();
		snprintf(label, sizeof(label), "-26 dBFS sine reads near -26 (got %.1f)",
			quiet);
		check(quiet > -32.0f && quiet < -20.0f, label);
	}

	printf("\n%s\n", sFailures == 0 ? "all checks passed"
		: "SOME CHECKS FAILED");
	return sFailures == 0 ? 0 : 1;
}
