#include "sdp.h"
#include "log.h"

#include <string.h>

#define PDU_ERROR                 0x01
#define PDU_SEARCH_REQ            0x02
#define PDU_SEARCH_RSP            0x03
#define PDU_ATTR_REQ              0x04
#define PDU_ATTR_RSP              0x05
#define PDU_SEARCH_ATTR_REQ       0x06
#define PDU_SEARCH_ATTR_RSP       0x07

#define ERR_INVALID_HANDLE        0x0002
#define ERR_INVALID_SYNTAX        0x0003

#define RECORD_HANDLE 0x00010001u

/* The A2DP Source record, attribute by attribute, already encoded as SDP
 * data elements. */
typedef struct {
    unsigned id;
    const unsigned char *value;
    int len;
} attr;

static const unsigned char v_handle[] = { 0x0A, 0x00, 0x01, 0x00, 0x01 };
static const unsigned char v_class[] = {            /* Audio Source */
    0x35, 0x03, 0x19, 0x11, 0x0A };
static const unsigned char v_protocols[] = {        /* L2CAP psm 25, AVDTP 1.3 */
    0x35, 0x10,
      0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x19,
      0x35, 0x06, 0x19, 0x00, 0x19, 0x09, 0x01, 0x03 };
static const unsigned char v_browse[] = {           /* PublicBrowseRoot */
    0x35, 0x03, 0x19, 0x10, 0x02 };
static const unsigned char v_profiles[] = {         /* A2DP 1.3 */
    0x35, 0x08,
      0x35, 0x06, 0x19, 0x11, 0x0D, 0x09, 0x01, 0x03 };
static const unsigned char v_name[] = {
    0x25, 0x0C, 'F', 'G', 'G', '-', 'P', 'l', 'a', 'y', 'P', 'o', 'd', 's' };
static const unsigned char v_features[] = {         /* player */
    0x09, 0x00, 0x01 };

static const attr k_record[] = {
    { 0x0000, v_handle,    (int)sizeof v_handle },
    { 0x0001, v_class,     (int)sizeof v_class },
    { 0x0004, v_protocols, (int)sizeof v_protocols },
    { 0x0005, v_browse,    (int)sizeof v_browse },
    { 0x0009, v_profiles,  (int)sizeof v_profiles },
    { 0x0100, v_name,      (int)sizeof v_name },
    { 0x0311, v_features,  (int)sizeof v_features },
};
#define N_ATTRS ((int)(sizeof k_record / sizeof k_record[0]))

/* UUIDs a search pattern may name that should find this record. */
static const unsigned k_uuids[] = { 0x110A, 0x110D, 0x0019, 0x0100, 0x1002 };

static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned be32(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}
static void put_be16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put_be32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* Parses one data element header. Returns the header length and sets the
 * type and content length, or returns 0 if it does not fit. */
static int de_header(const unsigned char *p, int avail, int *type, int *clen)
{
    static const int fixed[5] = { 1, 2, 4, 8, 16 };
    int idx;

    if (avail < 1) return 0;
    *type = p[0] >> 3;
    idx = p[0] & 7;

    if (*type == 0) { *clen = 0; return 1; }
    if (idx < 5) { *clen = fixed[idx]; return 1 + *clen <= avail ? 1 : 0; }
    if (idx == 5) { if (avail < 2) return 0; *clen = p[1]; return 2 + *clen <= avail ? 2 : 0; }
    if (idx == 6) { if (avail < 3) return 0; *clen = (int)be16(p + 1); return 3 + *clen <= avail ? 3 : 0; }
    if (avail < 5) return 0;
    *clen = (int)be32(p + 1);
    return 5 + *clen <= avail ? 5 : 0;
}

/* Does the pattern (a sequence of UUIDs) name anything this record offers?
 * Returns 1 if it does, 0 if not, -1 if malformed; sets *used. */
static int pattern_matches(const unsigned char *p, int avail, int *used)
{
    int type, clen, h = de_header(p, avail, &type, &clen), i, match = 0;

    if (!h || type != 6) return -1;
    *used = h + clen;

    for (i = h; i < h + clen; ) {
        int t, l, hh = de_header(p + i, h + clen - i, &t, &l);
        unsigned u = 0;
        int k;

        if (!hh || t != 3) return -1;
        if (l == 2) u = be16(p + i + hh);
        else if (l == 4) u = be32(p + i + hh);
        else if (l == 16) u = be32(p + i + hh);   /* base UUID: short form first */
        for (k = 0; k < (int)(sizeof k_uuids / sizeof k_uuids[0]); k++)
            if (u == k_uuids[k]) match = 1;
        i += hh + l;
    }
    return match;
}

/* Encodes the record's attributes that fall in the requested ID list as one
 * data element sequence. Returns its length, or -1 if the list is malformed
 * or the output does not fit. */
static int build_attrs(const unsigned char *ids, int avail, int *used,
                       unsigned char *out, int max)
{
    int type, clen, h = de_header(ids, avail, &type, &clen), a, n = 0;

    if (!h || type != 6) return -1;
    *used = h + clen;
    if (max < 3) return -1;
    n = 3;      /* room for a 16-bit-length sequence header */

    for (a = 0; a < N_ATTRS; a++) {
        int i, want = 0;

        for (i = h; i < h + clen; ) {
            int t, l, hh = de_header(ids + i, h + clen - i, &t, &l);
            if (!hh || t != 1) return -1;
            if (l == 2 && be16(ids + i + hh) == k_record[a].id) want = 1;
            if (l == 4) {
                unsigned lo = be16(ids + i + hh), hi = be16(ids + i + hh + 2);
                if (k_record[a].id >= lo && k_record[a].id <= hi) want = 1;
            }
            i += hh + l;
        }
        if (!want) continue;

        if (n + 3 + k_record[a].len > max) return -1;
        out[n++] = 0x09;
        put_be16(out + n, k_record[a].id);
        n += 2;
        memcpy(out + n, k_record[a].value, (size_t)k_record[a].len);
        n += k_record[a].len;
    }

    out[0] = 0x36;
    put_be16(out + 1, (unsigned)(n - 3));
    return n;
}

static int error_rsp(unsigned tid, unsigned code, unsigned char *rsp)
{
    rsp[0] = PDU_ERROR;
    put_be16(rsp + 1, tid);
    put_be16(rsp + 3, 2);
    put_be16(rsp + 5, code);
    return 7;
}

int sdp_handle(const unsigned char *req, int len, unsigned char *rsp, int max)
{
    unsigned tid;
    int plen, used, used2, n, m;
    const unsigned char *p;

    if (len < 5 || max < 64) return 0;
    tid = be16(req + 1);
    plen = (int)be16(req + 3);
    if (5 + plen > len) return error_rsp(tid, ERR_INVALID_SYNTAX, rsp);
    p = req + 5;

    switch (req[0]) {
    case PDU_SEARCH_REQ:
        m = pattern_matches(p, plen, &used);
        if (m < 0) return error_rsp(tid, ERR_INVALID_SYNTAX, rsp);
        log_line("sdp: service search -> %d record(s)", m);
        rsp[0] = PDU_SEARCH_RSP;
        put_be16(rsp + 1, tid);
        put_be16(rsp + 5, (unsigned)m);
        put_be16(rsp + 7, (unsigned)m);
        n = 9;
        if (m) { put_be32(rsp + n, RECORD_HANDLE); n += 4; }
        rsp[n++] = 0x00;                            /* no continuation */
        put_be16(rsp + 3, (unsigned)(n - 5));
        return n;

    case PDU_ATTR_REQ:
        if (plen < 6) return error_rsp(tid, ERR_INVALID_SYNTAX, rsp);
        if (be32(p) != RECORD_HANDLE) return error_rsp(tid, ERR_INVALID_HANDLE, rsp);
        n = build_attrs(p + 6, plen - 6, &used, rsp + 7, max - 8);
        if (n < 0) return error_rsp(tid, ERR_INVALID_SYNTAX, rsp);
        log_line("sdp: attribute request -> %d bytes", n);
        rsp[0] = PDU_ATTR_RSP;
        put_be16(rsp + 1, tid);
        put_be16(rsp + 5, (unsigned)n);
        rsp[7 + n] = 0x00;
        put_be16(rsp + 3, (unsigned)(n + 3));
        return 8 + n;

    case PDU_SEARCH_ATTR_REQ:
        m = pattern_matches(p, plen, &used);
        if (m < 0 || used + 2 > plen) return error_rsp(tid, ERR_INVALID_SYNTAX, rsp);
        if (m) {
            /* Outer sequence of one record's attribute sequence. */
            n = build_attrs(p + used + 2, plen - used - 2, &used2, rsp + 10, max - 11);
            if (n < 0) return error_rsp(tid, ERR_INVALID_SYNTAX, rsp);
            rsp[7] = 0x36;
            put_be16(rsp + 8, (unsigned)n);
            n += 3;
        } else {
            rsp[7] = 0x35;
            rsp[8] = 0x00;
            n = 2;
        }
        log_line("sdp: search+attributes -> %d record(s), %d bytes", m, n);
        rsp[0] = PDU_SEARCH_ATTR_RSP;
        put_be16(rsp + 1, tid);
        put_be16(rsp + 5, (unsigned)n);
        rsp[7 + n] = 0x00;
        put_be16(rsp + 3, (unsigned)(n + 3));
        return 8 + n;

    default:
        log_line("sdp: unhandled PDU %#04x", req[0]);
        return error_rsp(tid, ERR_INVALID_SYNTAX, rsp);
    }
}
