#include "backend.h"
#include "web_assets.h"

#include <signal.h>
#include <string.h>

int http_server_run(unsigned short port, volatile int *running);
static volatile int running = 1;

static void stop_server(int signal_number)
{
    (void)signal_number;
    running = 0;
}

const char *backend_status_name(backend_status status)
{
    (void)status;
    return "streaming";
}

const char *a2dp_profile_name(a2dp_profile profile)
{
    return profile == A2DP_PROFILE_LOW_LATENCY ? "low_latency" : "stable";
}

void backend_get_snapshot(backend_snapshot *out)
{
    memset(out, 0, sizeof *out);
    out->status = BACKEND_STREAMING;
    out->controller_ready = 1;
    out->selected_profile = A2DP_PROFILE_LOW_LATENCY;
    out->audio.profile = A2DP_PROFILE_STABLE;
    out->audio.streaming = 1;
    out->audio.packet_duration_us = 18666;
    out->audio.queue_ms = 16;
    out->audio.max_queue_ms = 85;
    out->audio.target_max_queue_ms = 250;
    out->audio.target_keep_queue_ms = 80;
    out->audio.packet_send_rate = 54;
    out->audio.completion_reports = 120;
    out->audio.assumed_completions = 2;
    out->audio.missing_reports = 2;
    out->state_revision = 3;
    out->devices_revision = 4;
    out->saved_revision = 5;
}

int backend_request_scan(void) { return 1; }
int backend_request_connect(const char *mac) { return mac != 0; }
int backend_request_disconnect(void) { return 1; }
int backend_set_audio_profile(const char *profile)
{
    return profile && (!strcmp(profile, "stable") ||
                       !strcmp(profile, "low_latency"));
}

int web_asset_find(const char *path, web_asset *asset)
{
    static const unsigned char page[] = "test";
    if (strcmp(path, "/") != 0) return 0;
    asset->content_type = "text/plain";
    asset->data = page;
    asset->size = sizeof page - 1;
    return 1;
}

void log_line(const char *format, ...) { (void)format; }

int main(void)
{
    signal(SIGTERM, stop_server);
    signal(SIGINT, stop_server);
    return http_server_run(18197, &running) ? 0 : 1;
}
