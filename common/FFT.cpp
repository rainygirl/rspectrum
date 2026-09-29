/*
 * Radix-2 in-place FFT.
 *
 * Distributed under the terms of the MIT License.
 */

#include "FFT.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>


namespace rspectrum {


FFT::FFT(int size)
	:
	fSize(0),
	fBits(0),
	fReverse(0),
	fCos(0),
	fSin(0),
	fReal(0),
	fImag(0)
{
	if (size < 2 || (size & (size - 1)) != 0)
		return;

	int bits = 0;
	while ((1 << bits) < size)
		bits++;

	fReverse = (int*)malloc(sizeof(int) * size);
	fCos = (float*)malloc(sizeof(float) * (size / 2));
	fSin = (float*)malloc(sizeof(float) * (size / 2));
	float* real = (float*)malloc(sizeof(float) * size);
	float* imag = (float*)malloc(sizeof(float) * size);

	if (fReverse == 0 || fCos == 0 || fSin == 0 || real == 0 || imag == 0) {
		free(fReverse);
		free(fCos);
		free(fSin);
		free(real);
		free(imag);
		fReverse = 0;
		fCos = 0;
		fSin = 0;
		return;
	}

	fSize = size;
	fBits = bits;
	fReal = real;
	fImag = imag;

	// Bit-reversal permutation, computed once. Doing it per transform is a
	// surprising share of the cost at these sizes.
	for (int i = 0; i < size; i++) {
		int reversed = 0;
		for (int bit = 0; bit < bits; bit++) {
			if ((i & (1 << bit)) != 0)
				reversed |= 1 << (bits - 1 - bit);
		}
		fReverse[i] = reversed;
	}

	for (int i = 0; i < size / 2; i++) {
		double angle = -2.0 * M_PI * i / size;
		fCos[i] = (float)cos(angle);
		fSin[i] = (float)sin(angle);
	}
}


FFT::~FFT()
{
	free(fReverse);
	free(fCos);
	free(fSin);
	free(fReal);
	free(fImag);
}


void
FFT::Forward(float* real, float* imag) const
{
	if (fSize == 0)
		return;

	for (int i = 0; i < fSize; i++) {
		int j = fReverse[i];
		if (j <= i)
			continue;
		float t = real[i]; real[i] = real[j]; real[j] = t;
		t = imag[i]; imag[i] = imag[j]; imag[j] = t;
	}

	for (int span = 1; span < fSize; span <<= 1) {
		int step = fSize / (span * 2);
		for (int start = 0; start < fSize; start += span * 2) {
			int twiddle = 0;
			for (int i = start; i < start + span; i++) {
				int j = i + span;
				float wr = fCos[twiddle];
				float wi = fSin[twiddle];
				twiddle += step;

				float tr = real[j] * wr - imag[j] * wi;
				float ti = real[j] * wi + imag[j] * wr;

				real[j] = real[i] - tr;
				imag[j] = imag[i] - ti;
				real[i] += tr;
				imag[i] += ti;
			}
		}
	}
}


void
FFT::Magnitude(const float* input, float* magnitude) const
{
	if (fSize == 0)
		return;

	memcpy(fReal, input, sizeof(float) * fSize);
	memset(fImag, 0, sizeof(float) * fSize);

	Forward(fReal, fImag);

	// Only the first half is meaningful for a real input; the rest mirrors it.
	for (int i = 0; i < fSize / 2; i++) {
		float re = fReal[i];
		float im = fImag[i];
		magnitude[i] = (float)sqrt(re * re + im * im);
	}
}


}	// namespace rspectrum
