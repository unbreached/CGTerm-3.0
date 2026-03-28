#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "gfx.h"
#include "menu.h"
#include "xfer.h"
#include "timer.h"

#define PUNTER_BLOCK_DATA_MAX 247  /* pktsize(254) - 7 header bytes */
#define PUNTER_SEND_TIMEOUT 1000
#define PUNTER_MAX_RETRIES 10

int punter_last_filetype = 0;
static int punter_scan_for_string(const char *target, int timeout_ms, int max_bytes);


void punter_fail(char *message) {
  //printf("punter_fail: %s\n", message);
  menu_update_xfer_progress(message, xfer_saved_bytes, 0);
  gfx_vbl();
}


void punter_retry(char *message) {
  //printf("punter_retry: %s\n", message);
  menu_update_xfer_progress(message, xfer_saved_bytes, 0);
  gfx_vbl();
}


void punter_send_string(char *s) {
  //printf("punter_send_string: %s\n", s);
  while (*s) {
    xfer_send_byte(*s++);
  }
}


int punter_recv_string(char *sendstring, char *recvstring) {
  int c, bytecnt;
  int errorcnt;

  //printf("punter_recv_string: sending \"%s\"\n", sendstring);
  memset(recvstring, 0, 4);
  if (sendstring[0]) {
    punter_send_string(sendstring);
  }
  bytecnt = 0;
  errorcnt = 20;
  while (bytecnt != 3) {
    c = xfer_recv_byte(3000);
    if (c < 0) {
      if (--errorcnt <= 0 || xfer_cancel) {
        return(0);
      }
      //printf("punter_recv_string: timeout %d\n", errorcnt);
      if (sendstring[0]) {
        punter_send_string(sendstring);
      }
      //printf("punter_recv_string: sending \"%s\"\n", sendstring);
      bytecnt = 0;
      continue;
    }
    recvstring[bytecnt++] = c;
  }
  recvstring[3] = 0;
  //printf("punter_recv_string: received \"%s\"\n", recvstring);

  return(3);
}


int punter_handshake(char *sendstring, char *waitstring) {
  char p[4];
  int l = 0;
  int errorcnt = 20;

  while (errorcnt--) {
    l = punter_recv_string(sendstring, p);
    if (l == 3) {
      if (strcmp(waitstring, p) == 0) {
	//printf("punter_handshake: got \"%s\", done\n", waitstring);
	return(1);
      } else if (strcmp(sendstring, p) == 0) {
	/* Could be BBS status screen output (e.g. "Good:" matches "GOO").
	 * Don't fail — just retry. */
      } else {
	if (strcmp("ACK", waitstring) == 0) {
	  if (strcmp("CKA", p) == 0) {
	    xfer_recv_byte(100);
	    xfer_recv_byte(100);
	    return(1);
	  }
	  if (strcmp("KAC", p) == 0) {
	    xfer_recv_byte(100);
	    return(1);
	  }
	}
      }
    }
  }
  //printf("punter_handshake: failed\n");
  return(0);
}


int punter_checksum(int len) {
  unsigned short cksum = 0;
  unsigned short clc = 0;
  unsigned char *data = xfer_buffer + 4;

  len -= 4;
  while (len--) {
    cksum += *data;
    clc ^= *data++;
    clc = (clc<<1) | (clc>>15);
  }
  if (cksum == (xfer_buffer[0] | (xfer_buffer[1]<<8))) {
    if (clc == (xfer_buffer[2] | (xfer_buffer[3]<<8))) {
      return(1);
    }
  }
  //printf("punter_checksum: cksum = %04x (%04x)\n", cksum, xfer_buffer[0] | (xfer_buffer[1]<<8));
  //printf("punter_checksum:   clc = %04x (%04x)\n", clc, xfer_buffer[2] | (xfer_buffer[3]<<8));
  return(0);
}


unsigned short punter_next_blocknum(void) {
  return(xfer_buffer[5] | (xfer_buffer[6]<<8));
}


signed int punter_recv_block(int len) {
  signed int c;
  int bytecnt;
  int errorcnt = 10;

 restart:
  //printf("punter_recv_block: receiving %d byte block\n", len);
  punter_send_string("S/B");
  bytecnt = 0;
  while (bytecnt < len) {
    if ((c = xfer_recv_byte_error(500, 10)) < 0) {
      if (bytecnt == 3) {
	if (strncmp("S/B", (const char *)xfer_buffer, 3) == 0) {
	  menu_update_xfer_progress("Transfer canceled by remote", xfer_saved_bytes, 0);
	  gfx_vbl();
	  return(-1);
	}
      }
      menu_update_xfer_progress("Block timed out, retrying", xfer_saved_bytes, 0);
      gfx_vbl();
      if (punter_handshake("BAD", "ACK")) {
	if (errorcnt--) {
	  goto restart;
	} else {
	  menu_update_xfer_progress("Block timed out", xfer_saved_bytes, 0);
	  gfx_vbl();
	  return(-1);
	}
      } else {
	menu_update_xfer_progress("Handshake timed out", xfer_saved_bytes, 0);
	gfx_vbl();
	//printf("punter_recv_block: bad handshake timeout\n");
	return(-1);
      }
    }
    //printf("punter_recv_block: received byte %3d: %02x\n", bytecnt, c);
    xfer_buffer[bytecnt++] = c;
    if (bytecnt == 4) {
      if (strncmp("ACK", (const char *)xfer_buffer, 3) == 0) {
	menu_update_xfer_progress("Lost sync, retrying...", xfer_saved_bytes, 0);
	gfx_vbl();
	if (xfer_buffer[3] == 'A') {
	  goto restart;
	} else {
	  //printf("punter_recv_block: skipping late ack\n");
	  xfer_buffer[0] = xfer_buffer[3];
	  bytecnt = 1;
	}
      }
    }
    if (bytecnt == 8) {
      if (strncmp("ACKACK", (const char *)(xfer_buffer + 2), 6) == 0) {
	menu_update_xfer_progress("Lost sync, retrying...", xfer_saved_bytes, 0);
	gfx_vbl();
	//printf("punter_recv_block: lost sync, restarting block\n");
	goto restart;
      }
      if (strncmp("CKACKA", (const char *)(xfer_buffer + 2), 6) == 0) {
	menu_update_xfer_progress("Lost sync, retrying...", xfer_saved_bytes, 0);
	gfx_vbl();
	//printf("punter_recv_block: lost sync, restarting block\n");
	goto restart;
      }
      if (strncmp("KACKAC", (const char *)(xfer_buffer + 2), 6) == 0) {
	menu_update_xfer_progress("Lost sync, retrying...", xfer_saved_bytes, 0);
	gfx_vbl();
	//printf("punter_recv_block: lost sync, restarting block\n");
	goto restart;
      }
    }
  }
  if (punter_checksum(bytecnt)) {
    if (punter_handshake("GOO", "ACK") == 0) {
      menu_update_xfer_progress("Handshake timed out", xfer_saved_bytes, 0);
      gfx_vbl();
      //printf("punter_recv_block: goo handshake timeout\n");
      return(-1);
    }
    menu_update_xfer_progress("Downloading...", xfer_saved_bytes, 0);
    gfx_vbl();
    if (len <= 8) {
      //printf("punter_recv_block: short block, returning %d\n", xfer_buffer[4]);
      return(xfer_buffer[4]);
    }
    if (xfer_save_data(xfer_buffer + 7, len - 7)) {
      //printf("punter_recv_block: returning %d\n", xfer_buffer[4]);
      return(xfer_buffer[4]);
    } else {
      punter_fail("Write error!");
      gfx_vbl();
      return(-1);
    }
  } else {
    menu_update_xfer_progress("Checksum failed, retrying", xfer_saved_bytes, 0);
    gfx_vbl();
    //printf("punter_recv_block: checksum failed\n");
    if (punter_handshake("BAD", "ACK")) {
      if (errorcnt--) {
	goto restart;
      } else {
	menu_update_xfer_progress("Checksum failed", xfer_saved_bytes, 0);
	gfx_vbl();
	return(-1);
      }
    } else {
      menu_update_xfer_progress("Handshake timed out", xfer_saved_bytes, 0);
      gfx_vbl();
      //printf("punter_recv_block: bad handshake timeout\n");
      return(-1);
    }
  }
}


void punter_countdown(int num) {
  char s[24];

  snprintf(s, sizeof(s), "Starting in %d", num);
  menu_update_xfer_progress(s, xfer_saved_bytes, 0);
  gfx_vbl();
}


int punter_recv(void) {
  signed int nextblocksize;
  int attempts;

  menu_update_xfer_progress("Starting...", xfer_saved_bytes, 0);
  gfx_vbl();

  /*
   * Initial handshake: send "GOO" repeatedly while consuming incoming
   * bytes (BBS status screen), scanning for "ACK" in the stream.
   */
  {
    int got_ack = 0;
    signed int c;
    unsigned int last_goo_time = 0;
    unsigned int now;

    attempts = 30;
    punter_send_string("GOO");
    last_goo_time = timer_get_ticks();

    while (attempts > 0 && !xfer_cancel) {
      c = xfer_recv_byte(1000);

      now = timer_get_ticks();
      if (now > last_goo_time + 2000) {
        punter_send_string("GOO");
        last_goo_time = now;
        attempts--;
      }

      if (c < 0) {
        continue;
      }

      if (c == 'A') {
        c = xfer_recv_byte(500);
        if (c == 'C') {
          c = xfer_recv_byte(500);
          if (c == 'K') {
            got_ack = 1;
            break;
          }
        }
      }
    }
    if (!got_ack) {
      punter_fail("Timed out");
      return(0);
    }
    punter_countdown(5);
  }

  punter_last_filetype = 0;

  nextblocksize = punter_recv_block(8);
  if (nextblocksize < 0) {
    //punter_fail("Timed out on filetype block");
    return(0);
  }

  punter_last_filetype = xfer_buffer[7];

  if (punter_handshake("GOO", "ACK")) {
    punter_countdown(4);
  } else {
    punter_fail("Handshake timeout");
    return(0);
  }

  if (punter_handshake("S/B", "SYN")) {
    punter_countdown(3);
  } else {
    punter_fail("Handshake timeout");
    return(0);
  }

  if (punter_handshake("SYN", "S/B")) {
    punter_countdown(2);
  } else {
    punter_fail("Handshake timeout");
    return(0);
  }

  if (punter_handshake("GOO", "ACK")) {
    punter_countdown(1);
  } else {
    punter_fail("Handshake timeout");
    return(0);
  }

  nextblocksize = punter_recv_block(7);
  if (nextblocksize < 0) {
    // error
    punter_fail("file start timeout");
    return(0);
  }

  while (punter_next_blocknum() < 0xff00 && nextblocksize >= 7) {
    nextblocksize = punter_recv_block(nextblocksize);
  }
  if (nextblocksize < 0) {
    //punter_fail("Block timeout");
    return(0);
  }

  menu_update_xfer_progress("Finishing...", xfer_saved_bytes, 0);
  gfx_vbl();

  if (punter_handshake("S/B", "SYN")) {
    punter_handshake("SYN", "S/B");
    menu_update_xfer_progress("Finished", xfer_saved_bytes, 0);
    gfx_vbl();
  } else {
    menu_update_xfer_progress("Done, but handshake timed out", xfer_saved_bytes, 0);
    gfx_vbl();
  }
  return(1);
}


static void punter_build_checksum(unsigned char *buf, int len) {
  unsigned short cksum = 0;
  unsigned short clc = 0;
  unsigned char *data = buf + 4;
  int datalen = len - 4;

  while (datalen--) {
    cksum += *data;
    clc ^= *data++;
    clc = (clc << 1) | (clc >> 15);
  }
  buf[0] = cksum & 0xff;
  buf[1] = (cksum >> 8) & 0xff;
  buf[2] = clc & 0xff;
  buf[3] = (clc >> 8) & 0xff;
}


/*
 * Wait for a 3-byte string from remote, then send a response.
 * This is the mirror of punter_handshake() — used by the sender side
 * where the BBS (receiver) speaks first.
 */
static int punter_wait_handshake(char *waitstring, char *sendstring) {
  char p[4];
  int bytecnt;
  int errorcnt = 20;
  signed int c;

  while (errorcnt-- > 0 && !xfer_cancel) {
    memset(p, 0, 4);
    bytecnt = 0;

    while (bytecnt < 3) {
      c = xfer_recv_byte(3000);
      if (c < 0) {
        break;
      }
      p[bytecnt++] = (char)c;
    }

    if (bytecnt == 3) {
      p[3] = 0;
      if (strcmp(waitstring, p) == 0) {
        punter_send_string(sendstring);
        return 1;
      }
      /* Handle rotated ACK variants */
      if (strcmp("ACK", waitstring) == 0) {
        if (strcmp("CKA", p) == 0) {
          xfer_recv_byte(100);
          xfer_recv_byte(100);
          punter_send_string(sendstring);
          return 1;
        }
        if (strcmp("KAC", p) == 0) {
          xfer_recv_byte(100);
          punter_send_string(sendstring);
          return 1;
        }
      }
    }
  }
  return 0;
}


/*
 * Wait for "S/B" from receiver, then send block data.
 * After sending, wait for "GOO" (accepted) or "BAD" (retry),
 * then respond with "ACK".
 */
/*
 * Scan incoming bytes for a 3-byte pattern (e.g. "S/B", "GOO", "BAD").
 * More resilient than reading exactly 3 bytes — handles stray bytes in the stream.
 */
static int punter_scan_for_string(const char *target, int timeout_ms, int max_bytes) {
  signed int c;
  int pos = 0;
  int total = 0;

  while (total < max_bytes && !xfer_cancel) {
    c = xfer_recv_byte(timeout_ms);
    if (c < 0) {
      return 0;
    }
    total++;
    if ((char)c == target[pos]) {
      pos++;
      if (pos == 3) {
        return 1;
      }
    } else {
      pos = ((char)c == target[0]) ? 1 : 0;
    }
  }
  return 0;
}


static int punter_send_block(int len) {
  int i;
  int retries = PUNTER_MAX_RETRIES;

  while (retries-- > 0 && !xfer_cancel) {
    /* Wait for S/B from receiver — scan stream for pattern */
    if (!punter_scan_for_string("S/B", 3000, 64)) {
      punter_fail("Timeout waiting for S/B");
      return 0;
    }

    /* Send the block data */
    for (i = 0; i < len; i++) {
      xfer_send_byte(xfer_buffer[i]);
    }

    /* Wait for GOO or BAD — scan stream */
    {
      char resp[4];
      int pos = 0;
      int total = 0;
      signed int c;

      memset(resp, 0, 4);
      while (total < 64 && !xfer_cancel) {
        c = xfer_recv_byte(3000);
        if (c < 0) {
          break;
        }
        total++;
        resp[pos++] = (char)c;
        if (pos >= 3) {
          resp[3] = 0;
          if (strcmp("GOO", resp) == 0) {
            punter_send_string("ACK");
            return 1;
          } else if (strcmp("BAD", resp) == 0) {
            punter_send_string("ACK");
            menu_update_xfer_progress("Block rejected, retrying", xfer_saved_bytes, xfer_file_size);
            gfx_vbl();
            break;  /* retry the block */
          }
          /* Shift window: try to match with overlap */
          resp[0] = resp[1];
          resp[1] = resp[2];
          pos = 2;
        }
      }
      if (xfer_cancel) break;
    }
  }

  if (xfer_cancel) {
    punter_fail("Cancelled");
  } else {
    punter_fail("Too many retries");
  }
  return 0;
}


/*
 * Punter upload — protocol traced from C*BASE punter.a65 receive side:
 *
 * Phase 1: BBS calls initrecv2 (filetype block, bufsize=8)
 *   BBS: recvblk → send GOO, wait ACK
 *   Us:  wait GOO, send ACK
 *   BBS: goodblock → send S/B, receive 8 bytes
 *   Us:  wait S/B, send filetype block (blknum=$ffff)
 *   BBS: checksum ok → recvblk → send GOO, wait ACK
 *   Us:  wait GOO, send ACK  (inside send_block)
 *   BBS: goodblock → lastblkflag (blknum=$ffff) → lastblock SYN exchange
 *   Us:  wait S/B → send SYN, wait SYN → send S/B
 *   BBS: initrecv2 returns
 *
 * Phase 2: BBS calls receive2 (data blocks, bufsize=7)
 *   BBS: recvblk → send GOO, wait ACK
 *   Us:  wait GOO, send ACK
 *   For each block:
 *     BBS: goodblock → send S/B, receive data
 *     Us:  wait S/B, send block data
 *     BBS: checksum ok → recvblk → send GOO, wait ACK
 *     Us:  wait GOO, send ACK  (inside send_block)
 *   Last block (blknum=$ffff):
 *     BBS: lastblock SYN exchange
 *     Us:  wait S/B → send SYN, wait SYN → send S/B
 */
static int punter_send_internal(int send_presignal) {
  int blocknum;
  int sent_bytes = 0;
  int remaining;
  char status_msg[64];

  menu_update_xfer_progress("Signaling BBS...", 0, xfer_file_size);
  gfx_vbl();

  /*
   * Phase 1: Filetype init block
   *
   * For single Punter upload (bbs.bas lines 5591-5620):
   * The BBS waits for CGTerm to send "GOO" BEFORE it enters initrecv2.
   *
   * For Multi Punter upload (bbs.bas lines 3600-3678):
   * The BBS already received the filename announcement and goes straight
   * to sys51008 (initrecv2). No pre-signal needed.
   */

  if (send_presignal) {
    /* Send GOO to signal the BBS we're ready to upload */
    punter_send_string("GOO");
  }

  /* Now BBS enters initrecv2 → recvblk → sends GOO, waits ACK */

  menu_update_xfer_progress("Waiting for BBS...", 0, xfer_file_size);
  gfx_vbl();

  if (!punter_wait_handshake("GOO", "ACK")) {
    punter_fail("Initial handshake failed");
    return 0;
  }

  punter_countdown(5);

  /* Build filetype block (8 bytes): checksum[4] + nextsize + blklo + blkhi + filetype
   * Block number MUST be $ffff to trigger BBS lastblock/SYN exchange */
  memset(xfer_buffer, 0, 8);
  xfer_buffer[4] = 0;      /* next block size = 0 (matches BBS initsend2) */
  xfer_buffer[5] = 0xff;   /* block number lo = $ff */
  xfer_buffer[6] = 0xff;   /* block number hi = $ff (triggers lastblkflag) */
  xfer_buffer[7] = 2;      /* filetype: PRG */
  punter_build_checksum(xfer_buffer, 8);

  /* send_block: waits S/B, sends data, waits GOO, sends ACK */
  if (!punter_send_block(8)) {
    punter_fail("Failed to send filetype block");
    return 0;
  }

  /*
   * BBS has lastblkflag set → lastblock SYN exchange:
   * BBS sends S/B, waits SYN → BBS sends SYN, waits (short timeout)
   */

  punter_countdown(4);

  if (!punter_wait_handshake("S/B", "SYN")) {
    punter_fail("SYN handshake failed");
    return 0;
  }
  punter_countdown(3);

  if (!punter_wait_handshake("SYN", "S/B")) {
    punter_fail("S/B handshake failed");
    return 0;
  }
  punter_countdown(2);

  /*
   * Phase 2: Data blocks
   * BBS calls receive2 → setup1 (resets flags, bufsize=7) → recvblk → sends GOO, waits ACK
   */


  if (!punter_wait_handshake("GOO", "ACK")) {
    punter_fail("Data phase handshake failed");
    return 0;
  }
  punter_countdown(1);

  /* Send data blocks using double-buffering (look-ahead), matching the BBS sender.
   *
   * The BBS receiver uses byte[4] of the CURRENT block to know how many bytes
   * the NEXT block will be. So we must read ahead one block to know its size
   * before sending the current one.
   *
   * BBS receive2 starts with bufsize=7, so the first block must be 7 bytes
   * (header only) with byte[4] = size of the first real data block. */

  {
    /* Double-buffer: cur_buf holds the block being sent, next_buf holds look-ahead */
    unsigned char cur_buf[260], next_buf[260];
    int cur_len, next_len, cur_total, next_total;
    int is_last;

    /* Read first data chunk into next_buf (look-ahead) */
    remaining = xfer_file_size - sent_bytes;
    next_len = xfer_load_data(next_buf, remaining < PUNTER_BLOCK_DATA_MAX ? remaining : PUNTER_BLOCK_DATA_MAX);
    if (next_len <= 0) next_len = 0;
    next_total = next_len + 7;

    /* First block: 7-byte header only, byte[4] = size of first real data block */
    xfer_buffer[4] = (unsigned char)next_total;
    xfer_buffer[5] = 0;
    xfer_buffer[6] = 0;
    punter_build_checksum(xfer_buffer, 7);

    if (!punter_send_block(7)) {
      punter_fail("Failed to send first data block");
      return 0;
    }

    blocknum = 1;
    while (!xfer_cancel && next_len > 0) {
      /* Current block = what was in next_buf */
      memcpy(cur_buf, next_buf, next_len);
      cur_len = next_len;
      cur_total = next_total;
      sent_bytes += cur_len;

      /* Read ahead: get the NEXT block's data */
      remaining = xfer_file_size - sent_bytes;
      if (remaining > 0) {
        next_len = xfer_load_data(next_buf, remaining < PUNTER_BLOCK_DATA_MAX ? remaining : PUNTER_BLOCK_DATA_MAX);
        if (next_len <= 0) next_len = 0;
        next_total = next_len + 7;
      } else {
        next_len = 0;
        next_total = 0;
      }

      /* Is this the last data block? */
      is_last = (next_len == 0);

      /* Build the block to send */
      memcpy(xfer_buffer + 7, cur_buf, cur_len);
      if (is_last) {
        xfer_buffer[4] = 0;
        xfer_buffer[5] = 0xff;
        xfer_buffer[6] = 0xff;
      } else {
        /* byte[4] = NEXT block's total size (look-ahead) */
        xfer_buffer[4] = (unsigned char)next_total;
        xfer_buffer[5] = blocknum & 0xff;
        xfer_buffer[6] = (blocknum >> 8) & 0xff;
      }
      punter_build_checksum(xfer_buffer, cur_total);

      if (!punter_send_block(cur_total)) {
        punter_fail("Block send failed");
        return 0;
      }

      xfer_saved_bytes = sent_bytes;
      blocknum++;

      snprintf(status_msg, sizeof(status_msg), "Uploading... %d bytes", sent_bytes);
      menu_update_xfer_progress(status_msg, sent_bytes, xfer_file_size);
      gfx_vbl();
    }
  }

  if (xfer_cancel) {
    punter_fail("Cancelled");
    return 0;
  }

  /*
   * Final SYN exchange (BBS lastblock sequence after last data block)
   * BBS: send S/B, wait SYN → send SYN, wait (short timeout)
   */

  menu_update_xfer_progress("Finishing...", sent_bytes, xfer_file_size);
  gfx_vbl();

  if (punter_wait_handshake("S/B", "SYN")) {
    punter_wait_handshake("SYN", "S/B");
    menu_update_xfer_progress("Upload complete", sent_bytes, xfer_file_size);
    gfx_vbl();
  } else {
    menu_update_xfer_progress("Done, but handshake timed out", sent_bytes, xfer_file_size);
    gfx_vbl();
  }


  return 1;
}

int punter_send(void) {
  return punter_send_internal(1);
}

int punter_send_no_presignal(void) {
  return punter_send_internal(0);
}
