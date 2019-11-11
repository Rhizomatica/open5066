/* dts.c  -  NATO STANAG 5066 Annex C Processing
 * Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
 * See file COPYING.
 *
 * 15.4.2006, started work over Easter holiday --Sampo
 * 22.4.2006, completed work for NONARQ over weekend --Sampo
 */

#include "afr.h"
#include "hiios.h"
#include "errmac.h"
#include "s5066.h"
#include "sis5066.h"    /* from libnc3a, see COPYING_sis5066_h */

#include <ctype.h>
#include <memory.h>
#include <netinet/in.h> /* htons(3) and friends */

#define DTS_MIN_PDU_SIZE 6     /* sync + d_type + EOW + length fields */
#define DTS_MAX_PDU_SIZE 4096

/* Macro for accessing specific header bytes. */
#define DTS_SHB(r, addr_size, ix) ((r)->m[DTS_MIN_PDU_SIZE + (addr_size) + (ix)])
#define DTS_SEG_C_PDU_SIZE(r, addr_size) ((DTS_SHB((r), (addr_size), 0) & 0x03) << 8 \
                                         | DTS_SHB((r), (addr_size), 1) & 0x00ff)

/* From S5066 specification, Annex C, paragraph C.3.2.8, p. C-13. Not optimized. */

unsigned short CRC_16_S5066(unsigned char DATA, unsigned short CRC)
{
  unsigned char i, bit;
  for (i=0x01; i; i<<=1) {
    bit = ( ((CRC & 0x0001) ? 1:0) ^ ((DATA & i) ? 1:0) );
    CRC >>= 1;
    if (bit)
      CRC ^= 0x9299;
  }
  return CRC;
}

unsigned short CRC_16_S5066_batch(char* p, char* lim)
{
  unsigned short CRC = 0;
  for (; p < lim; ++p)
    CRC = CRC_16_S5066((unsigned char)*p,  CRC);
  return CRC;
}

unsigned int CRC_32_S5066(unsigned char DATA, unsigned int CRC)
{
  unsigned char i, bit;
  for (i=0x01; i; i<<=1) {
    bit = ( ((CRC & 0x0001) ? 1:0) ^ ((DATA & i) ? 1:0) );
    CRC >>= 1;
    if (bit)
      CRC ^= 0xf3a4e550;
  }
  return CRC;
}

unsigned int CRC_32_S5066_batch(char* p, char* lim)
{
  unsigned int CRC = 0;
  for (; p < lim; ++p)
    CRC = CRC_32_S5066((unsigned char)*p,  CRC);
  return CRC;
}

/* N.B. This code tries to keep the addresses intact, even if they are not
 * optimally encoded. Client should set the addresses in optimal way.
 * For station address matching we need some canonical representation,
 * rather than client supplied representation. */

int dts_enc_two_addr(char* addr, char* t, char* f)  /* returns length of addresses */
{
  int t_len = (t[0] >> 5) & 0x07;
  int f_len = (f[0] >> 5) & 0x07;
  int len = MAX(t_len, f_len);
  int i,j,k;
  
  for (i = 0; t_len + i < len; ++i)        /* Adjust for leading zeroes */
    SET_NIBBLE(addr, i, 0);
  
  for (j = 1; i < len; ++i, ++j)           /* Copy nibbles, avoiding the first, which is len */
    SET_NIBBLE(addr, i, GET_NIBBLE(t, j));
  
  for (k = i; f_len + i - k < len; ++i)    /* Adjust for leading zeroes */
    SET_NIBBLE(addr, i, 0);
  
  for (j = 1; i - k < len; ++i, ++j)       /* Copy nibbles, avoiding the first, which is len */
    SET_NIBBLE(addr, i, GET_NIBBLE(f, j));
  
  /* *** More efficient implementation may be possible by unrolling the loops
   * and having switch statements that simply deal with every special case. Since
   * only lengths from 1-7 are possible, such switch need not be huge. */
  return len;
}

/* Decode from packed, variable length, two address field, as seen on D_PDUs, to two unpacked
 * single address fields, as seen on SIS interface. */

void dts_dec_two_addr(int len, char* addr, char* t, char* f)
{
  int i,j;
  
  t[0] = (len << 5) & 0x00e0;
  for (j = 4, i = 0; i < len; ++i, ++j)    /* Copy nibbles, avoiding the first, which is len */
    SET_NIBBLE(t, j, GET_NIBBLE(addr, i));
  
  f[0] = (len << 5) & 0x00e0;
  for (j = 4; i < len+len; ++i, ++j)       /* Copy nibbles, avoiding the first, which is len */
    SET_NIBBLE(f, j, GET_NIBBLE(addr, i));
  
  /* *** More efficient implementation may be possible by unrolling the loops
   * and having switch statements that simply deal with every special case. Since
   * only lengths from 1-7 are possible, such switch need not be huge. */
}

/* ================== SENDING DTS PRIMITIVES ================== */

char my_station_addr[] = { 0xe1, 0x23, 0x45, 0x67 };

struct hi_pdu* dts_encode_start(struct hi_thr* hit, int op, int eow, char* to, int hdr_len)
{
  struct hi_pdu* resp = hi_pdu_alloc(hit);
  if (!resp) { NEVERNEVER("*** out of pdus in bad place %d", op); }
  resp->m[0] = 0x90;   /* Maury-Styles */
  resp->m[1] = 0xeb;
  resp->m[2] = (op << 4) & 0xf0 | (eow >> 8) & 0x0f;
  resp->m[3] = eow & 0x00ff;
  resp->m[4] = 0;  /* EOT will be sent just before writev(2) at lower layer */
  resp->ad.dts.addr_len = dts_enc_two_addr(resp->m + 6, to, my_station_addr);
  resp->m[5] = resp->ad.dts.addr_len << 5 | hdr_len & 0x001f;

  resp->len = 2 /* preamble */ + hdr_len + resp->ad.dts.addr_len + 2 /* crc16 */ ;
  resp->ap += resp->len;
  return resp;
}

void dts_send_uni_final(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, struct hi_pdu* resp, int seg_size, char* p)
{
  unsigned int data_crc32;
  /* Grab memory for  data CRC right after the header */
  data_crc32 = CRC_32_S5066_batch(p, p + seg_size);
  resp->ap[0] = (data_crc32 >> 24) & 0x00ff;
  resp->ap[1] = (data_crc32 >> 16) & 0x00ff;
  resp->ap[2] = (data_crc32 >> 8) & 0x00ff;
  resp->ap[3] = data_crc32 & 0x00ff;
  resp->ap += 4;
  hi_send3(hit, io, req, resp, resp->len, resp->m, seg_size, p, 4, resp->m + resp->len);
}

void dts_send_uni_nonarq_seg(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d, int seg_size, char* p)
{
  struct hi_pdu* resp;
  unsigned short hdr_crc16;
  char* h;

  resp = dts_encode_start(hit, DTS_NONARQ, 0, req->m + 7, DTS_MIN_PDU_SIZE - 2 + 9);
  h = resp->m + DTS_MIN_PDU_SIZE + resp->ad.dts.addr_len;
  h[0] = (io->ad.dts->c_pdu_id >> 4) & 0x00f0 | (seg_size >> 8) & 0x03;
  h[1] = seg_size & 0x00ff;
  h[2] = io->ad.dts->c_pdu_id & 0x00ff;
  
  h[3] = (len >> 8) & 0x00ff;   /* C_PDU overall size */
  h[4] = len & 0x00ff;
  
  h[5] = ((p-d) >> 8) & 0x00ff; /* C_PDU segment offset */
  h[6] = (p-d) & 0x00ff;
  
  h[7] = 0;   /* C_PDU reception window will be set at low */
  h[8] = 0;   /* level in manner similar to EOT */
  
  hdr_crc16 = CRC_16_S5066_batch(resp->m + 2, h + 9);
  h[9] = (hdr_crc16 >> 8) & 0x00ff;
  h[10] = hdr_crc16 & 0x00ff;
  ASSERTOP(h+11, ==, resp->ap);
  
  dts_send_uni_final(hit, io, req, resp, seg_size, p);
}

void dts_send_uni_nonarq(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d)
{
  char* lim = d + len;
  char* p = d;
  
  /* Segment the c_pdu and prepare and send a d_pdu for every segment. */
  
  for (; lim-p > DTS_SEG_SIZE; p += DTS_SEG_SIZE)
    dts_send_uni_nonarq_seg(hit, io, req, len, d, DTS_SEG_SIZE, p);
  dts_send_uni_nonarq_seg(hit, io, req, len, d, lim-p, p);   /* Last segment */
}

void dts_send_uni_arq_seg(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int seg_size, char* p, int flags, int n_tx_seq)
{
  struct hi_pdu* resp;
  unsigned short hdr_crc16;
  int i,j;
  char* h;
  
  if (!(flags & 0x80)                                    /* latter parts of multisegment, no ACKs */
      || (io->ad.dts->rx_lwe == io->ad.dts->rx_lwe)) {   /* rx window is empty, no ACKs needed */
    resp = dts_encode_start(hit, DTS_DATA_ONLY, 0, req->m + 7, 4+4+2);
    h = resp->m + 6 + resp->ad.dts.addr_len;
    io->ad.dts->tx_pdus[n_tx_seq & 0x00ff] = resp;
    h[0] = flags | (seg_size >> 8) & 0x03
      | ((n_tx_seq == io->ad.dts->tx_uwe) ? 0x80 : 0)
      | ((n_tx_seq == io->ad.dts->tx_lwe) ? 0x40 : 0);
    h[1] = seg_size & 0x00ff;
    h[2] = n_tx_seq & 0x00ff;
    
    hdr_crc16 = CRC_16_S5066_batch(resp->m + 2, h + 3);
    h[3] = (hdr_crc16 >> 8) & 0x00ff;
    h[4] = hdr_crc16 & 0x00ff;
    ASSERTOP(h+5, ==, resp->ap);
  } else {
    /* *** Ideally the ack computation should not happen yet. Essentially we should
     * add the ACK payload just before we pump the PDU in transit. Especially for
     * latter segments of segmented C_PDU we probably should not be sending the ACKs */
    int ack_len = (io->ad.dts->rx_uwe - io->ad.dts->rx_lwe) / 8;
    resp = dts_encode_start(hit, DTS_DATA_ACK, 0, req->m + 7, 4+3+ack_len+2);
    h = resp->m + 6 + resp->ad.dts.addr_len;
    io->ad.dts->tx_pdus[n_tx_seq & 0x00ff] = resp;
    h[0] = flags | (seg_size >> 8) & 0x03
      | ((n_tx_seq == io->ad.dts->tx_uwe) ? 0x80 : 0)
      | ((n_tx_seq == io->ad.dts->tx_lwe) ? 0x40 : 0);
    h[1] = seg_size & 0x00ff;
    h[2] = n_tx_seq & 0x00ff;
    h[3] = io->ad.dts->rx_lwe & 0x00ff;
    
    memset(h + 4, 0, ack_len);
    for (i = 0, j = io->ad.dts->rx_lwe + 1; j <= io->ad.dts->rx_uwe; ++j, ++i)
      if (GET_BIT(io->ad.dts->acks, j & 0x00ff))
	SET_BIT(h + 4, i, 1);
    
    hdr_crc16 = CRC_16_S5066_batch(resp->m + 2, h + 4 + ack_len);
    h[4 + ack_len] = (hdr_crc16 >> 8) & 0x00ff;
    h[5 + ack_len] = hdr_crc16 & 0x00ff;
    ASSERTOP(h+5+ack_len, ==, resp->ap);
  }
  dts_send_uni_final(hit, io, req, resp, seg_size, p);
}

int dts_expand_tx_window(struct hi_io* io)
{
  int n_tx_seq;
  /* if (tx_window full) return -1; */
  n_tx_seq = ++io->ad.dts->tx_uwe;
  return n_tx_seq;
}

void dts_send_uni_arq(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d)
{
  int n_tx_seq;
  char* lim = d + len;
  char* p = d;
  
  /* Segment the c_pdu and prepare and send a d_pdu for every segment. */

  n_tx_seq = dts_expand_tx_window(io); /* *** race from alloc of n_tx_seq to populating tx_pdus */
  if (n_tx_seq == -1) {
    D("TX_WIN_FULL tx_lwe(%d) tx_uwe(%d)", io->ad.dts->tx_lwe, io->ad.dts->tx_uwe);
    return; /* *** add here proper handling of the situation */
  }
  dts_send_uni_arq_seg(hit, io, req, DTS_SEG_SIZE, p, 0x80, n_tx_seq); /* 1st seg */
  
  for (p += DTS_SEG_SIZE; lim-p > DTS_SEG_SIZE; p += DTS_SEG_SIZE) {
    n_tx_seq = dts_expand_tx_window(io); /* *** race from alloc of n_tx_seq to populating tx_pdus */
    if (n_tx_seq == -1) {
      D("TX_WIN_FULL tx_lwe(%d) tx_uwe(%d)", io->ad.dts->tx_lwe, io->ad.dts->tx_uwe);
      return; /* *** add here proper handling of the situation */
    }
    dts_send_uni_arq_seg(hit, io, req, DTS_SEG_SIZE, p, 0x00, n_tx_seq);
  }
  
  n_tx_seq = dts_expand_tx_window(io); /* *** race from alloc of n_tx_seq to populating tx_pdus */
  if (n_tx_seq == -1) {
    D("TX_WIN_FULL tx_lwe(%d) tx_uwe(%d)", io->ad.dts->tx_lwe, io->ad.dts->tx_uwe);
    return; /* *** add here proper handling of the situation */
  }
  dts_send_uni_arq_seg(hit, io, req, lim-p, p, 0x40, n_tx_seq);   /* Last segment */
}

/* N.B. len and d MUST reflect a U_PDU, not a S_PDU and there must be 6 bytes of free space
 * available before d so C_PCI and S_PDU header can be added (at negative offsets). */

void dts_send_uni(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req, int len, char* d)
{
  int priority   = (d[-11] >> 4) & 0x0f;
  int dest_sap   = /*req->fe->ad.sap*/ d[-11] & 0x0f;  /* The two saps really should be same */
  int tx_mode    = (d[-6] >> 4) & 0x0f;
  int flags      = d[-6] & 0x0f;
  int n_re_tx    = (d[-5] >> 4) & 0x0f;
  int ttl = ((d[-5] & 0x0f) << 16) | (d[-4] << 8) | d[-3];
  
  HEXDUMP("dts_send_uni: ", d, d+len, 800);  /* Should be HMTP payload */
  
  /* *** perform soft link establishment, if needed (effectively a subrequest) */
  
  ++io->ad.dts->c_pdu_id;
  
  /* Prepare C_PCI and S_PDU header */
  
  if (!ttl) {
    d[-4] = C_PDU_DATA; /* C_PCI */
    d[-3] = S_PDU_DATA | priority;
    d[-2] = (req->fe->ad.sap << 4) & 0xf0 | dest_sap;
    d[-1] = 0x00;  /* no valid TTD, no delivery confirmation */
    d -= 4;
    len += 4;
  } else {
    int ttd;
    d[-6] = C_PDU_DATA; /* C_PCI */
    d[-5] = S_PDU_DATA | priority;
    d[-4] = (req->fe->ad.sap << 4) & 0xf0 | dest_sap;
    ttd = time(0) + ttl;  /* *** not the real algorithm, see p. A-53 for confusing description */
    d[-3] = 0x40 | (ttd >> 16) & 0x0f;
    d[-2] = (ttd >> 8) & 0xff;
    d[-1] = ttd & 0xff;
    d -= 6;
    len += 6;
  }
    
  /* Choose transmission mode */
  
  if (!tx_mode) {
    tx_mode = saptab[req->fe->ad.sap].tx_mode;
    flags   = saptab[req->fe->ad.sap].flags;
    n_re_tx = saptab[req->fe->ad.sap].n_re_tx;
  }
  
  switch (tx_mode) {
  case 1:
    dts_send_uni_arq(hit, io, req, len, d);
    break;
  default:
    D("Other nonarq tx_mode(%d)", tx_mode);
    /* fall thru */
  case 2:    
    dts_send_uni_nonarq(hit, io, req, len, d);     /* always at least once */
    for (--n_re_tx; n_re_tx > 0; --n_re_tx)
      dts_send_uni_nonarq(hit, io, req, len, d);
  }
}

/* ================== DECODING DTS PRIMITIVES ================== */

/* Deal with data received from the pipe. Essentially we see segmented
 * c_pdus that need to be assembled and once complete, delivered
 * to the right SIS SAP. */

int dts_data(struct hi_thr* hit, struct hi_pdu* req, int addr_size)
{
  struct hi_io* io;
  int i, c_pdu_id, c_pdu_size, c_pdu_offset, c_pdu_rx_win, u_len, sap;
  int d_type = (req->m[2] >> 4 & 0x0f);
  int seg_size = DTS_SEG_C_PDU_SIZE(req, addr_size);
  struct hi_pdu* pdu;
  char* c_pdu;
  char* u_pdu;
  char* h;
  
  switch (d_type) {
  case DTS_DATA_ONLY:  /* 0 */
    D("DTS_DATA_ONLY seg_c_pdu_size(%d) flags(%x) tx_seq(%x)", seg_size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2));
    return 0;
  case DTS_DATA_ACK:   /* 2 */
    D("DTS_DATA_ACK seg_c_pdu_size(%d) flags(%x) tx_seq(%x) rx_lwe(%d)", seg_size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2), DTS_SHB(req, addr_size, 3));
    return 0;
  case DTS_EDATA_ONLY: /* 4 */
    D("DTS_EDATA_ONLY seg_c_pdu_size(%d) flags(%x) tx_seq(%x)", seg_size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2));
    return 0;
  case DTS_NONARQ:     /* 7 */
    c_pdu_id = DTS_SHB(req, addr_size, 2);
    c_pdu_size   = (DTS_SHB(req, addr_size, 3) << 8) & 0xff00 | DTS_SHB(req, addr_size, 4) & 0x0ff;
    c_pdu_offset = (DTS_SHB(req, addr_size, 5) << 8) & 0xff00 | DTS_SHB(req, addr_size, 6) & 0x0ff;
    c_pdu_rx_win = (DTS_SHB(req, addr_size, 7) << 8) & 0xff00 | DTS_SHB(req, addr_size, 8) & 0x0ff;
    D("DTS_NONARQ seg_c_pdu_size(0x%x) flags(%x) c_pdu_id(%x) c_pdu_size(0x%x) c_pdu_seg_offset(0x%x) c_pdu_rx_win(0x%x)",
      seg_size, DTS_SHB(req, addr_size, 0), c_pdu_id, c_pdu_size, c_pdu_offset, c_pdu_rx_win);
    
    /* Need to assemble a complete C_PDU before we can pass off to upper layer. This may
     * take time as segments may (a) arrive out of order, (b) arrive over several
     * repeatitions, (c) arrive errornous or not at all. */
    
    pdu = req->fe->ad.dts->nonarq_pdus[c_pdu_id];
    if (!pdu) {
      pdu = hi_pdu_alloc(hit);
      req->fe->ad.dts->nonarq_pdus[c_pdu_id] = pdu;
      pdu->len = c_pdu_size;
      memset(pdu->ad.dtsrx.rx_map, 0, sizeof(pdu->ad.dtsrx.rx_map));
    } else {
      if (pdu->len != c_pdu_size) {
	D("INSANITY c_pdu_id(%x) size mismatch: orig_len(%x) got c_pdu_size(%x)", c_pdu_id, pdu->len, c_pdu_size);
	return 0;
      }
    }
    
    if (c_pdu_offset + seg_size > pdu->len) {
      D("INSANITY: c_pdu_offset(%d) + seg_size(%d) exceed pdu->len(%d)", c_pdu_offset, seg_size, pdu->len);
      return 0;
    }
    
    /* Copy data to its place and color the map to indicate it was received. Data needs to
     * be copied at right offset so that when converted to U_PDU over SIS interface,
     * it will be in the right place, i.e. offset 19. The trick is to understand the
     * size of S_PDU headers. This may be complicated because we may not receive
     * the first segment first. The variable component of S_PDU is the TTD field.
     * For now we simply assume the TTD is there and only adjust in the end, if not.
     * Without TTD C_PDU+S_PDU header will take 4 bytes (would take 6 with TTD).
     * For time being we do not support S_UNIDATA_INDICATION with errored and
     * non-rd'd blocks descriptions (these would add yet another, potentially
     * large, variable component to the header). */
    
    c_pdu = pdu->m + SIS_UNIDATA_IND_MIN_HDR - 4;
    memcpy(c_pdu + c_pdu_offset,  req->m + 6 + addr_size + 9 + 2,  seg_size);
    for (i = c_pdu_offset; i < c_pdu_offset + seg_size; ++i)
      SET_BIT(pdu->ad.dtsrx.rx_map, i, 1);
    
    /* Scan the map to see if C_PDU has been completely received */
    
    for (i = 0; i < pdu->len; ++i)
      if (!GET_BIT(pdu->ad.dtsrx.rx_map, i)) {
	D("PDU incomplete len=%d", pdu->len);
	HEXDUMP("rx_map: ", pdu->ad.dtsrx.rx_map, pdu->ad.dtsrx.rx_map + (pdu->len >> 3) + 1, 32);
	return 0; /* PDU still incomplete */
      }
    /* Hurrah! PDU is compete. Ship it to the SIS layer. First formulate SIS headers. */
    HEXDUMP("C_PDU: ", c_pdu, c_pdu + c_pdu_size, 500);
    
    sap = c_pdu[2] & 0x0f; /* destination SAP ID */
    if (c_pdu[3] & 0x40) { /* TTD is present */
      h = pdu->m + 2;
      u_len = pdu->len - 6;
      u_pdu = c_pdu + 6;
    } else {  /* No TTD, need to shift the layout */
      h = pdu->m;
      u_len = pdu->len - 4;
      u_pdu = c_pdu + 4;
    }
    
    h[0] = 0x90;  /* Maury-Styles */
    h[1] = 0xeb;
    h[2] = 0x00;  /* Version */
    h[3] = ((SIS_UNIDATA_IND_MIN_HDR - 5 + u_len) >> 8) & 0x00ff;
    h[4] = (SIS_UNIDATA_IND_MIN_HDR - 5 + u_len) & 0x00ff;
    h[5] = S_UNIDATA_INDICATION;
    h[6] = (c_pdu[1] << 4) & 0xf0 | c_pdu[2] & 0x0f;  /* PRIO and DEST SAP ID */
    dts_dec_two_addr(addr_size, req->m + 6, h+7, h+12);
    h[11] = 0x00 | (c_pdu[2] >> 4) & 0x0f;  /* TX Mode and SRC SAP ID */
    h[16] = (u_len >> 8) & 0x00ff;
    h[17] = u_len & 0x00ff;
    h[18] = h[19] = 0; /* Number of Errored Blocks (none) */
    h[20] = h[21] = 0; /* Number of Non Received Blocks (none) */
  
    LOCK(saptab_mut, "deliver to sis");
    io = saptab[sap].io;
    UNLOCK(saptab_mut, "deliver to sis");
    if (io) {
      D("deliver UNIDATA_IND from DTS sap(%d) to sis fd(%x) u_len=%d", sap, io->fd, u_len);
      hi_send1(hit, io, 0, pdu, SIS_UNIDATA_IND_MIN_HDR + u_len, h);
    } else {
      ERR("Can not deliver UNIDATA_IND from DTS: No SIS client bound with sapid(%d)", sap);
    }
    return 0;
    
  case DTS_ENONARQ:    /* 8 */
    D("DTS_ENONARQ seg_c_pdu_size(%d) flags(%x) c_pdu_id(%x) c_pdu_size(0x%02x%02x) c_pdu_seg_offset(0x%02x%02x) c_pdu_rx_win(0x%02x%02x)",
      seg_size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2),
      DTS_SHB(req, addr_size, 3), DTS_SHB(req, addr_size, 4),
      DTS_SHB(req, addr_size, 5), DTS_SHB(req, addr_size, 6),
      DTS_SHB(req, addr_size, 7), DTS_SHB(req, addr_size, 8));
    return 0;
  default:            /* 9-14 reserved */
    NEVERNEVER("bad d_type(%x)", d_type);
  }
  return 0;
}

static int dts_process_hdr(struct hi_thr* hit, struct hi_io* io, struct hi_pdu* req,
			   int addr_size, int hdr_size)
{
  int size;
  int d_type = (req->m[2] >> 4 & 0x0f);
  int eow = (req->m[2] << 8) | req->m[3];
  int eot = req->m[4];
  switch (d_type) {
  case DTS_DATA_ONLY:  /* 0 */
    if (hdr_size != (DTS_MIN_PDU_SIZE + 3 - 2)) goto bad;
    size = DTS_SEG_C_PDU_SIZE(req, addr_size);
    D("DTS_DATA_ONLY seg_c_pdu_size(%x) flags(%x) tx_seq(%x)", size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2));
    return size;
  case DTS_ACK_ONLY:   /* 1 */
    if (hdr_size < (DTS_MIN_PDU_SIZE + 1 - 2)) goto bad;
    D("DTS_ACK_ONLY rx_lwe(%x)", DTS_SHB(req, addr_size, 0));
    return -1;
  case DTS_DATA_ACK:   /* 2 */
    if (hdr_size < (DTS_MIN_PDU_SIZE + 4 - 2)) goto bad;
    size = DTS_SEG_C_PDU_SIZE(req, addr_size);
    D("DTS_DATA_ACK seg_c_pdu_size(%x) flags(%x) tx_seq(%x) rx_lwe(%d)", size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2), DTS_SHB(req, addr_size, 3));
    return size;
  case DTS_RESET:      /* 3 */
    if (hdr_size != (DTS_MIN_PDU_SIZE + 3 - 2)) goto bad;
    D("DTS_RESET flags(%x) rx_lwe(%x) reset_frid(%x)", DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 1), DTS_SHB(req, addr_size, 2));
    return -1;
  case DTS_EDATA_ONLY: /* 4 */
    if (hdr_size != (DTS_MIN_PDU_SIZE + 3 - 2)) goto bad;
    size = DTS_SEG_C_PDU_SIZE(req, addr_size);
    D("DTS_EDATA_ONLY seg_c_pdu_size(%x) flags(%x) tx_seq(%x)", size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2));
    return size;
  case DTS_EACK_ONLY:  /* 5 */
    if (hdr_size < (DTS_MIN_PDU_SIZE + 1 - 2)) goto bad;
    D("DTS_EACK_ONLY rx_lwe(%x)", DTS_SHB(req, addr_size, 0));
    return -1;
  case DTS_MGMT:       /* 6 */
    if (hdr_size < (DTS_MIN_PDU_SIZE + 2 - 2)) goto bad;
    D("DTS_MGMT flags(%x) mgmt_frid(%x)", DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 1));
    return -1;
  case DTS_NONARQ:     /* 7 */
    if (hdr_size < (DTS_MIN_PDU_SIZE - 2 + 9)) goto bad;   /* 6 - 2 + 9 == 13 */  
    size = DTS_SEG_C_PDU_SIZE(req, addr_size);
    D("DTS_NONARQ seg_c_pdu_size(%x) flags(%x) c_pdu_id(%x) c_pdu_size(0x%02x%02x) c_pdu_seg_offset(0x%02x%02x) c_pdu_rx_win(0x%02x%02x)",
      size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2),
      DTS_SHB(req, addr_size, 3), DTS_SHB(req, addr_size, 4),
      DTS_SHB(req, addr_size, 5), DTS_SHB(req, addr_size, 6),
      DTS_SHB(req, addr_size, 7), DTS_SHB(req, addr_size, 8));
    return size;
  case DTS_ENONARQ:    /* 8 */
    if (hdr_size != (DTS_MIN_PDU_SIZE + 9 - 2)) goto bad;
    size = DTS_SEG_C_PDU_SIZE(req, addr_size);
    D("DTS_ENONARQ seg_c_pdu_size(%x) flags(%x) c_pdu_id(%x) c_pdu_size(0x%02x%02x) c_pdu_seg_offset(0x%02x%02x) c_pdu_rx_win(0x%02x%02x)",
      size, DTS_SHB(req, addr_size, 0), DTS_SHB(req, addr_size, 2),
      DTS_SHB(req, addr_size, 3), DTS_SHB(req, addr_size, 4),
      DTS_SHB(req, addr_size, 5), DTS_SHB(req, addr_size, 6),
      DTS_SHB(req, addr_size, 7), DTS_SHB(req, addr_size, 8));
    return size;
  case DTS_WARNING:   /* 15 */
    if (hdr_size != (DTS_MIN_PDU_SIZE + 1 - 2)) goto bad;
    D("DTS warning rx_frame_type+reason(%x)", DTS_SHB(req, addr_size, 0));
    return -1;
  default:            /* 9-14 reserved */
    D("reserved(%x)", d_type);
    return -1;
  }
 bad:
  ERR("Bad DTS PDU. Wrong hdr_size(%d) d_type(%x)", hdr_size, d_type);
  return -1;
}

int dts_decode(struct hi_thr* hit, struct hi_io* io)
{
  int addr_size, hdr_size, seg_c_pdu_size;
  unsigned short hdr_crc16;
  unsigned int data_crc32;
  unsigned char* p_crc;
  struct hi_pdu* req = io->cur_pdu;
  int n = req->ap - req->m;
  
  if (n < DTS_MIN_PDU_SIZE) {   /* too little, need more */
    req->need = DTS_MIN_PDU_SIZE - n;
    return 0;
  }
  
  if (req->m[0] != (char)0x90 || req->m[1] != (char)0xeb) { /* 16 bit Maury-Styles */
    ERR("Bad DTS PDU. fd(%x) need 0x90eb preamble", io->fd);
    HEXDUMP("bad preamble: ", req->m, req->m + DTS_MIN_PDU_SIZE, 50);
    /* *** Change this to scan for occurance of Maury-Styles and discard any junk before it,
     * afterall, we are expecting an errorful channel. */
    return HI_CONN_CLOSE;
  }
  
  addr_size = (req->m[5] >> 5) & 0x07;
  hdr_size = req->m[5] & 0x1f;
  req->len = 2 + addr_size + hdr_size;
  if (n < req->len) {                    /* Need more to complete header */
    req->need = req->len - n;
    return 0;
  }
  
  p_crc = (unsigned char*)(req->m + 2 + hdr_size + addr_size);
  hdr_crc16 = CRC_16_S5066_batch(req->m + 2, p_crc);
  if (p_crc[0] != ((hdr_crc16 >> 8) & 0x00ff) || p_crc[1] != (hdr_crc16 & 0x00ff)) {
    ERR("Bad DTS PDU. fd(%x) op(%x) header CRC check failed: hdr_crc(0x%02x%02x) calculated(0x%04x) hdr_size=%d addr_size=%d",
	io->fd, req->m[2], p_crc[0], p_crc[1], hdr_crc16, hdr_size, addr_size);
    /* *** Change this to scan for occurance of Maury-Styles and discard any junk before it,
     * afterall, we are expecting an errorful channel. */
    return HI_CONN_CLOSE;
  }
  
  seg_c_pdu_size = dts_process_hdr(hit, io, req, addr_size, hdr_size);
  if (seg_c_pdu_size == -1) {
    hi_checkmore(hit, io, req, DTS_MIN_PDU_SIZE);
    hi_free_req(hit, req);
    return 0;
  }
   
  req->len += seg_c_pdu_size + 4;
  if (n < req->len) {                    /* Need more to complete data */
    req->need = req->len - n;
    return 0;
  }
  
  req->ad.dts.c_pdu = p_crc + 2;
  p_crc = (unsigned char*)(req->ad.dts.c_pdu +  seg_c_pdu_size);
  data_crc32 = CRC_32_S5066_batch(req->ad.dts.c_pdu, p_crc);
  if (p_crc[0] != ((data_crc32 >> 24) & 0x00ff)
      || p_crc[1] != ((data_crc32 >> 16) & 0x00ff)
      || p_crc[2] != ((data_crc32 >> 8) & 0x00ff)
      || p_crc[3] != (data_crc32 & 0x00ff)) {
    ERR("Bad DTS PDU. fd(%x) op(%x) body CRC check failed: data_crc(0x%02x%02x%02x%02x) calculated(0x%08x)",
	io->fd, req->m[2], p_crc[0], p_crc[1], p_crc[2], p_crc[3], data_crc32);
    /* *** Need more graceful error handling afterall, we are expecting an errorful channel. */
    return HI_CONN_CLOSE;
  }
  
  hi_checkmore(hit, io, req, DTS_MIN_PDU_SIZE);
  hi_add_to_reqs(io, req);
  return dts_data(hit, req, addr_size);
}

/* EOF  --  dts.c */
