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
int punter_send_filetype = 1;   /* Punter file type: 1 = PRG, 2 = SEQ, 3 = USR (set by xfer.c) */
int punter_batch_end_seen = 0;  /* set by punter_recv() when the board sent its end-of-batch marker */
static int punter_last_block = -1;  /* last data block number saved (duplicate guard) */
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


/* Send sendstring and scan the incoming stream for waitstring.
 *
 * This used to read fixed 3-byte groups and compare each group, so a single
 * stray byte from the board (status text around the transfer) misaligned
 * every following group: "xSY" "NSY" "NSY"... and the download died with
 * "Handshake timeout". Scanning byte by byte resyncs on its own, the same
 * way punter_wait_handshake() does for uploads. sendstring is repeated
 * whenever the line goes quiet for 3 s. */
int punter_handshake(char *sendstring, char *waitstring) {
  signed int c;
  int errorcnt = 20;
  int pos = 0;
  int scanned = 0;

  punter_send_string(sendstring);
  while (!xfer_cancel) {
    c = xfer_recv_byte(3000);
    if (c == -2) {
      return(0);                      /* disconnected */
    }
    if (c < 0) {
      if (--errorcnt <= 0) {
        return(0);
      }
      punter_send_string(sendstring);
      pos = 0;
      continue;
    }
    if ((char)c == waitstring[pos]) {
      if (++pos == 3) {
        return(1);
      }
    } else {
      pos = ((char)c == waitstring[0]) ? 1 : 0;
    }
    if (++scanned > 4096) {
      return(0);                      /* endless junk: give up */
    }
  }
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


/* Receive one Punter block of 'len' bytes (header included). 'save' is 0 for
 * the two protocol setup blocks (filetype, first header) and 1 for data
 * blocks; data blocks of any length (even 8 bytes = 1 data byte) are saved. */
signed int punter_recv_block(int len, int save) {
  signed int c;
  int bytecnt;
  int errorcnt = 10;

  /* Clamp peer-supplied block size to buffer capacity to prevent overflow.
   * Real Punter blocks are ~254 bytes; anything larger is malicious. */
  if (len < 0 || len > XFER_BUFFER_SIZE) {
    return -1;
  }

 restart:
  //printf("punter_recv_block: receiving %d byte block\n", len);
  punter_send_string("S/B");
  bytecnt = 0;
  while (bytecnt < len) {
    if (xfer_cancel) {
      return(-1);   /* user pressed ESC — abort promptly */
    }
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
	  if (--errorcnt <= 0) return(-1);
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
	if (--errorcnt <= 0) return(-1);
	goto restart;
      }
      if (strncmp("CKACKA", (const char *)(xfer_buffer + 2), 6) == 0) {
	menu_update_xfer_progress("Lost sync, retrying...", xfer_saved_bytes, 0);
	gfx_vbl();
	//printf("punter_recv_block: lost sync, restarting block\n");
	if (--errorcnt <= 0) return(-1);
	goto restart;
      }
      if (strncmp("KACKAC", (const char *)(xfer_buffer + 2), 6) == 0) {
	menu_update_xfer_progress("Lost sync, retrying...", xfer_saved_bytes, 0);
	gfx_vbl();
	//printf("punter_recv_block: lost sync, restarting block\n");
	if (--errorcnt <= 0) return(-1);
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
    if (!save || len <= 7) {
      /* setup block, or a header-only block: nothing to store */
      return(xfer_buffer[4]);
    }
    {
      int blk = punter_next_blocknum();
      if (blk < 0xff00 && blk == punter_last_block) {
        /* The sender missed our GOO/ACK and re-sent the block we already
         * stored (e.g. we sat in the ESC confirmation prompt). Acknowledge
         * it again but don't append the data twice. */
        return(xfer_buffer[4]);
      }
      punter_last_block = blk;
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
  punter_batch_end_seen = 0;

  /*
   * Initial handshake: send "GOO" repeatedly while consuming incoming
   * bytes (BBS status screen), scanning for "ACK" in the stream.
   */
  {
    int got_ack = 0;
    int ackpos = 0;
    int eots = 0;
    signed int c;
    unsigned int last_goo_time = 0;
    unsigned int now;

    attempts = 30;
    punter_send_string("GOO");
    last_goo_time = timer_get_ticks();

    while (attempts > 0 && !xfer_cancel) {
      c = xfer_recv_byte(1000);
      if (c == -2) {
        punter_fail("Disconnected");
        return(0);
      }

      now = timer_get_ticks();
      if (now > last_goo_time + 2000) {
        punter_send_string("GOO");
        last_goo_time = now;
        attempts--;
      }

      if (c < 0) {
        continue;
      }

      /* Multi Punter: a board that refuses the announced file (no credits,
       * download limit) sends its end-of-batch marker, a run of $04, instead
       * of starting Punter. Without this we sent GOO for a full minute and
       * the letters landed in the board's prompt. */
      if (c == 0x04) {
        if (++eots >= 3) {
          punter_batch_end_seen = 1;
          punter_fail("Refused by the board");
          return(0);
        }
        continue;
      }
      eots = 0;

      /* running match, so "AACK" and "ACACK" are still seen */
      if (c == "ACK"[ackpos]) {
        if (++ackpos == 3) {
          got_ack = 1;
          break;
        }
      } else {
        ackpos = (c == 'A') ? 1 : 0;
      }
    }
    if (!got_ack) {
      punter_fail("Timed out");
      return(0);
    }
    punter_countdown(5);
  }

  punter_last_filetype = 0;
  punter_last_block = -1;

  nextblocksize = punter_recv_block(8, 0);
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

  nextblocksize = punter_recv_block(7, 0);
  if (nextblocksize < 0) {
    // error
    punter_fail("file start timeout");
    return(0);
  }

  while (!xfer_cancel && punter_next_blocknum() < 0xff00 && nextblocksize >= 7) {
    nextblocksize = punter_recv_block(nextblocksize, 1);
  }
  if (xfer_cancel) {
    /* a cooperating peer can otherwise stream valid short blocks forever and
     * keep the transfer un-cancellable; honor the user's ESC here */
    return(0);
  }
  if (nextblocksize < 0) {
    //punter_fail("Block timeout");
    return(0);
  }

  menu_update_xfer_progress("Finishing...", xfer_saved_bytes, 0);
  gfx_vbl();

  if (punter_handshake("S/B", "SYN")) {
    punter_handshake("SYN", "S/B");
  }

  menu_update_xfer_progress("Transfer complete", xfer_saved_bytes, 0);
  gfx_vbl();
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
  signed int c;
  int errorcnt = 20;   /* quiet gaps tolerated (a board writing to a 1541 is slow) */
  int pos = 0;
  int scanned = 0;

  /* Scan the incoming stream byte-by-byte for waitstring, discarding stray
   * bytes and resynchronising on a mismatch — exactly like the download side's
   * ACK scan and punter_scan_for_string(). The previous version read rigid
   * 3-byte groups and compared them, so a single stray byte around the GOO/ACK
   * or SYN exchange (status-screen output, a prompt character, a late byte from
   * the prior block) misaligned every following group and failed the whole
   * upload. A real board emits such bytes; the clean scripted test and
   * single-byte XMODEM never do, which is why download and XMODEM worked while
   * Punter / Multi Punter uploads did not. Byte scanning also subsumes the old
   * rotated-ACK (CKA/KAC) special case: "ACK" is matched wherever it appears. */
  while (!xfer_cancel) {
    c = xfer_recv_byte(3000);
    if (c == -2) {
      return 0;          /* disconnected */
    }
    if (c < 0) {
      if (--errorcnt <= 0) {
        return 0;        /* ~60 s of silence: give up */
      }
      pos = 0;           /* a real gap resets any partial match */
      continue;
    }
    if ((char)c == waitstring[pos]) {
      if (++pos == 3) {
        punter_send_string(sendstring);
        return 1;
      }
    } else {
      pos = ((char)c == waitstring[0]) ? 1 : 0;
    }
    if (++scanned > 4096) {
      return 0;          /* bound a junk flood so this can't spin forever */
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
/* Returns 1 when found, 0 on timeout / too much unrelated data, -1 on
 * disconnect. */
static int punter_scan_for_string(const char *target, int timeout_ms, int max_bytes) {
  signed int c;
  int pos = 0;
  int total = 0;

  while (total < max_bytes && !xfer_cancel) {
    c = xfer_recv_byte(timeout_ms);
    if (c == -2) {
      return -1;
    }
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
    /* Wait for S/B from receiver — scan stream for pattern. A C64 BBS that
     * writes the previous block to a real 1541 can stay silent for well
     * over 3 s, so allow up to 20 timeouts (~60 s, like the receive side)
     * instead of failing the whole upload on the first quiet spell. */
    {
      int r = 0, waits = 0;
      while (!xfer_cancel) {
        r = punter_scan_for_string("S/B", 3000, 256);
        if (r != 0 || ++waits >= 20) {
          break;
        }
        punter_retry("Waiting for receiver...");
      }
      if (r < 0) {
        punter_fail("Disconnected");
        return 0;
      }
      if (r != 1) {
        punter_fail("Timeout waiting for S/B");
        return 0;
      }
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

      int quiet = 0;

      memset(resp, 0, 4);
      while (total < 64 && !xfer_cancel) {
        c = xfer_recv_byte(3000);
        if (c == -2) {
          punter_fail("Disconnected");
          return 0;
        }
        if (c < 0) {
          /* The board answers GOO/BAD only after it has written the block to
           * disk and repeats it for about 23 s. Giving up after one quiet 3 s
           * read sent us back to waiting for S/B, so a late GOO was never
           * ACKed and the board aborted the upload. Wait as long as the board
           * keeps trying. */
          if (++quiet >= 9) {
            break;
          }
          punter_retry("Waiting for receiver...");
          continue;
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

  /* Now BBS enters initrecv2 → recvblk → sends GOO, waits ACK */

  menu_update_xfer_progress("Waiting for BBS...", 0, xfer_file_size);
  gfx_vbl();

  if (send_presignal) {
    /* Send GOO to signal the BBS we're ready to upload, and repeat it every
     * 3 s until the board says anything: a single GOO that got lost left
     * both sides waiting for each other until they timed out. The board
     * leaves its wait loop on the first 'G', so repeats are harmless, and
     * we stop repeating as soon as it starts talking. */
    signed int c;
    int pos = 0, tries = 20, heard = 0, scanned = 0, ok = 0;

    punter_send_string("GOO");
    while (!xfer_cancel) {
      c = xfer_recv_byte(3000);
      if (c == -2) {
        break;
      }
      if (c < 0) {
        if (--tries <= 0) {
          break;
        }
        if (!heard) {
          punter_send_string("GOO");
        }
        pos = 0;
        continue;
      }
      heard = 1;
      if ((char)c == "GOO"[pos]) {
        if (++pos == 3) {
          punter_send_string("ACK");
          ok = 1;
          break;
        }
      } else {
        pos = ((char)c == 'G') ? 1 : 0;
      }
      if (++scanned > 4096) {
        break;
      }
    }
    if (!ok) {
      punter_fail("Initial handshake failed");
      return 0;
    }
  } else if (!punter_wait_handshake("GOO", "ACK")) {
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
  /* Punter's file type byte is NOT the CBM DOS type byte: 1 = PRG, 2 = SEQ
   * (Zimodem, C*Base). 3.0 sent 2 for everything and every upload landed on
   * the boards as a SEQ file nobody could download. */
  xfer_buffer[7] = (unsigned char)punter_send_filetype;
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
    if (next_len == 0) {
      /* Empty file: there is no data block, so this header must already be
       * the terminating ($ffff) block or the receiver waits for data that
       * never comes. */
      xfer_buffer[4] = 0;
      xfer_buffer[5] = 0xff;
      xfer_buffer[6] = 0xff;
    } else {
      xfer_buffer[4] = (unsigned char)next_total;
      xfer_buffer[5] = 0;
      xfer_buffer[6] = 0;
    }
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
