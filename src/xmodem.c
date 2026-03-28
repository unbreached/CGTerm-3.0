#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "gfx.h"
#include "menu.h"
#include "xfer.h"
#include "crc.h"

#define XM_SOH 0x01
#define XM_STX 0x02
#define XM_EOT 0x04
#define XM_ACK 0x06
#define XM_NAK 0x15
#define XM_CAN 0x18
#define XM_PAD 0x1a
#define XM_C   0x43

#ifndef XMODEM_DEBUG
#define XMODEM_DEBUG 0
#endif

#if XMODEM_DEBUG
#define XMDBG(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#else
#define XMDBG(...) do { } while (0)
#endif

#if XMODEM_DEBUG
static const char *xm_name(int c) {
  switch (c) {
  case XM_SOH: return "SOH";
  case XM_STX: return "STX";
  case XM_EOT: return "EOT";
  case XM_ACK: return "ACK";
  case XM_NAK: return "NAK";
  case XM_CAN: return "CAN";
  case XM_C:   return "C";
  case -1:     return "TIMEOUT";
  case -2:     return "DISCONNECT";
  default:     return "?";
  }
}
#endif

static void xm_dbg_byte(const char *dir, int c) {
#if XMODEM_DEBUG
  if (c >= 0) {
    XMDBG("[XMODEM] %s %s (%02X)\n", dir, xm_name(c), c & 0xff);
  } else {
    XMDBG("[XMODEM] %s %s (%d)\n", dir, xm_name(c), c);
  }
#else
  (void)dir;
  (void)c;
#endif
}

static void xm_send_byte_dbg(int c) {
  xm_dbg_byte("tx", c);
  xfer_send_byte(c);
}

static int xm_recv_byte_dbg(int timeout_ms) {
  int c = xfer_recv_byte(timeout_ms);
  xm_dbg_byte("rx", c);
  return c;
}

static int xm_recv_byte_error_dbg(int timeout_ms, int retries) {
  int c = xfer_recv_byte_error(timeout_ms, retries);
  xm_dbg_byte("rx", c);
  return c;
}

void xmodem_fail(char *message) {
  xfer_progress(message);
  XMDBG("[XMODEM] FAIL: %s\n", message);
  while (xfer_recv_byte(1000) >= 0);
  xm_send_byte_dbg(XM_CAN);
  xm_send_byte_dbg(XM_CAN);
}

void xmodem_retry(char *message) {
  xfer_progress(message);
  XMDBG("[XMODEM] RETRY: %s\n", message);
  while (xfer_recv_byte(1000) >= 0);
  xm_send_byte_dbg(XM_NAK);
}

int xmodem_checksum(int blocksize, int usecrc) {
  int i;
  unsigned short remotecrc;
  unsigned char checksum = 0;
  unsigned short crc = 0;

  if (blocksize == 128 && usecrc == 0) {
    for (i = 0; i < 128; ++i) {
      checksum += xfer_buffer[i];
    }
    XMDBG("[XMODEM] checksum local=%02X remote=%02X\n", checksum, xfer_buffer[i]);
    return checksum == xfer_buffer[i];
  } else if (blocksize == 128 && usecrc) {
    crc = crc16_calc(xfer_buffer, 128);
    remotecrc = (xfer_buffer[128]<<8) | xfer_buffer[129];
    XMDBG("[XMODEM] crc128 local=%04X remote=%04X\n", crc, remotecrc);
    return crc == remotecrc;
  } else if (blocksize == 1024 && usecrc) {
    crc = crc16_calc(xfer_buffer, 1024);
    remotecrc = (xfer_buffer[1024]<<8) | xfer_buffer[1025];
    XMDBG("[XMODEM] crc1k  local=%04X remote=%04X\n", crc, remotecrc);
    return crc == remotecrc;
  }
  return 0;
}

static unsigned char xmodem_buffer[1024];
static unsigned int xmodem_buffer_len = 0;
static int xmodem_last_payload_len = 0;
static int xmodem_recv_bytes = 0;

int xmodem_save_data(unsigned char *data, int length) {
  if (data) {
    XMDBG("[XMODEM] save_data buffer incoming length=%d pending=%u\n", length, xmodem_buffer_len);
    if (xmodem_buffer_len) {
      /* Check if the NEW block is all padding — if so, don't write
       * the previous block yet. It might be the true last data block
       * that needs trimming. */
      int all_pad = 1;
      int i;
      for (i = 0; i < length; i++) {
        if (data[i] != XM_PAD) { all_pad = 0; break; }
      }
      if (!all_pad) {
        /* New block has real data — safe to write the buffered one */
        if (xfer_save_data(xmodem_buffer, xmodem_buffer_len) != (int)xmodem_buffer_len) {
          XMDBG("[XMODEM] save_data write failed on buffered block len=%u\n", xmodem_buffer_len);
          return 0;
        }
      }
      /* If all_pad, we skip writing the previous block for now —
       * it will be written (with trim) at final flush */
    }
    memcpy(xmodem_buffer, data, length);
    xmodem_buffer_len = (unsigned int)length;
  } else {
    XMDBG("[XMODEM] save_data flush final pending=%u\n", xmodem_buffer_len);
    if (xmodem_buffer_len) {
      while (xmodem_buffer_len && xmodem_buffer[xmodem_buffer_len - 1] == XM_PAD) {
        --xmodem_buffer_len;
      }
      XMDBG("[XMODEM] final block after trim=%u\n", xmodem_buffer_len);
      if (xmodem_buffer_len) {
        if (xfer_save_data(xmodem_buffer, xmodem_buffer_len) != (int)xmodem_buffer_len) {
          XMDBG("[XMODEM] save_data write failed on final flush len=%u\n", xmodem_buffer_len);
          return 0;
        }
      }
    }
  }
  return 1;
}

int xmodem_recv(int usecrc) {
  int c;
  int blocknum, bufctr, errorcnt, blocksize;
  int nexthandshake;

  xmodem_buffer_len = 0;
  xmodem_recv_bytes = 0;
  blocknum = 1;
  nexthandshake = usecrc ? XM_C : XM_NAK;

  XMDBG("[XMODEM] recv start mode=%s initial_handshake=%s\n",
        usecrc ? "CRC" : "CHECKSUM", xm_name(nexthandshake));
  xm_send_byte_dbg(nexthandshake);

  xfer_progress_status("Starting...", 0, 0);

  for (;;) {
    if (xfer_cancel) {
      xmodem_fail("Cancelling...");
      return 0;
    }

    errorcnt = 0;
    while ((c = xm_recv_byte_dbg(10000)) == -1 && errorcnt < 10) {
      XMDBG("[XMODEM] recv handshake timeout retry=%d resend=%s\n", errorcnt + 1, xm_name(nexthandshake));
      xm_send_byte_dbg(nexthandshake);
      ++errorcnt;
    }
    nexthandshake = XM_NAK;

    switch (c) {
    case -2:
      xmodem_fail("Disconnected!");
      return 0;
    case -1:
      xmodem_fail("Timeout!");
      return 0;
    case XM_CAN:
      xmodem_fail("Remote cancel!");
      return 0;
    case XM_STX:
      blocksize = 1026;
      XMDBG("[XMODEM] recv block header STX expected_block=%d\n", blocknum & 0xff);
      goto getblock;
    case XM_SOH:
      blocksize = usecrc ? 130 : 129;
      XMDBG("[XMODEM] recv block header SOH expected_block=%d\n", blocknum & 0xff);
getblock:
      c = xm_recv_byte_error_dbg(1000, 10);
      if (c != (blocknum & 255) && c != ((blocknum - 1) & 255)) {
        XMDBG("[XMODEM] wrong block got=%d expected=%d prev=%d\n", c, blocknum & 255, (blocknum - 1) & 255);
        xmodem_retry("Wrong block, retrying");
        continue;
      }
      {
        int blockid = c;
        int is_duplicate = (blockid == ((blocknum - 1) & 255));
        int payload_len;
        int blockcomp = xm_recv_byte_error_dbg(1000, 10);
        if ((255 - blockcomp) != blockid) {
          XMDBG("[XMODEM] block mismatch blk=%d comp=%d\n", blockid, blockcomp);
          xmodem_retry("Block mismatch, retrying");
          continue;
        }
        if (is_duplicate) {
          XMDBG("[XMODEM] duplicate block %d\n", blockid);
        }
        XMDBG("[XMODEM] receiving payload block=%d payload_plus_check=%d%s\n",
              blockid, blocksize, is_duplicate ? " (duplicate)" : "");
        for (bufctr = 0; bufctr < blocksize; ++bufctr) {
          c = xm_recv_byte_error_dbg(1000, 5);
          if (c < 0) {
            break;
          }
          xfer_buffer[bufctr] = c;
        }
        if (c < 0) {
          XMDBG("[XMODEM] lost sync bufctr=%d c=%d\n", bufctr, c);
          xmodem_retry("Lost sync, retrying...");
          continue;
        }

        payload_len = blocksize & 0xfff0;
        if (!xmodem_checksum(payload_len, usecrc)) {
          xmodem_retry("Checksum failed, retrying...");
          continue;
        }

        if (is_duplicate) {
          xm_send_byte_dbg(XM_ACK);
          XMDBG("[XMODEM] ACK duplicate block=%d expected still=%d\n", blockid, blocknum & 0xff);
          continue;
        }

        if (blocknum == 0) {
          XMDBG("[XMODEM] block 0 received, not saving\n");
        } else {
          if (!xmodem_save_data(xfer_buffer, payload_len)) {
            xmodem_fail("Write error!");
            return 0;
          }
          xmodem_recv_bytes += payload_len;
          xfer_progress_status("Downloading...", xmodem_recv_bytes, 0);
        }

        xm_send_byte_dbg(XM_ACK);
        nexthandshake = XM_ACK;
        ++blocknum;
        XMDBG("[XMODEM] ACK sent next_expected=%d\n", blocknum & 0xff);
      }
      break;
    case XM_EOT:
      XMDBG("[XMODEM] recv EOT\n");
      if (!xmodem_save_data(NULL, 0)) {
        xmodem_fail("Write error!");
        return 0;
      }
      xm_send_byte_dbg(XM_ACK);
      xfer_progress_status("Transfer complete", xfer_saved_bytes, xfer_saved_bytes);
      return 1;
    default:
      XMDBG("[XMODEM] unexpected start byte=%d (%02X)\n", c, c & 0xff);
      xmodem_fail("Wtf!?");
      return 0;
    }
  }
}

int xmodem_load_block(int blocksize) {
  int l = xfer_load_data(xfer_buffer, blocksize);
  XMDBG("[XMODEM] load_block requested=%d got=%d\n", blocksize, l);
  if (l == 0) {
    xmodem_last_payload_len = 0;
    return 0;
  }
  xmodem_last_payload_len = l;
  if (l < blocksize) {
    memset(xfer_buffer + l, XM_PAD, blocksize - l);
  }
  return blocksize;
}

void xmodem_send_block(unsigned char blocknum, int blocksize, int usecrc) {
  int i;
  unsigned char cksum;
  unsigned short crc;

  XMDBG("[XMODEM] send block=%u size=%d mode=%s\n", blocknum, blocksize, usecrc ? "CRC" : "CHECKSUM");
  xm_send_byte_dbg(blocksize == 128 ? XM_SOH : XM_STX);
  xm_send_byte_dbg(blocknum & 0xff);
  xm_send_byte_dbg(0xff ^ (blocknum & 0xff));

  for (i = 0; i < blocksize; ++i) {
    xfer_send_byte(xfer_buffer[i]);
  }

  if (usecrc) {
    crc = crc16_calc(xfer_buffer, blocksize);
    XMDBG("[XMODEM] send crc=%04X\n", crc);
    xm_send_byte_dbg(crc >> 8);
    xm_send_byte_dbg(crc & 0xff);
  } else {
    cksum = 0;
    for (i = 0; i < 128; ++i) {
      cksum += xfer_buffer[i];
    }
    XMDBG("[XMODEM] send checksum=%02X\n", cksum);
    xm_send_byte_dbg(cksum);
  }
}

int xmodem_send(int send1k) {
  int c, usecrc, blocknum, bytesleft, sentbytes, blocksize;

  xfer_progress_status("Starting...", 0, xfer_file_size);

  XMDBG("[XMODEM] send start allow1k=%d waiting for receiver handshake\n", send1k);
  c = 0;
  while (c != XM_C && c != XM_NAK) {
    c = xm_recv_byte_dbg(1000);
    if (xfer_cancel) {
      xfer_progress_status("Canceled", 0, xfer_file_size);
      return 0;
    }
  }

  usecrc = (c == XM_C) ? 1 : 0;
  XMDBG("[XMODEM] send negotiated mode=%s via %s\n", usecrc ? "CRC" : "CHECKSUM", xm_name(c));

  bytesleft = xfer_file_size;
  sentbytes = 0;
  blocknum = 1;

  xfer_progress_status("Uploading...", 0, xfer_file_size);

  blocksize = (bytesleft > 896 && send1k && usecrc) ? 1024 : 128;
  if (xmodem_load_block(blocksize) == 0) {
    xfer_progress_status("Read error", 0, xfer_file_size);
    return 0;
  }
  xmodem_send_block(blocknum, blocksize, usecrc);

  while (bytesleft > 0) {
    c = xm_recv_byte_dbg(10000);
    switch (c) {
    case -2:
      xmodem_fail("Disconnected!");
      return 0;
    case XM_CAN:
      xfer_progress_status("Canceled by remote!", sentbytes, xfer_file_size);
      return 0;
    case XM_ACK:
      if (blocknum != 0) {
        sentbytes += xmodem_last_payload_len;
        bytesleft -= xmodem_last_payload_len;
        if (sentbytes > xfer_file_size) {
          sentbytes = xfer_file_size;
        }
        if (bytesleft < 0) {
          bytesleft = 0;
        }
      }
      ++blocknum;
      XMDBG("[XMODEM] send ACK block complete sent=%d left=%d next_block=%u\n", sentbytes, bytesleft, blocknum);
      xfer_progress_status("Uploading...", sentbytes, xfer_file_size);
      blocksize = (bytesleft > 896 && send1k && usecrc) ? 1024 : 128;
      if (bytesleft > 0) {
        if (xmodem_load_block(blocksize) == 0) {
          xfer_progress_status("Read error", sentbytes, xfer_file_size);
          return 0;
        }
        xmodem_send_block(blocknum, blocksize, usecrc);
      }
      break;
    case XM_NAK:
      XMDBG("[XMODEM] send NAK resend block=%u size=%d\n", blocknum, blocksize);
      xfer_progress_status("Resending...", sentbytes, xfer_file_size);
      xmodem_send_block(blocknum, blocksize, usecrc);
      break;
    default:
      XMDBG("[XMODEM] send ignoring byte=%d (%02X)\n", c, c >= 0 ? (c & 0xff) : 0);
      break;
    }
  }

  xfer_progress_status("Finishing...", sentbytes, xfer_file_size);
  xm_send_byte_dbg(XM_EOT);
  c = xm_recv_byte_dbg(10000);
  if (c != XM_ACK) {
    xm_send_byte_dbg(XM_EOT);
    c = xm_recv_byte_dbg(10000);
  }

  xfer_progress_status("Transfer complete", xfer_file_size, xfer_file_size);
  return 1;
}
