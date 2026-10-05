/* A2DP: the headset's audio sink, set up over AVDTP and fed SBC audio.
 *
 * SBC is the one codec every A2DP headset must support. The stream runs at
 * 48 kHz when the headset allows it, which matches the console's capture;
 * otherwise at 44.1 kHz, resampled.
 */
#ifndef FGG_A2DP_H
#define FGG_A2DP_H

/* Finds the headset's SBC sink, configures it and starts the stream.
 * Returns 0 on failure, with the reason logged. */
int  a2dp_start(void);

/* Captures the console's audio and streams it until the headset goes away.
 * `capturing` says whether capture_open() found audio to capture; if not,
 * silence is sent until it does. Returns 1 if any audio was sent. */
int  a2dp_stream(int capturing);

/* Closes the stream and its channels, if the headset is still there. */
void a2dp_stop(void);

#endif
