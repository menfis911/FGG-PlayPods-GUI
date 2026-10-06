/* A2DP: the Bluetooth audio device's sink, set up over AVDTP and fed SBC audio.
 *
 * SBC is the codec every A2DP sink must support. The stream runs at
 * 48 kHz when the audio device allows it, which matches the console's capture;
 * otherwise at 44.1 kHz, resampled.
 */
#ifndef FGG_A2DP_H
#define FGG_A2DP_H

typedef enum {
    A2DP_PROFILE_STABLE = 0,
    A2DP_PROFILE_LOW_LATENCY
} a2dp_profile;

typedef struct {
    a2dp_profile profile;
    int streaming;
    int sample_rate;
    int bitpool;
    int sbc_frames_per_packet;
    int packet_duration_us;
    int queue_ms;
    int max_queue_ms;
    int target_max_queue_ms;
    int target_keep_queue_ms;
    long packets;
    long capture_records;
    long trimmed_frames;
    long capture_overruns;
    int capture_restarts;
    int packet_send_rate;
    long completion_reports;
    long assumed_completions;
    long missing_reports;
    long stall_ms;
    char safety_stop_reason[96];
} a2dp_metrics;

void a2dp_set_profile(a2dp_profile profile);
a2dp_profile a2dp_get_profile(void);
const char *a2dp_profile_name(a2dp_profile profile);
void a2dp_get_metrics(a2dp_metrics *out);

/* Finds the audio device's SBC sink, configures it and starts the stream.
 * Returns 0 on failure, with the reason logged. */
int  a2dp_start(void);

/* Captures the console's audio and streams it until the audio device goes away.
 * `capturing` says whether capture_open() found audio to capture; if not,
 * silence is sent until it does. Returns 1 if any audio was sent. */
int  a2dp_stream(int capturing);
void a2dp_request_stop(void);
void a2dp_clear_stop(void);
const char *a2dp_safety_reason(void);

/* Closes the stream and its channels, if the audio device is still there. */
void a2dp_stop(void);

#endif
