
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <SDL.h>
#include "gfx.h"
#include "menu.h"
#include "xfer.h"
#include "rainbow.h"
#include "timer.h"

#define RB_CAN 0xc1
#define RB_ACK 0x81
#define RB_NAK 0x85
#define RB_EOT 0xa1
#define RB_EOD 0x91
#define RB_SOH 0x89
#define RB_GOO 0x83

#define RB_DEFAULT_BLOCKSIZE 255
#define RB_MAX_RETRIES 10
#define RB_START_TIMEOUT 10000
#define RB_BYTE_TIMEOUT 1000

static int rainbow_sent_bytes = 0;
static int rainbow_total_blocks = 0;

static int rainbow_total_blocks_from_header(long advertised_value) {
  if (advertised_value <= 0) {
    return 0;
  }

  /*
   * Interop note:
   * Many Commodore-side Rainbow implementations advertise the total number
   * of data blocks here, not the raw byte size. For receive progress we want
   * one visible step per received block, so use the header value directly as
   * the authoritative block total.
   */
  return (int)advertised_value;
}

static void rainbow_recv_progress(int downloaded_blocks, int complete) {
  int total_blocks = rainbow_total_blocks;

  if (downloaded_blocks < 0) {
    downloaded_blocks = 0;
  }
  if (complete && total_blocks > 0) {
    downloaded_blocks = total_blocks;
  }
  if (total_blocks > 0 && downloaded_blocks > total_blocks) {
    downloaded_blocks = total_blocks;
  }

  menu_update_xfer_block_progress("Downloading:", "Rainbow", downloaded_blocks, total_blocks);
  gfx_vbl();
}

static void rainbow_status(const char *message) {
  menu_update_xfer_progress((char *)message, xfer_saved_bytes, xfer_file_size);
  gfx_vbl();
}

static void rainbow_send_status(const char *message) {
  menu_update_xfer_progress((char *)message, rainbow_sent_bytes, xfer_file_size);
  gfx_vbl();
}

static void rainbow_fail(const char *message) {
  if (xfer_direction == DIR_SEND) {
    rainbow_send_status(message);
  } else {
    rainbow_status(message);
  }
}

static unsigned char rainbow_checksum(const unsigned char *data, int len) {
  unsigned int sum = 0;
  while (len-- > 0) {
    sum = (sum + *data++) & 0xff;
  }
  return (unsigned char)sum;
}

static int rainbow_recv_exact(unsigned char *buf, int len, int timeout) {
  int i;
  signed int c;

  for (i = 0; i < len; ++i) {
    c = xfer_recv_byte(timeout);
    if (c < 0) {
      return c;
    }
    buf[i] = (unsigned char)c;
  }
  return len;
}

static int rainbow_recv_data_with_progress(unsigned char *buf, int len, int timeout, int blockno) {
  (void)blockno;
  return rainbow_recv_exact(buf, len, timeout);
}

static int rainbow_send_block(unsigned char blockno, const unsigned char *data, int len) {
  int retries;
  signed int c;
  unsigned char sum;

  if (len < 0 || len > 255) {
    return 0;
  }

  sum = rainbow_checksum(data, len);

  for (retries = 0; retries < RB_MAX_RETRIES && !xfer_cancel; ++retries) {
    xfer_send_byte(RB_SOH);
    /* Scan for ACK — skip any stray status screen bytes */
    {
      int scan_count = 64;
      int got_ack = 0;
      while (scan_count-- > 0) {
        c = xfer_recv_byte(RB_START_TIMEOUT);
        if (c < 0) break;
        if ((unsigned char)c == RB_ACK) { got_ack = 1; break; }
        if ((unsigned char)c == RB_CAN) {
          rainbow_fail("Rainbow: cancelled by remote");
          return 0;
        }
      }
      if (!got_ack) {
        rainbow_send_status("Rainbow: waiting for ACK...");
        continue;
      }
    }

    xfer_send_byte(blockno);
    xfer_send_byte(0xff ^ blockno);
    xfer_send_byte((unsigned char)len);
    xfer_send_byte(0xff ^ (unsigned char)len);
    while (len-- > 0) {
      xfer_send_byte(*data++);
    }
    xfer_send_byte(RB_EOD);
    xfer_send_byte(sum);
    xfer_send_byte(0xff ^ sum);

    c = xfer_recv_byte(RB_START_TIMEOUT);
    if (c < 0) {
      rainbow_send_status("Rainbow: block timeout, retrying");
      continue;
    }
    switch ((unsigned char)c) {
    case RB_ACK:
      return 1;
    case RB_GOO:
    case RB_NAK:
      rainbow_send_status("Rainbow: block retry requested");
      break;
    case RB_CAN:
      rainbow_fail("Rainbow: cancelled by remote");
      return 0;
    default:
      rainbow_send_status("Rainbow: unexpected reply, retrying");
      break;
    }
  }

  if (xfer_cancel) {
    xfer_send_byte(RB_CAN);
    rainbow_fail("Rainbow: cancelled");
  } else {
    rainbow_fail("Rainbow: too many block retries");
  }
  return 0;
}

static int rainbow_send_eot(void) {
  int retries;
  signed int c;

  for (retries = 0; retries < RB_MAX_RETRIES && !xfer_cancel; ++retries) {
    xfer_send_byte(RB_EOT);
    c = xfer_recv_byte(RB_START_TIMEOUT);
    if (c < 0) {
      continue;
    }
    if ((unsigned char)c == RB_ACK) {
      rainbow_send_status("Transfer complete");
      return 1;
    }
    if ((unsigned char)c == RB_CAN) {
      rainbow_fail("Rainbow: cancelled by remote");
      return 0;
    }
  }

  if (xfer_cancel) {
    xfer_send_byte(RB_CAN);
    rainbow_fail("Rainbow: cancelled");
  } else {
    rainbow_fail("Rainbow: EOT failed");
  }
  return 0;
}

static int rainbow_recv_block(unsigned char *blockno, unsigned char *data, int *len_out) {
  unsigned char hdr[4];
  unsigned char csum[2];
  unsigned char eod;
  unsigned char sum;
  int len;

  if (rainbow_recv_exact(hdr, 4, RB_BYTE_TIMEOUT) < 0) {
    rainbow_status("Rainbow: header timeout");
    return -1;
  }

  if ((unsigned char)(hdr[0] ^ hdr[1]) != 0xff) {
    xfer_send_byte(RB_GOO);
    rainbow_status("Rainbow: block number mismatch");
    return 0;
  }
  if ((unsigned char)(hdr[2] ^ hdr[3]) != 0xff) {
    xfer_send_byte(RB_GOO);
    rainbow_status("Rainbow: block length mismatch");
    return 0;
  }

  *blockno = hdr[0];
  len = hdr[2];
  if (len > 0 && rainbow_recv_data_with_progress(data, len, RB_BYTE_TIMEOUT, *blockno) < 0) {
    xfer_send_byte(RB_GOO);
    rainbow_status("Rainbow: data timeout");
    return 0;
  }

  if (rainbow_recv_exact(&eod, 1, RB_BYTE_TIMEOUT) < 0 || eod != RB_EOD) {
    xfer_send_byte(RB_GOO);
    rainbow_status("Rainbow: missing EOD");
    return 0;
  }

  if (rainbow_recv_exact(csum, 2, RB_BYTE_TIMEOUT) < 0) {
    xfer_send_byte(RB_GOO);
    rainbow_status("Rainbow: checksum timeout");
    return 0;
  }

  if ((unsigned char)(csum[0] ^ csum[1]) != 0xff) {
    xfer_send_byte(RB_NAK);
    rainbow_status("Rainbow: checksum complement mismatch");
    return 0;
  }

  sum = rainbow_checksum(data, len);
  if (sum != csum[0]) {
    xfer_send_byte(RB_GOO);
    rainbow_status("Rainbow: checksum failed");
    return 0;
  }

  *len_out = len;
  return 1;
}

static const char *rainbow_ext_from_type(unsigned char filetype) {
  switch (filetype) {
  case 'S':
    return ".seq";
  case 'U':
    return ".usr";
  case 'R':
    return ".rel";
  case 'P':
  default:
    return ".prg";
  }
}

int rainbow_recv(void) {
  unsigned char blockno;
  unsigned char filetype = 'P';
  unsigned char block[260];
  int blocklen;
  int retries = 0;
  int expected_block = 0;
  long advertised_size = 0;
  signed int c;

  snprintf(xfer_filename, 256, "download.prg");
  rainbow_total_blocks = 0;
  rainbow_status("Rainbow: starting receive");

  while (!xfer_cancel) {
    if (retries++ >= RB_MAX_RETRIES) {
      rainbow_fail("Rainbow: start handshake failed");
      return 0;
    }

    xfer_send_byte(RB_GOO);
    c = xfer_recv_byte(RB_START_TIMEOUT);
    if (c < 0) {
      continue;
    }
    if ((unsigned char)c == RB_CAN) {
      rainbow_fail("Rainbow: cancelled by remote");
      return 0;
    }
    if ((unsigned char)c != RB_SOH) {
      continue;
    }

    xfer_send_byte(RB_ACK);
    if (rainbow_recv_block(&blockno, block, &blocklen) <= 0) {
      continue;
    }

    if (blockno != 0 || blocklen < 5 || block[0] != 0x01) {
      xfer_send_byte(RB_GOO);
      rainbow_status("Rainbow: invalid block 0");
      continue;
    }

    advertised_size = (long)block[1] | ((long)block[2] << 8);
    rainbow_total_blocks = rainbow_total_blocks_from_header(advertised_size);
    xfer_file_size = 0;
    filetype = block[3];

    {
      char tmpname[64];
      snprintf(tmpname, sizeof(tmpname), "download%s", rainbow_ext_from_type(filetype));
      snprintf(xfer_filename, 256, "%s", tmpname);
    }

    rainbow_recv_progress(0, 0);
    xfer_send_byte(RB_ACK);
    expected_block = 1;
    break;
  }

  if (xfer_cancel) {
    xfer_send_byte(RB_CAN);
    rainbow_fail("Rainbow: cancelled");
    return 0;
  }

  for (;;) {
    if (xfer_cancel) {
      xfer_send_byte(RB_CAN);
      rainbow_fail("Rainbow: cancelled");
      return 0;
    }

    c = xfer_recv_byte(RB_START_TIMEOUT);
    if (c < 0) {
      rainbow_fail("Rainbow: timed out waiting for block");
      return 0;
    }

    if ((unsigned char)c == RB_EOT) {
      xfer_send_byte(RB_ACK);
      if (rainbow_total_blocks <= 0) {
        rainbow_total_blocks = expected_block - 1;
      }
      rainbow_recv_progress(expected_block - 1, 1);
      xfer_file_size = xfer_saved_bytes;
      return 1;
    }
    if ((unsigned char)c == RB_CAN) {
      rainbow_fail("Rainbow: cancelled by remote");
      return 0;
    }
    if ((unsigned char)c != RB_SOH) {
      continue;
    }

    xfer_send_byte(RB_ACK);
    if (rainbow_recv_block(&blockno, block, &blocklen) <= 0) {
      continue;
    }

    if (blockno == (unsigned char)(expected_block - 1)) {
      /* duplicate of the previous block — use the same 8-bit wrap semantics
       * as the accept path below so detection still works past 256 blocks */
      xfer_send_byte(RB_ACK);
      continue;
    }
    if (blockno != (unsigned char)expected_block) {
      xfer_send_byte(RB_GOO);
      rainbow_status("Rainbow: wrong block number");
      continue;
    }

    if (blocklen > 0) {
      if (xfer_save_data(block, blocklen) != blocklen) {
        xfer_send_byte(RB_CAN);
        rainbow_fail("Rainbow: write error");
        return 0;
      }
      rainbow_recv_progress(expected_block, 0);
    }

    xfer_send_byte(RB_ACK);
    ++expected_block;
  }
}

int rainbow_send(const char *filename) {
  unsigned char block[260];
  unsigned char blockno = 0;
  int blocklen;
  const char *ext;
  unsigned char filetype = 'P';

  rainbow_sent_bytes = 0;

  /*
   * Rainbow upload handshake (from rainbow_protocol_cb.asm):
   *
   * BBS receiver flow:
   *   _c49d: drain incoming bytes until silence (timeout)
   *   _c4b1: send GOO ($83) repeatedly, wait for SOH ($89)
   *   _c41b: got SOH → send ACK, receive block
   *
   * Problem: We can't reliably detect GOO ($83) because it's
   * the same as PETSCII orange (appears in BBS status screen).
   *
   * Solution: Skip GOO detection entirely. Instead:
   * 1. Send a wake-up byte to satisfy _c49d drain loop
   * 2. Wait for silence (BBS finishes status screen + drain)
   * 3. Let rainbow_send_block handle the SOH/ACK handshake
   *    The BBS at _c4b1 sends GOO and waits for SOH.
   *    rainbow_send_block sends SOH and waits for ACK.
   *    When BBS gets SOH it sends ACK. Handshake complete.
   */
  /*
   * Rainbow upload: the BBS receiver sends GOO (0x83) when ready.
   * 0x83 also appears in PETSCII status screen output.
   * The BBS sends GOO in a tight loop at _c4b1, waiting for SOH.
   * We just need to get past the status screen to the GOO loop.
   *
   * Strategy: read bytes. When we see 0x83, immediately try sending
   * SOH. If the BBS responds with ACK, we're in the protocol.
   * If not (bad SOH handshake), the retry loop handles it.
   * rainbow_send_block already retries SOH/ACK 10 times.
   *
   * No drain phase needed — just go straight to send_block.
   * The send_block SOH/ACK retry will naturally sync with the
   * BBS's GOO/SOH handshake.
   */
  rainbow_send_status("Rainbow: connecting to receiver");

  ext = strrchr(filename, '.');
  if (ext) {
    if (strcasecmp(ext, ".seq") == 0) {
      filetype = 'S';
    } else if (strcasecmp(ext, ".usr") == 0) {
      filetype = 'U';
    } else if (strcasecmp(ext, ".rel") == 0) {
      filetype = 'R';
    }
  }

  block[0] = 0x01;
  block[1] = (unsigned char)(xfer_file_size & 0xff);
  block[2] = (unsigned char)((xfer_file_size >> 8) & 0xff);
  block[3] = filetype;
  block[4] = 0x00;
  if (!rainbow_send_block(blockno++, block, 5)) {
    return 0;
  }

  rainbow_send_status("Uploading...");
  while ((blocklen = xfer_load_data(block, RB_DEFAULT_BLOCKSIZE)) > 0) {
    if (!rainbow_send_block(blockno++, block, blocklen)) {
      return 0;
    }
    rainbow_sent_bytes += blocklen;
    if (rainbow_sent_bytes > xfer_file_size) {
      rainbow_sent_bytes = xfer_file_size;
    }
    rainbow_send_status("Uploading...");
  }

  return rainbow_send_eot();
}
