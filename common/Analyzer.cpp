/*
 * Samples in, display bars out.
 *
 * Distributed under the terms of the MIT License.
 */

#include "Analyzer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>


namespace rspectrum {


// How fast a bar may rise and fall, per displayed frame. Rising quickly and
// falling slowly is what makes a spectrum readable: the ear notices onsets,
// and a bar that decays as fast as it rises just flickers.
static const float kRise = 0.55f;
static const float kFall = 0.14f;

// Peak markers hang, then slide.
static const float kPeakFall = 0.035f;


Analyzer::Analyzer(int fftSize, int bars)
	:
	fFFT(fftSize),
	fSize(fftSize),
	fBars(bars),
	fSampleRate(48000.0f),
	fRing(0),
	fRingSize(0),
	fWritePos(0),
	fReadPos(0),
	fWindow(0),
	fFrame(0),
	fMagnitude(0),
	fBandStart(0),
	fBandEnd(0),
	fBars_(0),
	fPeaks(0),
	fLevelDb(kFloorDb)
{
	if (!fFFT.IsValid() || bars < 1)
		return;

	// Room for several windows, so a slow redraw does not lose audio the
	// capture thread has already delivered.
	fRingSize = fftSize * 8;

	fRing = (float*)calloc(fRingSize, sizeof(float));
	fWindow = (float*)malloc(sizeof(float) * fftSize);
	fFrame = (float*)malloc(sizeof(float) * fftSize);
	fMagnitude = (float*)malloc(sizeof(float) * (fftSize / 2));
	fBandStart = (int*)malloc(sizeof(int) * bars);
	fBandEnd = (int*)malloc(sizeof(int) * bars);
	fBars_ = (float*)calloc(bars, sizeof(float));
	fPeaks = (float*)calloc(bars, sizeof(float));

	if (fRing == 0 || fWindow == 0 || fFrame == 0 || fMagnitude == 0
		|| fBandStart == 0 || fBandEnd == 0 || fBars_ == 0 || fPeaks == 0) {
		return;
	}

	// Hann. Any of the usual windows would do; this one has a gentle enough
	// skirt that a pure tone lands in two or three bins rather than smearing
	// across the display.
	for (int i = 0; i < fftSize; i++)
		fWindow[i] = 0.5f * (1.0f - (float)cos(2.0 * M_PI * i / (fftSize - 1)));

	_BuildBands();
}


Analyzer::~Analyzer()
{
	free(fRing);
	free(fWindow);
	free(fFrame);
	free(fMagnitude);
	free(fBandStart);
	free(fBandEnd);
	free(fBars_);
	free(fPeaks);
}


bool
Analyzer::IsValid() const
{
	return fRing != 0 && fWindow != 0 && fFrame != 0 && fMagnitude != 0
		&& fBandStart != 0 && fBandEnd != 0 && fBars_ != 0 && fPeaks != 0;
}


void
Analyzer::SetSampleRate(float rate)
{
	if (rate <= 0 || rate == fSampleRate)
		return;

	fSampleRate = rate;
	_BuildBands();
}


void
Analyzer::_BuildBands()
{
	if (fBandStart == 0)
		return;

	int usable = fSize / 2;
	float binWidth = fSampleRate / fSize;

	// Bars are spaced logarithmically, because pitch is. On a linear axis
	// everything below a few kHz is crammed into the first tenth of the
	// window and the display is all but useless for music or speech.
	float low = kMinFrequency;
	float high = kMaxFrequency;
	if (high > fSampleRate / 2)
		high = fSampleRate / 2;
	if (low >= high)
		low = high / 100.0f;

	double ratio = log((double)high / low) / fBars;

	for (int i = 0; i < fBars; i++) {
		double from = low * exp(ratio * i);
		double to = low * exp(ratio * (i + 1));

		int start = (int)(from / binWidth);
		int end = (int)(to / binWidth);

		// Never an empty band: at the bottom of the scale several bars share
		// the same bin, and a bar with no bins would sit dead at zero.
		if (end <= start)
			end = start + 1;
		if (start < 1)
			start = 1;			// skip DC
		if (end > usable)
			end = usable;
		if (start >= end)
			start = end - 1;

		fBandStart[i] = start;
		fBandEnd[i] = end;
	}
}


void
Analyzer::Feed(const float* samples, int frameCount, int channels)
{
	if (fRing == 0 || samples == 0 || frameCount <= 0 || channels < 1)
		return;

	int pos = fWritePos;
	for (int i = 0; i < frameCount; i++) {
		float sum = 0;
		for (int c = 0; c < channels; c++)
			sum += samples[i * channels + c];
		fRing[pos % fRingSize] = sum / channels;
		pos++;
	}
	fWritePos = pos;
}


void
Analyzer::FeedInt16(const short* samples, int frameCount, int channels)
{
	if (fRing == 0 || samples == 0 || frameCount <= 0 || channels < 1)
		return;

	int pos = fWritePos;
	for (int i = 0; i < frameCount; i++) {
		int sum = 0;
		for (int c = 0; c < channels; c++)
			sum += samples[i * channels + c];
		fRing[pos % fRingSize] = (float)sum / (channels * 32768.0f);
		pos++;
	}
	fWritePos = pos;
}


bool
Analyzer::Update()
{
	if (!IsValid())
		return false;

	int available = fWritePos - fReadPos;

	// Fell behind: skip to the newest full window rather than working
	// through a backlog nobody will see.
	if (available > fRingSize) {
		fReadPos = fWritePos - fSize;
		available = fSize;
	}

	bool fresh = available >= fSize;

	if (fresh) {
		int start = fWritePos - fSize;
		for (int i = 0; i < fSize; i++) {
			int index = start + i;
			if (index < 0)
				index = 0;
			fFrame[i] = fRing[index % fRingSize] * fWindow[i];
		}

		// Overlap by half a window: consecutive frames then share half their
		// samples, which is what stops a transient falling between two
		// displayed frames and vanishing.
		fReadPos = fWritePos - fSize / 2;

		fFFT.Magnitude(fFrame, fMagnitude);

		float loudest = 0;
		for (int i = 0; i < fBars; i++) {
			// Peak within the band, not the mean. A mean over a wide band at
			// the top of the scale buries a single strong partial among its
			// quiet neighbours.
			float peak = 0;
			for (int bin = fBandStart[i]; bin < fBandEnd[i]; bin++) {
				if (fMagnitude[bin] > peak)
					peak = fMagnitude[bin];
			}
			if (peak > loudest)
				loudest = peak;

			// The window halves the amplitude on average and the transform
			// spreads it over Size()/2 bins; normalising here keeps a
			// full-scale sine at roughly 0 dBFS.
			float amplitude = peak * 4.0f / fSize;
			float db = amplitude > 1e-7f
				? 20.0f * (float)log10(amplitude) : kFloorDb;

			float value = (db - kFloorDb) / (kCeilingDb - kFloorDb);
			if (value < 0)
				value = 0;
			if (value > 1)
				value = 1;

			if (value > fBars_[i])
				fBars_[i] += (value - fBars_[i]) * kRise;
			else
				fBars_[i] += (value - fBars_[i]) * kFall;
		}

		float amplitude = loudest * 4.0f / fSize;
		fLevelDb = amplitude > 1e-7f
			? 20.0f * (float)log10(amplitude) : kFloorDb;
	} else {
		// No new audio: let the bars fall so a stopped source does not leave
		// the display frozen mid-song.
		for (int i = 0; i < fBars; i++)
			fBars_[i] -= fBars_[i] * kFall;
	}

	for (int i = 0; i < fBars; i++) {
		if (fBars_[i] > fPeaks[i])
			fPeaks[i] = fBars_[i];
		else {
			fPeaks[i] -= kPeakFall;
			if (fPeaks[i] < fBars_[i])
				fPeaks[i] = fBars_[i];
			if (fPeaks[i] < 0)
				fPeaks[i] = 0;
		}
	}

	return fresh;
}


}	// namespace rspectrum
