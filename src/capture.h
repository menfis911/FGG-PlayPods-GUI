/* The console's own audio, as Remote Play hears it.
 *
 * libSceAvcap2 is the client side of the system's capture service
 * (av_capture_manager), which feeds recording, broadcasting, Share Screen and
 * Remote Play. Opened the way Remote Play opens it, the stream carries the
 * game and the system sounds alike and keeps going while the PS menu is up.
 *
 * The audio is 48 kHz, stereo, interleaved float32, delivered in records of
 * 1024 frames (8192 bytes).
 */
#ifndef FGG_CAPTURE_H
#define FGG_CAPTURE_H

#include <stddef.h>

#define CAPTURE_RATE      48000
#define CAPTURE_CHANNELS  2
#define CAPTURE_RECORD    8192    /* bytes per record */

/* Loads the libraries and starts capturing. Returns 1 when capturing, 0 when
 * the libraries are ready but the service has nothing to capture yet (retry
 * with capture_restart), -1 on failure. */
int  capture_open(void);

/* Reads one record without waiting. Returns its size in bytes, 0 when none
 * is ready, or -1 when the service ended the capture (restart it). */
int  capture_read(float *buf, size_t size);

/* Tears the session down and starts a new one. Returns 1 when capturing. */
int  capture_restart(void);

void capture_close(void);

/* Times the service got a full ring ahead of us and audio was skipped. */
long capture_overruns(void);

#endif
