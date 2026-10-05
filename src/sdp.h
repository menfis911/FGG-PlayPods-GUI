/* A minimal SDP server: one record, "this is an A2DP audio source".
 *
 * Headsets look up the source's service record as soon as they connect. Only
 * the three request types they send are answered, and responses always fit
 * in one PDU, so continuation state is never produced.
 */
#ifndef FGG_SDP_H
#define FGG_SDP_H

/* Answers one SDP request PDU. Returns the response length written to `rsp`,
 * or 0 if there is nothing to send. */
int sdp_handle(const unsigned char *req, int len, unsigned char *rsp, int max);

#endif
