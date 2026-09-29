/*
 * R Spectrum for Haiku -- a real-time audio spectrum analyzer.
 *
 * The analysis is in ../common; this file is only the two things that cannot
 * be shared: getting audio out of the Media Kit and putting bars on screen.
 *
 * Distributed under the terms of the MIT License.
 */

#include "../common/Analyzer.h"

#include <Alert.h>
#include <Application.h>
#include <LocaleRoster.h>
#include <MediaDefs.h>
#include <MediaRecorder.h>
#include <MediaRoster.h>
#include <MessageRunner.h>
#include <String.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>


static const char* const kAppSignature = "application/x-vnd.RSpectrum";

static const uint32 kMsgTick = 'tick';

// 33 ms. Fast enough that the bars move continuously, slow enough that
// redrawing 48 of them is not itself a load on a 1.33 GHz single core.
static const bigtime_t kFrameInterval = 33000;


// #pragma mark - strings


enum string_id {
	kStrTitle = 0, kStrListening, kStrNoInput, kStrStatusFmt,
	kStringCount
};

static const char* const kStringsEn[kStringCount] = {
	"R Spectrum", "listening", "No audio input device",
	"%g Hz   %" B_PRId32 " bands   peak %.1f dBFS"
};

static const char* const kStringsKo[kStringCount] = {
	"R Spectrum", "듣는 중", "오디오 입력 장치 없음",
	"%g Hz   대역 %" B_PRId32 "개   피크 %.1f dBFS"
};

static const char* const* sStrings = kStringsEn;

static inline const char*
T(string_id id)
{
	return sStrings[id];
}


// #pragma mark - SpectrumView


class SpectrumView : public BView {
public:
							SpectrumView(BRect frame,
								rspectrum::Analyzer* analyzer);

	virtual	void			Draw(BRect updateRect);

private:
			rgb_color		_BarColor(float value) const;

			rspectrum::Analyzer*	fAnalyzer;
};


SpectrumView::SpectrumView(BRect frame, rspectrum::Analyzer* analyzer)
	:
	BView(frame, "spectrum", B_FOLLOW_ALL, B_WILL_DRAW),
	fAnalyzer(analyzer)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


rgb_color
SpectrumView::_BarColor(float value) const
{
	// Green through amber to red with height, so a glance tells you how close
	// a band is to clipping without having to read a scale.
	rgb_color color;
	color.alpha = 255;

	if (value < 0.6f) {
		float t = value / 0.6f;
		color.red = (uint8)(80 + t * 140);
		color.green = 220;
		color.blue = (uint8)(120 - t * 60);
	} else {
		float t = (value - 0.6f) / 0.4f;
		if (t > 1)
			t = 1;
		color.red = (uint8)(220 + t * 30);
		color.green = (uint8)(220 - t * 150);
		color.blue = (uint8)(60 - t * 30);
	}

	return color;
}


void
SpectrumView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();

	SetHighColor(18, 18, 22);
	FillRect(bounds);

	if (fAnalyzer == NULL || !fAnalyzer->IsValid())
		return;

	int32 count = fAnalyzer->BarCount();
	const float* bars = fAnalyzer->Bars();
	const float* peaks = fAnalyzer->Peaks();

	// Horizontal rules every 12 dB down from the ceiling, behind the bars.
	float span = rspectrum::kCeilingDb - rspectrum::kFloorDb;
	SetHighColor(38, 38, 46);
	for (float db = 12; db < span; db += 12) {
		float y = bounds.top + bounds.Height() * db / span;
		if (y > bounds.top && y < bounds.bottom)
			StrokeLine(BPoint(bounds.left, y), BPoint(bounds.right, y));
	}

	float width = (bounds.Width() + 1) / count;
	float gap = width > 6 ? 2 : 1;

	for (int32 i = 0; i < count; i++) {
		float left = bounds.left + i * width;
		float right = left + width - gap;
		if (right <= left)
			right = left;

		float value = bars[i];
		float height = bounds.Height() * value;

		if (height >= 1) {
			SetHighColor(_BarColor(value));
			FillRect(BRect(left, bounds.bottom - height, right,
				bounds.bottom));
		}

		// The peak marker is what makes a transient readable: the bar itself
		// has already fallen by the time the eye gets there.
		float peak = peaks[i];
		if (peak > 0.01f) {
			float y = bounds.bottom - bounds.Height() * peak;
			SetHighColor(235, 235, 245);
			StrokeLine(BPoint(left, y), BPoint(right, y));
		}
	}
}


// #pragma mark - SpectrumWindow


class SpectrumWindow : public BWindow {
public:
							SpectrumWindow();
	virtual					~SpectrumWindow();

	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();

			void			AudioArrived(void* data, size_t size,
								const media_format& format);

private:
			status_t		_StartCapture();
			void			_StopCapture();

			rspectrum::Analyzer	fAnalyzer;
			SpectrumView*		fView;
			BStringView*		fStatus;
			BMessageRunner*		fRunner;

			BMediaRecorder*		fRecorder;
			media_node			fInput;
			bool				fHaveInput;
			int32				fChannels;
};


static void
record_hook(void* cookie, bigtime_t, void* data, size_t size,
	const media_format& format)
{
	((SpectrumWindow*)cookie)->AudioArrived(data, size, format);
}


SpectrumWindow::SpectrumWindow()
	:
	BWindow(BRect(120, 120, 680, 480), T(kStrTitle), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS),
	fView(NULL),
	fStatus(NULL),
	fRunner(NULL),
	fRecorder(NULL),
	fHaveInput(false),
	fChannels(1)
{
	BRect bounds = Bounds();

	BRect viewFrame = bounds;
	viewFrame.bottom -= 22;
	fView = new SpectrumView(viewFrame, &fAnalyzer);
	AddChild(fView);

	fStatus = new BStringView(BRect(10, bounds.bottom - 18,
		bounds.right - 10, bounds.bottom - 2), "status", T(kStrListening),
		B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
	AddChild(fStatus);

	if (_StartCapture() != B_OK)
		fStatus->SetText(T(kStrNoInput));

	fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgTick),
		kFrameInterval);
}


SpectrumWindow::~SpectrumWindow()
{
	delete fRunner;
	_StopCapture();
}


status_t
SpectrumWindow::_StartCapture()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return B_ERROR;

	status_t status = roster->GetAudioInput(&fInput);
	if (status != B_OK)
		return status;
	fHaveInput = true;

	fRecorder = new BMediaRecorder("R Spectrum", B_MEDIA_RAW_AUDIO);
	if (fRecorder->InitCheck() != B_OK) {
		status = fRecorder->InitCheck();
		delete fRecorder;
		fRecorder = NULL;
		return status;
	}

	fRecorder->SetHooks(record_hook, NULL, this);

	// Wildcard: let the hardware pick, then adapt. Forcing a rate here is how
	// you get a silent connection on a device that does not offer it.
	media_format format;
	memset(&format, 0, sizeof(format));
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;

	status = fRecorder->Connect(fInput, NULL, &format);
	if (status != B_OK) {
		delete fRecorder;
		fRecorder = NULL;
		return status;
	}

	media_format got = fRecorder->AcceptedFormat();
	fChannels = got.u.raw_audio.channel_count > 0
		? got.u.raw_audio.channel_count : 1;
	fAnalyzer.SetSampleRate(got.u.raw_audio.frame_rate);

	status = fRecorder->Start();
	if (status != B_OK) {
		fRecorder->Disconnect();
		delete fRecorder;
		fRecorder = NULL;
		return status;
	}

	return B_OK;
}


void
SpectrumWindow::_StopCapture()
{
	if (fRecorder == NULL)
		return;

	fRecorder->Stop();
	fRecorder->Disconnect();
	delete fRecorder;
	fRecorder = NULL;
}


void
SpectrumWindow::AudioArrived(void* data, size_t size,
	const media_format& format)
{
	int32 channels = format.u.raw_audio.channel_count;
	if (channels < 1)
		channels = fChannels;

	// The driver on this machine is set to 16-bit capture; handle float too,
	// since nothing stops another device negotiating it.
	if (format.u.raw_audio.format == media_raw_audio_format::B_AUDIO_SHORT) {
		int32 frames = (int32)(size / sizeof(short) / channels);
		fAnalyzer.FeedInt16((const short*)data, frames, channels);
	} else if (format.u.raw_audio.format
			== media_raw_audio_format::B_AUDIO_FLOAT) {
		int32 frames = (int32)(size / sizeof(float) / channels);
		fAnalyzer.Feed((const float*)data, frames, channels);
	}
}


void
SpectrumWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTick:
		{
			fAnalyzer.Update();
			fView->Invalidate();

			// Once a second is enough for a numeric readout, and it keeps the
			// string formatting off the 30 Hz path.
			static int32 tick = 0;
			if (++tick >= 30) {
				tick = 0;
				if (fHaveInput) {
					char text[192];
					snprintf(text, sizeof(text), T(kStrStatusFmt),
						fAnalyzer.SampleRate(), fAnalyzer.BarCount(),
						fAnalyzer.LevelDb());
					fStatus->SetText(text);
				}
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
SpectrumWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


// #pragma mark -


static void
choose_language()
{
	BMessage preferred;
	if (BLocaleRoster::Default()->GetPreferredLanguages(&preferred) != B_OK)
		return;

	const char* language = NULL;
	for (int32 i = 0;
			preferred.FindString("language", i, &language) == B_OK; i++) {
		if (language != NULL && strncmp(language, "ko", 2) == 0) {
			sStrings = kStringsKo;
			return;
		}
	}
}


int
main(void)
{
	BApplication app(kAppSignature);

	choose_language();

	SpectrumWindow* window = new SpectrumWindow();
	window->Show();
	app.Run();
	return 0;
}
