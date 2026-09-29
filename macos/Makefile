# Builds "R Spectrum.app".
#
# A bundle, not a bare executable: macOS refuses microphone access to a
# process with no Info.plist carrying NSMicrophoneUsageDescription, and the
# refusal is silent -- the tap installs and delivers nothing but zeros.
#
# Every rule is phony and the bundle path is quoted throughout: the app name
# contains a space, and make splits target and prerequisite lists on
# whitespace no matter how they are escaped.

APP        = R Spectrum.app
SRC        = main.mm ../common/FFT.cpp ../common/Analyzer.cpp
HEADERS    = ../common/Analyzer.h ../common/FFT.h
FLAGS      = -O2 -fobjc-arc -Wall
FRAMEWORKS = -framework Cocoa -framework AVFoundation

all:
	@mkdir -p "$(APP)/Contents/MacOS"
	clang++ $(FLAGS) -o "$(APP)/Contents/MacOS/RSpectrum" $(SRC) $(FRAMEWORKS)
	@cp Info.plist "$(APP)/Contents/Info.plist"
	@echo "Built $(APP)"

run: all
	open "$(APP)"

clean:
	rm -rf "$(APP)"

.PHONY: all run clean
