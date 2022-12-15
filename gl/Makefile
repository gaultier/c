LDFLAGS := -framework AppKit -framework AudioToolbox -framework Carbon -framework Cocoa  -framework CoreAudio -framework CoreFoundation -framework CoreGraphics -framework CoreHaptics -framework CoreServices -framework CoreVideo -framework ForceFeedback -framework Foundation -framework GameController -framework IOKit -framework Metal -framework OpenGL -framework QuartzCore -liconv -lobjc

CFLAGS := -O2 -march=native -g -Weverything -isystem/usr/local/include -Wno-deprecated

gl: gl.c /usr/local/lib/libsdl2.a
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

