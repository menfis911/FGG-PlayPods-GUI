#include <stdint.h>

/* The Bluetooth link to the audio device: controller setup, pairing and
 * reconnection, encryption, L2CAP channels and ACL flow control.
 *
 * Everything runs on one thread: callers pump the controller with bt_poll()
 * or bt_wait(), which dispatch whatever arrived. Only events about the
 * selected audio device are acted on; the controller is shared with the system, whose
 * links on it are left alone.
 */
#ifndef FGG_BT_H
#define FGG_BT_H

/* One L2CAP channel. `name` and `scid` (our end) are set by the owner. */
typedef struct {
    const char *name;
    unsigned scid, dcid;
    int conn_done, conn_result;
    int cfg_rsp_ok, cfg_req_done, closed;
    unsigned remote_mtu;
} l2cap_chan;

/* Receives L2CAP frames on channels opened with bt_open_channel. */
typedef void (*bt_frame_fn)(unsigned cid, const unsigned char *data, int len);

#define BT_MAX_DEVICES 32

typedef struct {
    int valid;
    unsigned char addr[6];
    unsigned cod;
    int8_t rssi;
    char name[64];
} bt_device;

int bt_scan_start(void);
int bt_scan_poll(void);
void bt_scan_cancel(void);
int bt_device_count(void);
const bt_device *bt_device_get(int index);
int bt_connect_device(int index, const char *key_path);
int bt_connect_addr(const unsigned char addr[6], const char *name,
                    const char *key_path);
void bt_request_stop(void);
void bt_clear_stop(void);

/* Opens the controller and prepares it. Returns 0 on failure. */
int  bt_start(void);

/* Disconnects the audio device if still linked and closes the controller. */
void bt_stop(void);

/* Connects to the audio device paired before (its key is kept in key_path), or
 * pairs one found in pairing mode, and encrypts the link. Returns 0 on
 * failure, with the reason logged. */
int  bt_connect(const char *key_path);

/* Opens an L2CAP channel to `psm` on the audio device and configures it. Frames
 * that arrive on it go to `on_frame`. Returns 0 on failure. */
int  bt_open_channel(l2cap_chan *ch, unsigned psm, bt_frame_fn on_frame);
void bt_close_channel(l2cap_chan *ch);

/* Sends one L2CAP frame to the audio device's channel `dcid`. */
int  bt_send(unsigned dcid, const unsigned char *data, int len);

/* True while the controller has room for another ACL packet. */
int  bt_can_send(void);

/* Largest L2CAP frame one ACL packet carries. */
int  bt_max_frame(void);

/* Pumps the controller for up to timeout_ms and dispatches what arrived. */
void bt_poll(int timeout_ms);

/* Pumps until *flag is set, the link drops, or timeout_ms passes. Returns
 * 1 if the flag was set. */
int  bt_wait(volatile int *flag, int timeout_ms);

/* True once the audio device has disconnected. */
int  bt_link_lost(void);

/* Called about once a second while the link layer waits or streams. */
void bt_set_tick(void (*fn)(void));

/* ACL packets taken as sent after their completion report went astray. */
long bt_completions_assumed(void);
int  bt_credits(void);

/* Packets sent minus packets reported sent: the reports the system's driver
 * took, plus whatever is in the controller right now. */
long bt_reports_missing(void);

#endif
