/*
 * zmodem.c - ZMODEM batch file transfer for CGTerm.
 *
 * Implements both halves of ZMODEM (Chuck Forsberg, Omen Technology,
 * 1986-1988) on top of the byte I/O layer in xfer.c. Verified against
 * lrzsz (sz/rz), which is what nearly every BBS and terminal still uses.
 *
 * Protocol in one paragraph: every exchange is a 5-byte header (a type and
 * either a 32-bit little-endian file position or four flag bytes), sent in
 * one of three encodings - hex (printable, CRC-16), binary with CRC-16
 * (ZBIN) or binary with CRC-32 (ZBIN32). A receiver only ever sends hex
 * headers; a sender uses binary headers for ZFILE/ZDATA/ZEOF and hex for
 * the rest. ZFILE, ZSINIT and ZDATA headers are followed by data
 * subpackets, each terminated by ZDLE + ZCRCx and a CRC in the same width
 * as the header. Bytes that might upset a modem or telnet link are sent as
 * ZDLE + (byte ^ 0x40). The receiver drives recovery: whenever something is
 * wrong it says ZRPOS <offset> and the sender rewinds to that offset.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xfer.h"
#include "crc.h"
#include "zmodem.h"

/* Provided by xfer.c. The file functions open/close the temp download. */
extern FILE *xfer_sendfile;
extern int xfer_begin_file(void);
extern int xfer_end_file(const char *remote_name);
extern void xfer_abort_file(void);

int zmodem_recv_crc32 = 1;

/* ---- protocol constants ---------------------------------------------- */

#define ZPAD    0x2a   /* '*': padding before a header */
#define ZDLE    0x18   /* CAN: escape character, also the cancel character */
#define ZDLEE   (ZDLE ^ 0x40)
#define ZBIN    0x41   /* 'A': binary header, 16-bit CRC */
#define ZHEX    0x42   /* 'B': hex header, 16-bit CRC */
#define ZBIN32  0x43   /* 'C': binary header, 32-bit CRC */
#define XON     0x11
#define XOFF    0x13

#define ZRQINIT     0  /* sender: request receiver init */
#define ZRINIT      1  /* receiver: capabilities */
#define ZSINIT      2  /* sender: options + Attn string */
#define ZACK        3
#define ZFILE       4  /* file name/info subpacket follows */
#define ZSKIP       5  /* receiver: skip this file */
#define ZNAK        6
#define ZABORT      7
#define ZFIN        8
#define ZRPOS       9  /* receiver: resume at this position */
#define ZDATA      10  /* data subpackets follow */
#define ZEOF       11
#define ZFERR      12
#define ZCRC       13  /* request/answer file CRC */
#define ZCHALLENGE 14
#define ZCOMPL     15
#define ZCAN       16
#define ZFREECNT   17
#define ZCOMMAND   18
#define ZSTDERR    19

#define ZCRCE   0x68   /* 'h': end of frame, header follows */
#define ZCRCG   0x69   /* 'i': frame continues, no response */
#define ZCRCQ   0x6a   /* 'j': frame continues, ZACK expected */
#define ZCRCW   0x6b   /* 'k': end of frame, ZACK expected */
#define ZRUB0   0x6c   /* 'l': escaped 0x7f */
#define ZRUB1   0x6d   /* 'm': escaped 0xff */

/* header byte positions (byte 0 is the type) */
#define ZF0 4
#define ZF1 3
#define ZF2 2
#define ZF3 1
#define ZP0 1
#define ZP1 2
#define ZP2 3
#define ZP3 4

/* ZRINIT ZF0 flags */
#define CANFDX  0x01   /* full duplex */
#define CANOVIO 0x02   /* can overlap disk I/O (streaming) */
#define CANFC32 0x20   /* accepts 32-bit CRC frames */
#define ESCCTL  0x40   /* wants all control chars escaped */
/* ZSINIT ZF0 flags */
#define TESCCTL 0x40   /* sender wants control chars escaped towards it */
/* ZFILE ZF0 conversion option */
#define ZCBIN   1      /* binary transfer */

/* ---- tuning ----------------------------------------------------------- */

#define ZM_TIMEOUT_MS    10000   /* per header / per byte inside a frame */
#define ZM_RETRIES       10
#define ZM_BLKLEN        1024    /* data subpacket size we send */
#define ZM_MAX_SUBPACKET 8192    /* largest subpacket we accept */
#define ZM_MAX_GARBAGE   65536   /* bytes without a header before giving up (a
                                    pipe/TCP window of stale data after ZRPOS) */
#define ZM_NAME_MAX      256

/* internal result codes; -1/-2 match xfer_recv_byte so they pass through */
#define ZM_TIMEOUT     -1
#define ZM_DISCONN     -2
#define ZM_CANCEL      -3   /* remote sent 5+ CAN */
#define ZM_ERROR       -4   /* CRC error, malformed frame, too much garbage */
#define ZM_USERCANCEL  -5   /* xfer_cancel set */
#define ZM_GOTOR     0x100  /* zm_dlread: a frame end, low byte is ZCRCx */

/* ---- state ------------------------------------------------------------ */

static unsigned long zm_crc32tab[256];
static int zm_crc32_ready = 0;

static unsigned char zm_rxhdr[5];
static int zm_rxframe;            /* ZBIN / ZHEX / ZBIN32 of the last header */
static unsigned char zm_rxbuf[ZM_MAX_SUBPACKET];
static int zm_rxcount;

static int zm_txfcs32;            /* sender: use ZBIN32 + CRC-32 */
static int zm_ctlesc;             /* escape all control chars on send */
static int zm_lastsent;           /* for the "@<CR>" telenet escape rule */
static int zm_unget_c = -1;       /* one byte of pushback */
static int zm_rxbuflen;           /* receiver's buffer size, 0 = streaming */
static int zm_can_stream;         /* receiver has CANOVIO and CANFDX */
static unsigned char zm_attn[32]; /* receiver: sender's Attn string */

/* ---- CRC -------------------------------------------------------------- */

/* Standard reflected CRC-32 (IEEE 802.3), the same one zlib computes, built
 * at first use so we don't depend on zlib. */
static void zm_crc32_init(void) {
  unsigned long c;
  int n, k;

  if (zm_crc32_ready) {
    return;
  }
  for (n = 0; n < 256; n++) {
    c = (unsigned long)n;
    for (k = 0; k < 8; k++) {
      c = (c & 1) ? (0xedb88320UL ^ (c >> 1)) : (c >> 1);
    }
    zm_crc32tab[n] = c;
  }
  zm_crc32_ready = 1;
}

static unsigned long zm_crc32_upd(unsigned long crc, int c) {
  return zm_crc32tab[(crc ^ (unsigned long)c) & 0xff] ^ (crc >> 8);
}

/* CRC-16/XMODEM, one byte at a time (crc16_calc in crc.c does buffers). */
static unsigned short zm_crc16_upd(unsigned short crc, int c) {
  int i;

  crc ^= (unsigned short)(c << 8);
  for (i = 0; i < 8; i++) {
    crc = (crc & 0x8000) ? (unsigned short)((crc << 1) ^ 0x1021) : (unsigned short)(crc << 1);
  }
  return crc;
}

/* ---- byte I/O --------------------------------------------------------- */

static int zm_getc(int timeout_ms) {
  int c;

  if (xfer_cancel) {
    return ZM_USERCANCEL;
  }
  if (zm_unget_c >= 0) {
    c = zm_unget_c;
    zm_unget_c = -1;
    return c;
  }
  c = xfer_recv_byte(timeout_ms);
  if (c == -1 && xfer_cancel) {
    return ZM_USERCANCEL;
  }
  return c;
}

static void zm_unget(int c) {
  zm_unget_c = c;
}

static void zm_putc(int c) {
  zm_lastsent = c & 0xff;
  xfer_send_byte((unsigned char)(c & 0xff));
}

/* Send a data/header byte with ZDLE escaping. ZDLE itself, DLE, XON and
 * XOFF (with and without parity) are always escaped. CR is escaped after
 * '@' because "@<CR>" wakes up Telenet PADs. With ESCCTL every control
 * character goes escaped, and 0x7f/0xff get their own codes. */
static void zm_sendline(int c) {
  c &= 0xff;
  switch (c) {
  case ZDLE:
    zm_putc(ZDLE);
    zm_putc(ZDLEE);
    break;
  case 0x0d:
  case 0x8d:
    if (!zm_ctlesc && (zm_lastsent & 0x7f) != '@') {
      zm_putc(c);
    } else {
      zm_putc(ZDLE);
      zm_putc(c ^ 0x40);
    }
    break;
  case 0x10:
  case 0x11:
  case 0x13:
  case 0x90:
  case 0x91:
  case 0x93:
    zm_putc(ZDLE);
    zm_putc(c ^ 0x40);
    break;
  case 0x7f:
    if (zm_ctlesc) {
      zm_putc(ZDLE);
      zm_putc(ZRUB0);
    } else {
      zm_putc(c);
    }
    break;
  case 0xff:
    if (zm_ctlesc) {
      zm_putc(ZDLE);
      zm_putc(ZRUB1);
    } else {
      zm_putc(c);
    }
    break;
  default:
    if (zm_ctlesc && !(c & 0x60)) {
      zm_putc(ZDLE);
      zm_putc(c ^ 0x40);
    } else {
      zm_putc(c);
    }
    break;
  }
}

/* Read one ZDLE-decoded byte. Returns 0..255, ZM_GOTOR | ZCRCx for a frame
 * end, or a negative code. XON/XOFF are flow control noise and dropped;
 * other unescaped control characters are data (we never ask for ESCCTL). */
static int zm_dlread(void) {
  int c, cans;

  for (;;) {
    c = zm_getc(ZM_TIMEOUT_MS);
    if (c < 0) {
      return c;
    }
    if (c & 0x60) {
      return c;
    }
    if (c == ZDLE) {
      break;
    }
    if (c == XON || c == XOFF || c == (XON | 0x80) || c == (XOFF | 0x80)) {
      continue;
    }
    return c;
  }

  /* After ZDLE. Five CANs in a row (ZDLE + 4 more) is a remote cancel. */
  cans = 0;
  for (;;) {
    c = zm_getc(ZM_TIMEOUT_MS);
    if (c < 0) {
      return c;
    }
    if (c == ZDLE) {
      if (++cans >= 4) {
        return ZM_CANCEL;
      }
      continue;
    }
    if (c == XON || c == XOFF || c == (XON | 0x80) || c == (XOFF | 0x80)) {
      continue;
    }
    break;
  }

  switch (c) {
  case ZCRCE:
  case ZCRCG:
  case ZCRCQ:
  case ZCRCW:
    return c | ZM_GOTOR;
  case ZRUB0:
    return 0x7f;
  case ZRUB1:
    return 0xff;
  default:
    if ((c & 0x60) == 0x40) {
      return c ^ 0x40;
    }
    return ZM_ERROR;
  }
}

/* ---- headers ---------------------------------------------------------- */

static void zm_hdr_pos(unsigned char *h, int type, unsigned long pos) {
  h[0] = (unsigned char)type;
  h[ZP0] = (unsigned char)(pos & 0xff);
  h[ZP1] = (unsigned char)((pos >> 8) & 0xff);
  h[ZP2] = (unsigned char)((pos >> 16) & 0xff);
  h[ZP3] = (unsigned char)((pos >> 24) & 0xff);
}

static void zm_hdr_flags(unsigned char *h, int type, int f0, int f1, int f2, int f3) {
  h[0] = (unsigned char)type;
  h[ZF0] = (unsigned char)f0;
  h[ZF1] = (unsigned char)f1;
  h[ZF2] = (unsigned char)f2;
  h[ZF3] = (unsigned char)f3;
}

static unsigned long zm_rxpos(void) {
  return (unsigned long)zm_rxhdr[ZP0] | ((unsigned long)zm_rxhdr[ZP1] << 8) |
         ((unsigned long)zm_rxhdr[ZP2] << 16) | ((unsigned long)zm_rxhdr[ZP3] << 24);
}

static void zm_puthex(int c) {
  static const char digits[] = "0123456789abcdef";

  /* lrzsz only accepts lowercase hex digits */
  zm_putc(digits[(c >> 4) & 0x0f]);
  zm_putc(digits[c & 0x0f]);
}

/* Hex header: "**" ZDLE 'B' 14 hex digits CR LF [XON]. The LF carries the
 * parity bit and the XON uncorks a sender that stopped on a stray XOFF,
 * both as the reference implementation does it. */
static void zm_shhdr(const unsigned char *h) {
  unsigned short crc = 0;
  int n;

  zm_putc(ZPAD);
  zm_putc(ZPAD);
  zm_putc(ZDLE);
  zm_putc(ZHEX);
  for (n = 0; n < 5; n++) {
    zm_puthex(h[n]);
    crc = zm_crc16_upd(crc, h[n]);
  }
  zm_puthex(crc >> 8);
  zm_puthex(crc & 0xff);
  zm_putc(0x0d);
  zm_putc(0x8a);
  if (h[0] != ZFIN && h[0] != ZACK) {
    zm_putc(XON);
  }
}

static void zm_shhdr_pos(int type, unsigned long pos) {
  unsigned char h[5];

  zm_hdr_pos(h, type, pos);
  zm_shhdr(h);
}

/* Binary header, CRC width chosen by the receiver's CANFC32 flag. */
static void zm_sbhdr(const unsigned char *h) {
  int n;

  zm_putc(ZPAD);
  zm_putc(ZDLE);
  if (zm_txfcs32) {
    unsigned long crc = 0xffffffffUL;

    zm_putc(ZBIN32);
    for (n = 0; n < 5; n++) {
      zm_sendline(h[n]);
      crc = zm_crc32_upd(crc, h[n]);
    }
    crc = ~crc;
    for (n = 0; n < 4; n++) {
      zm_sendline((int)(crc & 0xff));
      crc >>= 8;
    }
  } else {
    unsigned short crc = 0;

    zm_putc(ZBIN);
    for (n = 0; n < 5; n++) {
      zm_sendline(h[n]);
      crc = zm_crc16_upd(crc, h[n]);
    }
    zm_sendline(crc >> 8);
    zm_sendline(crc & 0xff);
  }
}

static void zm_sbhdr_pos(int type, unsigned long pos) {
  unsigned char h[5];

  zm_hdr_pos(h, type, pos);
  zm_sbhdr(h);
}

static int zm_hexval(int c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static int zm_gethex(void) {
  int hi, lo, c;

  c = zm_getc(ZM_TIMEOUT_MS);
  if (c < 0) {
    return c;
  }
  hi = zm_hexval(c);
  c = zm_getc(ZM_TIMEOUT_MS);
  if (c < 0) {
    return c;
  }
  lo = zm_hexval(c);
  if (hi < 0 || lo < 0) {
    return ZM_ERROR;
  }
  return (hi << 4) | lo;
}

static int zm_rhhdr(void) {
  unsigned short crc = 0;
  int n, c;

  for (n = 0; n < 5; n++) {
    c = zm_gethex();
    if (c < 0) {
      return c;
    }
    zm_rxhdr[n] = (unsigned char)c;
    crc = zm_crc16_upd(crc, c);
  }
  for (n = 0; n < 2; n++) {
    c = zm_gethex();
    if (c < 0) {
      return c;
    }
    crc = zm_crc16_upd(crc, c);
  }
  if (crc != 0) {
    return ZM_ERROR;
  }
  /* Swallow the CR LF that follows; a trailing XON is skipped by the next
   * header scan. A short timeout keeps a non-conforming peer from stalling
   * us here, and anything that isn't CR/LF is pushed back rather than lost. */
  c = zm_getc(1000);
  if (c == ZM_DISCONN || c == ZM_USERCANCEL) {
    return c;
  }
  if (c >= 0 && (c & 0x7f) == 0x0d) {
    c = zm_getc(1000);
    if (c == ZM_DISCONN || c == ZM_USERCANCEL) {
      return c;
    }
    if (c >= 0 && (c & 0x7f) != 0x0a) {
      zm_unget(c);
    }
  } else if (c >= 0) {
    zm_unget(c);
  }
  return zm_rxhdr[0];
}

static int zm_rbhdr16(void) {
  unsigned short crc = 0;
  int n, c;

  for (n = 0; n < 7; n++) {
    c = zm_dlread();
    if (c < 0) {
      return c;
    }
    if (c & ZM_GOTOR) {
      return ZM_ERROR;
    }
    if (n < 5) {
      zm_rxhdr[n] = (unsigned char)c;
    }
    crc = zm_crc16_upd(crc, c);
  }
  if (crc != 0) {
    return ZM_ERROR;
  }
  return zm_rxhdr[0];
}

static int zm_rbhdr32(void) {
  unsigned long crc = 0xffffffffUL;
  int n, c;

  for (n = 0; n < 9; n++) {
    c = zm_dlread();
    if (c < 0) {
      return c;
    }
    if (c & ZM_GOTOR) {
      return ZM_ERROR;
    }
    if (n < 5) {
      zm_rxhdr[n] = (unsigned char)c;
    }
    crc = zm_crc32_upd(crc, c);
  }
  /* running a CRC-32 over data + its own inverted CRC yields this residue */
  if (crc != 0xdebb20e3UL) {
    return ZM_ERROR;
  }
  return zm_rxhdr[0];
}

/* Wait for a header. Returns its type (zm_rxhdr filled in, zm_rxframe set)
 * or a negative code. Anything that is not a header is garbage and skipped,
 * up to a limit, because a sender keeps streaming for a while after we
 * have abandoned a frame. Five CANs in a row is a remote cancel. */
static int zm_gethdr(int timeout_ms) {
  int c, garbage = 0, cans = 0;

  for (;;) {
    c = zm_getc(timeout_ms);
    if (c < 0) {
      return c;
    }
    if (c == ZDLE) {
      if (++cans >= 5) {
        return ZM_CANCEL;
      }
      if (++garbage > ZM_MAX_GARBAGE) {
        return ZM_ERROR;
      }
      continue;
    }
    cans = 0;
    if (c != ZPAD && c != (ZPAD | 0x80)) {
      if (++garbage > ZM_MAX_GARBAGE) {
        return ZM_ERROR;
      }
      continue;
    }

    /* got a ZPAD; hex headers have two. A peer streaming '*' must still
     * run into the garbage limit instead of pinning us here. */
    while ((c = zm_getc(timeout_ms)) == ZPAD) {
      if (++garbage > ZM_MAX_GARBAGE) {
        return ZM_ERROR;
      }
    }
    if (c < 0) {
      return c;
    }
    if (c != ZDLE) {
      if (++garbage > ZM_MAX_GARBAGE) {
        return ZM_ERROR;
      }
      continue;
    }

    c = zm_getc(timeout_ms);
    if (c < 0) {
      return c;
    }
    switch (c) {
    case ZBIN:
      zm_rxframe = ZBIN;
      return zm_rbhdr16();
    case ZBIN32:
      zm_rxframe = ZBIN32;
      return zm_rbhdr32();
    case ZHEX:
      zm_rxframe = ZHEX;
      return zm_rhhdr();
    case ZDLE:
      cans = 2;   /* "* CAN CAN" could be the start of a cancel burst */
      break;
    default:
      break;
    }
    if (++garbage > ZM_MAX_GARBAGE) {
      return ZM_ERROR;
    }
  }
}

/* ---- data subpackets -------------------------------------------------- */

/* Send one subpacket ending in `frameend`, CRC width as negotiated. The
 * frame end byte is covered by the CRC. ZCRCW packets are followed by XON
 * so the receiver can answer even if it had sent XOFF. */
static void zm_sdata(const unsigned char *buf, int len, int frameend) {
  int i;

  if (zm_txfcs32) {
    unsigned long crc = 0xffffffffUL;

    for (i = 0; i < len; i++) {
      zm_sendline(buf[i]);
      crc = zm_crc32_upd(crc, buf[i]);
    }
    zm_putc(ZDLE);
    zm_putc(frameend);
    crc = ~zm_crc32_upd(crc, frameend);
    for (i = 0; i < 4; i++) {
      zm_sendline((int)(crc & 0xff));
      crc >>= 8;
    }
  } else {
    unsigned short crc;

    for (i = 0; i < len; i++) {
      zm_sendline(buf[i]);
    }
    crc = crc16_calc((unsigned char *)buf, len);
    zm_putc(ZDLE);
    zm_putc(frameend);
    crc = zm_crc16_upd(crc, frameend);
    zm_sendline(crc >> 8);
    zm_sendline(crc & 0xff);
  }
  if (frameend == ZCRCW) {
    zm_putc(XON);
  }
}

/* Receive one subpacket into zm_rxbuf / zm_rxcount. Returns the frame end
 * byte (ZCRCE/G/Q/W) or a negative code. The CRC width follows the header
 * that introduced the frame (zm_rxframe). */
static int zm_rdata(void) {
  int c, n = 0, frameend, i;

  for (;;) {
    c = zm_dlread();
    if (c < 0) {
      return c;
    }
    if (c & ZM_GOTOR) {
      frameend = c & 0xff;
      break;
    }
    if (n >= ZM_MAX_SUBPACKET) {
      return ZM_ERROR;   /* no sane sender does this; don't overrun */
    }
    zm_rxbuf[n++] = (unsigned char)c;
  }

  if (zm_rxframe == ZBIN32) {
    unsigned long crc = 0xffffffffUL;

    for (i = 0; i < n; i++) {
      crc = zm_crc32_upd(crc, zm_rxbuf[i]);
    }
    crc = zm_crc32_upd(crc, frameend);
    for (i = 0; i < 4; i++) {
      c = zm_dlread();
      if (c < 0) {
        return c;
      }
      if (c & ZM_GOTOR) {
        return ZM_ERROR;
      }
      crc = zm_crc32_upd(crc, c);
    }
    if (crc != 0xdebb20e3UL) {
      return ZM_ERROR;
    }
  } else {
    unsigned short crc = crc16_calc(zm_rxbuf, n);

    crc = zm_crc16_upd(crc, frameend);
    for (i = 0; i < 2; i++) {
      c = zm_dlread();
      if (c < 0) {
        return c;
      }
      if (c & ZM_GOTOR) {
        return ZM_ERROR;
      }
      crc = zm_crc16_upd(crc, c);
    }
    if (crc != 0) {
      return ZM_ERROR;
    }
  }
  zm_rxcount = n;
  return frameend;
}

/* ---- common ----------------------------------------------------------- */

static void zm_reset(void) {
  zm_crc32_init();
  zm_unget_c = -1;
  zm_lastsent = 0;
  zm_ctlesc = 0;
  zm_txfcs32 = 0;
  zm_rxbuflen = 0;
  zm_can_stream = 1;
  zm_rxcount = 0;
  zm_attn[0] = 0;
}

/* Abort the session the way sz/rz do: a burst of CANs, then backspaces so
 * the CANs don't linger on a terminal that echoes them. */
static void zm_send_cancel(void) {
  int i;

  for (i = 0; i < 8; i++) {
    zm_putc(ZDLE);
  }
  for (i = 0; i < 8; i++) {
    zm_putc(0x08);
  }
}

/* Map a negative code from the I/O layer to a user message. Returns 1 if
 * the session is over (caller must give up). */
static int zm_fatal(int c, int cur, int total) {
  switch (c) {
  case ZM_DISCONN:
    xfer_progress_status("Disconnected!", cur, total);
    return 1;
  case ZM_CANCEL:
    xfer_progress_status("Cancelled by remote!", cur, total);
    return 1;
  case ZM_USERCANCEL:
    zm_send_cancel();
    xfer_progress_status("Cancelled", cur, total);
    return 1;
  default:
    return 0;
  }
}

/* ---- receiver --------------------------------------------------------- */

static void zm_send_zrinit(void) {
  unsigned char h[5];
  int flags = CANFDX | CANOVIO;

  if (zmodem_recv_crc32) {
    flags |= CANFC32;
  }
  /* ZP0/ZP1 = 0: no buffer limit, the sender may stream full tilt */
  zm_hdr_flags(h, ZRINIT, flags, 0, 0, 0);
  zm_shhdr(h);
}

/* Ask the sender to rewind. The Attn string (from ZSINIT) goes first; its
 * break (0xdd) and pause (0xde) codes mean nothing on a TCP link. */
static void zm_send_zrpos(unsigned long pos) {
  int i;

  for (i = 0; zm_attn[i] && i < (int)sizeof(zm_attn); i++) {
    if (zm_attn[i] != 0xdd && zm_attn[i] != 0xde) {
      zm_putc(zm_attn[i]);
    }
  }
  zm_shhdr_pos(ZRPOS, pos);
}

/* Parse the ZFILE subpacket: "name\0size mtime mode serial ...". Only the
 * name and the decimal size matter to us. */
static void zm_parse_zfile(char *name, size_t namesz, unsigned long *size) {
  int i, n = zm_rxcount;
  size_t len;

  *size = 0;
  for (i = 0; i < n && zm_rxbuf[i]; i++) {
    ;
  }
  len = (size_t)i;
  if (len >= namesz) {
    len = namesz - 1;
  }
  memcpy(name, zm_rxbuf, len);
  name[len] = 0;
  if (i < n) {
    char info[64];
    int k;

    for (k = 0; k < (int)sizeof(info) - 1 && i + 1 + k < n; k++) {
      info[k] = (char)zm_rxbuf[i + 1 + k];
    }
    info[k] = 0;
    *size = strtoul(info, NULL, 10);
  }
}

int zmodem_recv(void) {
  char name[ZM_NAME_MAX];
  unsigned long size = 0, rxbytes = 0;
  int files = 0, failed = 0;
  int finished = 0;
  int file_open = 0;
  int tries = 0;
  int ignored = 0;   /* well-formed but unexpected headers; bounded */
  int c;

  zm_reset();
  name[0] = 0;
  xfer_progress_status("ZMODEM: waiting for sender...", 0, 0);
  zm_send_zrinit();

  for (;;) {
    c = zm_gethdr(ZM_TIMEOUT_MS);

    if (c == ZM_TIMEOUT || c == ZM_ERROR) {
      if (++tries > ZM_RETRIES) {
        xfer_progress_status("Timeout!", (int)rxbytes, (int)size);
        zm_send_cancel();
        break;
      }
      if (file_open) {
        xfer_progress_status("No data, asking for resend...", (int)rxbytes, (int)size);
        zm_send_zrpos(rxbytes);
      } else {
        zm_send_zrinit();
      }
      continue;
    }
    if (c < 0) {
      zm_fatal(c, (int)rxbytes, (int)size);
      break;
    }

    switch (c) {
    case ZRQINIT:
      /* the sender hasn't seen our ZRINIT yet (or is just starting) */
      if (++ignored > ZM_RETRIES * 4) {
        xfer_progress_status("Sender never starts", (int)rxbytes, (int)size);
        goto out;
      }
      zm_send_zrinit();
      break;

    case ZSINIT:
      c = zm_rdata();
      if (c < 0) {
        if (zm_fatal(c, (int)rxbytes, (int)size)) {
          goto out;
        }
        if (++ignored > ZM_RETRIES) {
          goto out;
        }
        zm_shhdr_pos(ZNAK, 0);
        break;
      }
      memset(zm_attn, 0, sizeof(zm_attn));
      memcpy(zm_attn, zm_rxbuf, zm_rxcount < (int)sizeof(zm_attn) - 1 ? (size_t)zm_rxcount : sizeof(zm_attn) - 1);
      zm_ctlesc = (zm_rxhdr[ZF0] & TESCCTL) != 0;
      zm_shhdr_pos(ZACK, 0);
      break;

    case ZFILE:
      c = zm_rdata();
      if (c < 0) {
        if (zm_fatal(c, (int)rxbytes, (int)size)) {
          goto out;
        }
        zm_shhdr_pos(ZNAK, 0);
        if (++tries > ZM_RETRIES) {
          goto fail;
        }
        break;
      }
      if (file_open) {
        /* our ZRPOS to the previous ZFILE was lost; start over */
        xfer_abort_file();
        file_open = 0;
      }
      zm_parse_zfile(name, sizeof(name), &size);
      rxbytes = 0;
      if (name[0] == 0 || !xfer_begin_file()) {
        xfer_progress_status("Cannot open file, skipping", 0, 0);
        failed++;
        zm_shhdr_pos(ZSKIP, 0);
        break;
      }
      file_open = 1;
      tries = 0;
      xfer_progress_status(name, 0, (int)size);
      zm_shhdr_pos(ZRPOS, 0);
      break;

    case ZDATA:
      if (!file_open) {
        zm_send_zrinit();
        break;
      }
      if (zm_rxpos() != rxbytes) {
        /* stale frame from before a resync, or we lost one: rewind it */
        if (++tries > ZM_RETRIES) {
          goto fail;
        }
        zm_send_zrpos(rxbytes);
        break;
      }
      for (;;) {
        c = zm_rdata();
        if (c == ZM_ERROR || c == ZM_TIMEOUT) {
          if (++tries > ZM_RETRIES) {
            goto fail;
          }
          xfer_progress_status("CRC error, asking for resend...", (int)rxbytes, (int)size);
          zm_send_zrpos(rxbytes);
          break;
        }
        if (c < 0) {
          zm_fatal(c, (int)rxbytes, (int)size);
          goto out;
        }
        if (zm_rxcount && xfer_save_data(zm_rxbuf, zm_rxcount) != zm_rxcount) {
          xfer_progress_status("Write error!", (int)rxbytes, (int)size);
          zm_send_cancel();
          goto out;
        }
        rxbytes += (unsigned long)zm_rxcount;
        tries = 0;
        xfer_progress_status(name, (int)rxbytes, (int)size);
        if (c == ZCRCW) {
          zm_shhdr_pos(ZACK, rxbytes);
          break;
        } else if (c == ZCRCQ) {
          zm_shhdr_pos(ZACK, rxbytes);
        } else if (c == ZCRCE) {
          break;
        }
        /* ZCRCG: more subpackets follow without a header */
      }
      break;

    case ZEOF:
      if (!file_open) {
        zm_send_zrinit();
        break;
      }
      if (zm_rxpos() < rxbytes) {
        break;   /* stale ZEOF from before a resync */
      }
      if (zm_rxpos() > rxbytes) {
        if (++tries > ZM_RETRIES) {
          goto fail;
        }
        zm_send_zrpos(rxbytes);
        break;
      }
      file_open = 0;
      if (xfer_end_file(name)) {
        files++;
      } else {
        failed++;
      }
      tries = 0;
      zm_send_zrinit();
      break;

    case ZFIN:
      if (file_open) {
        xfer_abort_file();
        file_open = 0;
      }
      zm_shhdr_pos(ZFIN, 0);
      /* the sender signs off with "OO"; don't leave it on the terminal */
      c = zm_getc(1000);
      if (c == 'O') {
        zm_getc(1000);
      }
      finished = 1;
      goto out;

    case ZFREECNT:
      /* free disk space: we don't know, claim plenty */
      if (++ignored > ZM_RETRIES * 4) {
        goto out;
      }
      zm_shhdr_pos(ZACK, 0x7fffffffUL);
      break;

    case ZCOMMAND:
      /* never run remote commands; read the subpacket and report failure */
      c = zm_rdata();
      if (c < 0 && zm_fatal(c, (int)rxbytes, (int)size)) {
        goto out;
      }
      if (++ignored > ZM_RETRIES * 4) {
        goto out;
      }
      zm_shhdr_pos(ZCOMPL, 1);
      break;

    case ZABORT:
    case ZCAN:
      xfer_progress_status("Cancelled by remote!", (int)rxbytes, (int)size);
      goto out;

    default:
      /* ZNAK, ZACK, ZCHALLENGE answers, unknown: restate our state */
      if (++tries > ZM_RETRIES) {
        goto fail;
      }
      if (file_open) {
        zm_send_zrpos(rxbytes);
      } else {
        zm_send_zrinit();
      }
      break;
    }
  }
  goto out;

fail:
  xfer_progress_status("Too many errors!", (int)rxbytes, (int)size);
  zm_send_cancel();

out:
  if (file_open) {
    xfer_abort_file();
  }
  if (finished && files > 0 && failed == 0) {
    char msg[64];

    snprintf(msg, sizeof(msg), "Received %d file%s", files, files == 1 ? "" : "s");
    xfer_progress_status(msg, (int)rxbytes, (int)rxbytes);
  } else if (files > 0) {
    char msg[96];

    /* files already closed by xfer_end_file stay saved; say so because the
     * return value below reports the batch as a whole */
    snprintf(msg, sizeof(msg), "%d file%s saved, batch %s", files, files == 1 ? "" : "s",
             finished ? "had failures" : "did not finish");
    xfer_progress_status(msg, (int)rxbytes, (int)rxbytes);
  }
  return (finished && failed == 0) ? files : 0;
}

/* ---- sender ----------------------------------------------------------- */

int zmodem_send_begin(void) {
  int tries = 0;
  int sent = 1;
  int c;

  zm_reset();
  xfer_progress_status("ZMODEM: waiting for receiver...", 0, 0);
  zm_shhdr_pos(ZRQINIT, 0);

  for (;;) {
    c = zm_gethdr(ZM_TIMEOUT_MS);
    if (c == ZM_TIMEOUT || c == ZM_ERROR) {
      if (++tries > ZM_RETRIES) {
        xfer_progress_status("Receiver did not start", 0, 0);
        zm_send_cancel();
        return 0;
      }
      /* the original Omen rz gives up after seeing five ZRQINITs, so
       * keep waiting silently once we've sent four */
      if (sent < 4) {
        sent++;
        zm_shhdr_pos(ZRQINIT, 0);
      }
      continue;
    }
    if (c < 0) {
      zm_fatal(c, 0, 0);
      return 0;
    }
    switch (c) {
    case ZRINIT:
      /* remember what the receiver can take; see zm_send_data */
      zm_txfcs32 = zmodem_recv_crc32 && (zm_rxhdr[ZF0] & CANFC32);
      zm_ctlesc = (zm_rxhdr[ZF0] & ESCCTL) != 0;
      zm_can_stream = (zm_rxhdr[ZF0] & CANOVIO) && (zm_rxhdr[ZF0] & CANFDX);
      zm_rxbuflen = zm_rxhdr[ZP0] | (zm_rxhdr[ZP1] << 8);
      return 1;
    case ZCHALLENGE:
      /* prove we are a live sender by echoing the number */
      zm_shhdr_pos(ZACK, zm_rxpos());
      break;
    case ZFIN:
    case ZABORT:
    case ZCAN:
      xfer_progress_status("Receiver gave up", 0, 0);
      return 0;
    default:
      /* ZNAK, stray ZRQINIT echo, etc: say it again (same cap) */
      if (++tries > ZM_RETRIES) {
        xfer_progress_status("Receiver did not start", 0, 0);
        zm_send_cancel();
        return 0;
      }
      if (sent < 4) {
        sent++;
        zm_shhdr_pos(ZRQINIT, 0);
      }
      break;
    }
  }
}

/* Rewind the upload to `pos`; 0 = ok. */
static int zm_seek(unsigned long pos) {
  if (pos > (unsigned long)xfer_file_size) {
    return -1;
  }
  return fseek(xfer_sendfile, (long)pos, SEEK_SET);
}

/* Answer a ZCRC request with the CRC-32 of the whole file, as sz does. */
static int zm_send_file_crc(void) {
  unsigned long crc = 0xffffffffUL;
  long remaining = xfer_file_size;
  int n, i;

  if (zm_seek(0)) {
    return 0;
  }
  while (remaining > 0) {
    n = remaining > XFER_BUFFER_SIZE ? XFER_BUFFER_SIZE : (int)remaining;
    if (xfer_load_data(xfer_buffer, n) != n) {
      return 0;
    }
    for (i = 0; i < n; i++) {
      crc = zm_crc32_upd(crc, xfer_buffer[i]);
    }
    remaining -= n;
  }
  zm_shhdr_pos(ZCRC, ~crc);
  return 1;
}

/* Stream the file from `pos` to the end, then ZEOF. Returns 1 when the
 * receiver acknowledges the file with ZRINIT (or ZSKIP), 0 on failure.
 *
 * Framing policy: the first subpacket of every ZDATA frame is ZCRCW so we
 * know the receiver is in sync before streaming; after that we send ZCRCG
 * subpackets back to back, polling the reverse channel between them for a
 * ZRPOS. Receivers that cannot stream (no CANOVIO/CANFDX, or a declared
 * buffer size) get a ZCRCW on every subpacket. */
static int zm_send_data(unsigned long pos, const char *name) {
  unsigned long size = (unsigned long)xfer_file_size;
  unsigned long framestart, lastrpos = 0;
  int errors = 0;
  int ignored = 0;   /* well-formed but unexpected headers; bounded */
  int c, n, want, frameend, blklen;

  blklen = ZM_BLKLEN;
  if (zm_rxbuflen > 0 && zm_rxbuflen < blklen) {
    blklen = zm_rxbuflen;
  }

newframe:
  if (zm_seek(pos)) {
    xfer_progress_status("Seek error!", (int)pos, (int)size);
    zm_send_cancel();
    return 0;
  }
  zm_sbhdr_pos(ZDATA, pos);
  framestart = pos;

  for (;;) {
    want = (size - pos) > (unsigned long)blklen ? blklen : (int)(size - pos);
    n = want > 0 ? xfer_load_data(xfer_buffer, want) : 0;
    if (n != want) {
      xfer_progress_status("Read error!", (int)pos, (int)size);
      zm_send_cancel();
      return 0;
    }
    if (pos + (unsigned long)n >= size) {
      frameend = ZCRCE;
    } else if (pos == framestart || !zm_can_stream || zm_rxbuflen > 0) {
      frameend = ZCRCW;
    } else {
      frameend = ZCRCG;
    }
    zm_sdata(xfer_buffer, n, frameend);
    pos += (unsigned long)n;
    xfer_progress_status(name, (int)pos, (int)size);

    if (frameend == ZCRCE) {
      break;
    }

    if (frameend == ZCRCW) {
      /* wait for ZACK of exactly this position */
      for (;;) {
        c = zm_gethdr(ZM_TIMEOUT_MS);
        if (c == ZACK) {
          if (zm_rxpos() == pos) {
            break;
          }
          if (++ignored > ZM_RETRIES * 4) goto toomany;
          continue;   /* stale ACK from an earlier frame */
        }
        if (c == ZRPOS) {
          goto resync;
        }
        if (c == ZSKIP) {
          goto skipped;
        }
        if (c == ZM_TIMEOUT || c == ZM_ERROR || c == ZNAK) {
          if (++errors > ZM_RETRIES) {
            goto toomany;
          }
          xfer_progress_status("No reply, resending...", (int)pos, (int)size);
          pos -= (unsigned long)n;
          goto newframe;
        }
        if (c < 0) {
          zm_fatal(c, (int)pos, (int)size);
          return 0;
        }
        if (c == ZFIN || c == ZABORT || c == ZCAN) {
          xfer_progress_status("Receiver gave up", (int)pos, (int)size);
          return 0;
        }
        /* ZRINIT (stale, from the handshake) and the like: keep waiting,
         * but not forever */
        if (++ignored > ZM_RETRIES * 4) goto toomany;
      }
      goto newframe;   /* a ZCRCW ends the frame; the next needs a header */
    }

    /* ZCRCG: peek at the reverse channel without blocking. Only a header
     * (or a cancel burst) is interesting; XON/XOFF and junk are dropped. */
    c = zm_getc(0);
    if (c == ZM_DISCONN || c == ZM_USERCANCEL) {
      zm_fatal(c, (int)pos, (int)size);
      return 0;
    }
    if (c == ZPAD || c == ZDLE) {
      zm_unget(c);
      c = zm_gethdr(ZM_TIMEOUT_MS);
      if (c == ZRPOS) {
        goto resync;
      }
      if (c == ZSKIP) {
        goto skipped;
      }
      if (c == ZM_DISCONN || c == ZM_CANCEL || c == ZM_USERCANCEL) {
        zm_fatal(c, (int)pos, (int)size);
        return 0;
      }
      if (c == ZFIN || c == ZABORT || c == ZCAN) {
        xfer_progress_status("Receiver gave up", (int)pos, (int)size);
        return 0;
      }
      /* ZACK, ZNAK, timeout, garbage: carry on streaming */
    }
  }

  /* end of file */
  errors = 0;
  for (;;) {
    zm_sbhdr_pos(ZEOF, pos);
    for (;;) {
      c = zm_gethdr(ZM_TIMEOUT_MS);
      if (c == ZRINIT) {
        xfer_progress_status(name, (int)size, (int)size);
        return 1;
      }
      if (c == ZSKIP) {
        goto skipped;
      }
      if (c == ZRPOS) {
        goto resync;
      }
      if (c == ZACK) {
        if (++ignored > ZM_RETRIES * 4) {
          xfer_progress_status("Too many errors!", xfer_file_size, xfer_file_size);
          zm_send_cancel();
          return 0;
        }
        continue;   /* late ACK; the ZRINIT is still coming */
      }
      if (c == ZM_TIMEOUT || c == ZM_ERROR || c == ZNAK) {
        if (++errors > ZM_RETRIES) {
          goto toomany;
        }
        break;   /* resend ZEOF */
      }
      if (c < 0) {
        zm_fatal(c, (int)pos, (int)size);
        return 0;
      }
      if (c == ZFIN || c == ZABORT || c == ZCAN) {
        xfer_progress_status("Receiver gave up", (int)pos, (int)size);
        return 0;
      }
    }
  }

resync:
  /* The receiver wants data from zm_rxpos(). A request that doesn't move
   * forward means the link keeps corrupting the same spot; bound it. */
  if (zm_rxpos() > size) {
    xfer_progress_status("Bad position from receiver!", (int)pos, (int)size);
    zm_send_cancel();
    return 0;
  }
  if (zm_rxpos() <= lastrpos) {
    if (++errors > ZM_RETRIES) {
      goto toomany;
    }
  } else {
    errors = 0;
  }
  lastrpos = zm_rxpos();
  pos = zm_rxpos();
  xfer_progress_status("Resending...", (int)pos, (int)size);
  goto newframe;

skipped:
  xfer_progress_status("Skipped by receiver", (int)size, (int)size);
  return 1;

toomany:
  xfer_progress_status("Too many errors!", (int)pos, (int)size);
  zm_send_cancel();
  return 0;
}

int zmodem_send_file(const char *displayname) {
  unsigned char h[5];
  char info[ZM_NAME_MAX + 64];
  const char *name;
  const char *p;
  int tries = 0;
  int ignored = 0;   /* well-formed but unexpected headers; bounded */
  int crc_requests = 0;
  int len, c;

  /* ZMODEM carries a bare file name; never leak a local path */
  name = displayname;
  for (p = displayname; *p; p++) {
    if (*p == '/' || *p == '\\') {
      name = p + 1;
    }
  }
  if (name[0] == 0) {
    name = "upload";
  }

  /* "name\0size mtime mode serial files-left bytes-left" */
  len = snprintf(info, sizeof(info), "%.*s", ZM_NAME_MAX - 1, name) + 1;
  len += snprintf(info + len, sizeof(info) - (size_t)len, "%ld 0 0 0 1 %ld",
                  (long)xfer_file_size, (long)xfer_file_size) + 1;

  zm_hdr_flags(h, ZFILE, ZCBIN, 0, 0, 0);
  xfer_progress_status(name, 0, xfer_file_size);

  for (;;) {
    zm_sbhdr(h);
    zm_sdata((unsigned char *)info, len, ZCRCW);

    for (;;) {
      c = zm_gethdr(ZM_TIMEOUT_MS);
      if (c == ZRPOS) {
        return zm_send_data(zm_rxpos(), name);
      }
      if (c == ZSKIP) {
        xfer_progress_status("Skipped by receiver", xfer_file_size, xfer_file_size);
        return 1;
      }
      if (c == ZCRC) {
        /* answer once per ZFILE: each answer re-reads the whole file, so a
         * receiver repeating ZCRC must not be able to pin us here */
        if (++crc_requests > 2 || !zm_send_file_crc()) {
          xfer_progress_status(crc_requests > 2 ? "Too many errors!" : "Read error!", 0, xfer_file_size);
          zm_send_cancel();
          return 0;
        }
        continue;
      }
      if (c == ZRINIT) {
        if (++ignored > ZM_RETRIES * 4) {
          xfer_progress_status("Too many errors!", 0, xfer_file_size);
          zm_send_cancel();
          return 0;
        }
        /* Not an answer to the ZFILE: receivers send ZRINIT once at
         * startup and again for our ZRQINIT, and both may still be in
         * the pipe. Resending ZFILE now would make rz answer every frame
         * one step late (it ZRPOSes a duplicate ZFILE), so wait for the
         * real ZRPOS instead, as sz does. A lost ZFILE shows up as a
         * timeout below. */
        continue;
      }
      if (c == ZNAK || c == ZM_TIMEOUT || c == ZM_ERROR) {
        /* the receiver didn't get (or didn't like) the ZFILE: again */
        if (++tries > ZM_RETRIES) {
          xfer_progress_status("Too many errors!", 0, xfer_file_size);
          zm_send_cancel();
          return 0;
        }
        break;
      }
      if (c < 0) {
        zm_fatal(c, 0, xfer_file_size);
        return 0;
      }
      if (c == ZFIN || c == ZABORT || c == ZCAN) {
        xfer_progress_status("Receiver gave up", 0, xfer_file_size);
        return 0;
      }
      /* stale ZACK etc: keep waiting for the real answer, within limits */
      if (++ignored > ZM_RETRIES * 4) {
        xfer_progress_status("Too many errors!", 0, xfer_file_size);
        zm_send_cancel();
        return 0;
      }
    }
  }
}

int zmodem_send_end(void) {
  int tries = 0;
  int ignored = 0;   /* well-formed but unexpected headers; bounded */
  int c;

  for (;;) {
    zm_shhdr_pos(ZFIN, 0);
    for (;;) {
      c = zm_gethdr(ZM_TIMEOUT_MS);
      if (c == ZFIN) {
        /* "Over and Out": tells the receiver it may exit */
        zm_putc('O');
        zm_putc('O');
        xfer_progress_status("Transfer complete", xfer_file_size, xfer_file_size);
        return 1;
      }
      if (c == ZM_DISCONN || c == ZM_CANCEL || c == ZM_USERCANCEL) {
        zm_fatal(c, xfer_file_size, xfer_file_size);
        return 0;
      }
      if (c == ZM_TIMEOUT || c == ZM_ERROR || c == ZNAK || c == ZRINIT) {
        if (++tries > ZM_RETRIES) {
          xfer_progress_status("Receiver did not finish", xfer_file_size, xfer_file_size);
          return 0;
        }
        break;   /* resend ZFIN */
      }
      /* late ZACK and the like: wait for the ZFIN, within limits */
      if (++ignored > ZM_RETRIES * 4) {
        xfer_progress_status("Receiver did not finish", xfer_file_size, xfer_file_size);
        return 0;
      }
    }
  }
}
