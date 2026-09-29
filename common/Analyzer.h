/*
 * Samples in, display bars out.
 *
 * Everything a spectrum display needs that is not platform-specific lives
 * here: the sample ring the capture callback writes into, windowing, the
 * transform, the conversion to decibels, the mapping onto a logarithmic
 * frequency axis, and the smoothing that stops the result flickering.
 *
 * The split matters because the four platforms differ only in how audio
 * arrives and how pixels are drawn. Anything else in a platform directory
 * would be duplicated four times and would drift.
 *
 * Distributed under the terms of the MIT License.
 */
#ifndef RSPECTRUM_ANALYZER_H
#define RSPECTRUM_ANALYZER_H


#include "FFT.h"


namespace rspectrum {


// Defaults chosen for a 1.33 GHz in-order CPU: a 1024-point transform at
// 48 kHz is a 21 ms window and about 46 bins per octave at the top end,
// which is plenty for a bar display and cheap enough to run at 30 fps
// without the machine noticing.
const int kDefaultFFTSize = 1024;
const int kDefaultBars = 48;

const float kMinFrequency = 40.0f;
const float kMaxFrequency = 16000.0f;

const float kFloorDb = -78.0f;
const float kCeilingDb = -6.0f;


class Analyzer {
public:
							Analyzer(int fftSize = kDefaultFFTSize,
								int bars = kDefaultBars);
							~Analyzer();

			bool			IsValid() const;

	// Set once the capture format is known; safe to call again if it
	// changes.
			void			SetSampleRate(float rate);
			float			SampleRate() const { return fSampleRate; }

			int				BarCount() const { return fBars; }

	// Called from the audio thread. Interleaved frames are mixed down to
	// mono; channels may be 1 or more.
			void			Feed(const float* samples, int frameCount,
								int channels);
			void			FeedInt16(const short* samples, int frameCount,
								int channels);

	// Called from the drawing thread, once per displayed frame. Returns
	// false when not enough audio has arrived yet to fill a window, in which
	// case the previous bars are still valid and simply decay.
			bool			Update();

	// Both arrays hold BarCount() values in 0..1, ready to scale to pixels.
			const float*	Bars() const { return fBars_; }
			const float*	Peaks() const { return fPeaks; }

	// Peak level of the most recent window, in dBFS. Useful for a readout
	// and for telling silence from a dead capture path.
			float			LevelDb() const { return fLevelDb; }

private:
			void			_BuildBands();

			FFT				fFFT;
			int				fSize;
			int				fBars;
			float			fSampleRate;

			float*			fRing;		// captured mono samples
			int				fRingSize;
	volatile int			fWritePos;
			int				fReadPos;

			float*			fWindow;	// Hann coefficients
			float*			fFrame;		// windowed copy handed to the FFT
			float*			fMagnitude;

			int*			fBandStart;	// first bin of each bar
			int*			fBandEnd;	// one past the last bin

			float*			fBars_;
			float*			fPeaks;
			float			fLevelDb;
};


}	// namespace rspectrum


#endif	// RSPECTRUM_ANALYZER_H
