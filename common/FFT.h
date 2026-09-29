/*
 * Radix-2 in-place FFT.
 *
 * Plain C++ with no dependencies: the point of this file is that the same
 * object compiles unchanged on Haiku, Windows, Linux and macOS, so nothing
 * here may reach for a platform header.
 *
 * Distributed under the terms of the MIT License.
 */
#ifndef RSPECTRUM_FFT_H
#define RSPECTRUM_FFT_H


namespace rspectrum {


class FFT {
public:
	// size must be a power of two.
							FFT(int size);
							~FFT();

			bool			IsValid() const { return fReal != 0; }
			int				Size() const { return fSize; }

	// Transforms in place. real[] and imag[] must each hold Size() values.
			void			Forward(float* real, float* imag) const;

	// Magnitude spectrum of a real-valued signal.
	//
	// input[] holds Size() samples; magnitude[] receives Size()/2 values,
	// one per bin from DC up to just below Nyquist. The input is left
	// untouched, which matters because the caller usually wants to keep the
	// windowed copy around for the next overlapping frame.
			void			Magnitude(const float* input,
								float* magnitude) const;

private:
			int				fSize;
			int				fBits;
			int*			fReverse;	// bit-reversal permutation
			float*			fCos;		// twiddles, one per stage step
			float*			fSin;
	mutable	float*			fReal;		// scratch for Magnitude()
	mutable	float*			fImag;
};


}	// namespace rspectrum


#endif	// RSPECTRUM_FFT_H
