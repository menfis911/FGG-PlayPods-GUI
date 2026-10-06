/* HCI over USB to the PlayStation 5's second Bluetooth controller.
 *
 * The console's wireless chip exposes two complete Bluetooth controllers on
 * one USB device. The system runs the DualSense on the second one; the first
 * is shared with it here. Nothing is detached from the system's driver:
 * detaching any of the chip's interfaces takes the DualSense down with it.
 *
 * Sharing is inherently lossy: the system driver and this process race for
 * packets on the same endpoints.  Keep only one read pending per IN endpoint
 * to minimise packets stolen from the system.  Safe coexistence still needs
 * hardware validation; callers must stop on sustained missing completions.
 */
#ifndef FGG_HCI_H
#define FGG_HCI_H

#define HCI_PKT_MAX 1100

/* Opens the controller alongside the system's driver. Returns 0 on failure. */
int  hci_open(void);
void hci_close(void);

/* Sends an HCI command. Returns 0 on a transport error. */
int  hci_cmd(unsigned opcode, const void *params, int plen);

/* Sends one complete ACL packet (4-byte header included), waiting up to a
 * second for the previous one to leave. Returns 0 on error or timeout. */
int  hci_acl_send(const unsigned char *pkt, int len);

/* Waits up to timeout_ms for incoming packets and queues them. Returns 1 if
 * a packet is waiting in either queue. */
int  hci_pump(int timeout_ms);

/* Pops one queued packet and returns its length, or 0 if none. */
int  hci_next_event(unsigned char *out, int max);
int  hci_next_acl(unsigned char *out, int max);

#endif
