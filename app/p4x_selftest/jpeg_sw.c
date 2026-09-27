/****************************************************************************
 * app/p4x_selftest/jpeg_sw.c
 *
 * Self-contained baseline JPEG encoder (RGB565 -> JPEG, 4:4:4, no chroma
 * subsampling).  Uses the standard JPEG Annex K quantization and Huffman
 * tables and an orthonormal 8x8 DCT.  Pure C + libm; no peripheral access.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "jpeg_sw.h"
#include <string.h>
#include <math.h>

/* ---- Standard tables (JPEG Annex K) ------------------------------------ */

/* zigzag[k] = natural block index (row-major, v*8+u) of the k-th zigzag step */
static const int ZZ[64] =
{
   0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
  12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
  35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
  58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63
};

static const int LQT[64] =
{
  16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55,
  14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
  18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
  49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99
};

static const int CQT[64] =
{
  17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99,
  24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99
};

static const uint8_t DC_L_BITS[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t DC_L_VAL[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t DC_C_BITS[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t DC_C_VAL[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};

static const uint8_t AC_L_BITS[16] =
  {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t AC_L_VAL[162] =
{
  0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
  0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
  0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
  0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
  0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
  0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
  0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
  0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
  0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
  0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
  0xf9,0xfa
};

static const uint8_t AC_C_BITS[16] =
  {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const uint8_t AC_C_VAL[162] =
{
  0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
  0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
  0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
  0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
  0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
  0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
  0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
  0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
  0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
  0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
  0xf9,0xfa
};

/* ---- Huffman code build ------------------------------------------------ */

typedef struct
{
  uint16_t code[256];
  uint8_t  size[256];
} huff_t;

static void huff_build(huff_t *h, const uint8_t *bits, const uint8_t *val)
{
  int k = 0;
  uint16_t code = 0;
  memset(h, 0, sizeof(*h));
  for (int l = 1; l <= 16; l++)
    {
      for (int i = 0; i < bits[l - 1]; i++)
        {
          h->code[val[k]] = code;
          h->size[val[k]] = (uint8_t)l;
          code++;
          k++;
        }
      code <<= 1;
    }
}

/* ---- Bit / byte writer with 0xFF stuffing ------------------------------ */

typedef struct
{
  uint8_t *buf;
  int      cap;
  int      len;
  int      fail;
  uint32_t acc;
  int      nacc;
} wr_t;

static void put_byte(wr_t *w, uint8_t b)
{
  if (w->len >= w->cap)
    {
      w->fail = 1;
      return;
    }
  w->buf[w->len++] = b;
}

static void put_word(wr_t *w, uint16_t v)
{
  put_byte(w, (uint8_t)(v >> 8));
  put_byte(w, (uint8_t)(v & 0xff));
}

static void put_bits(wr_t *w, uint32_t val, int n)
{
  if (n <= 0)
    {
      return;
    }

  w->acc = (w->acc << n) | (val & ((1u << n) - 1));
  w->nacc += n;
  while (w->nacc >= 8)
    {
      w->nacc -= 8;
      uint8_t b = (uint8_t)((w->acc >> w->nacc) & 0xff);
      put_byte(w, b);
      if (b == 0xff)
        {
          put_byte(w, 0x00);            /* byte stuffing */
        }
    }
}

static void flush_bits(wr_t *w)
{
  while (w->nacc & 7)                    /* pad with 1s to byte boundary */
    {
      put_bits(w, 1, 1);
    }
}

/* ---- helpers ----------------------------------------------------------- */

static int category(int v)
{
  int a = v < 0 ? -v : v;
  int c = 0;
  while (a)
    {
      c++;
      a >>= 1;
    }
  return c;
}

/* Encode one 8x8 spatial block (already level-shifted floats) into the
 * entropy stream. quant is the natural-order scaled quant table.
 */
static void encode_block(wr_t *w, const float *blk, const int *quant,
                         const huff_t *dc, const huff_t *ac, const float M[8][8],
                         int *prev_dc)
{
  float row[64];
  float dct[64];
  int q[64];
  int i;
  int u;
  int v;
  int x;
  int y;

  /* 1D DCT along x for each row */
  for (y = 0; y < 8; y++)
    {
      for (u = 0; u < 8; u++)
        {
          float s = 0.0f;
          for (x = 0; x < 8; x++)
            {
              s += M[u][x] * blk[y * 8 + x];
            }
          row[y * 8 + u] = s;
        }
    }

  /* 1D DCT along y for each column -> dct[v*8+u] */
  for (u = 0; u < 8; u++)
    {
      for (v = 0; v < 8; v++)
        {
          float s = 0.0f;
          for (y = 0; y < 8; y++)
            {
              s += M[v][y] * row[y * 8 + u];
            }
          dct[v * 8 + u] = s;
        }
    }

  /* quantize (natural order) */
  for (i = 0; i < 64; i++)
    {
      float f = dct[i] / (float)quant[i];
      q[i] = (int)floorf(f + 0.5f);
    }

  /* DC: differential */
  {
    int diff = q[0] - *prev_dc;
    int c = category(diff);
    int m = diff < 0 ? (diff + (1 << c) - 1) : diff;
    *prev_dc = q[0];
    put_bits(w, dc->code[c], dc->size[c]);
    put_bits(w, (uint32_t)m, c);
  }

  /* AC: run-length + Huffman, zigzag order */
  {
    int run = 0;
    int k;
    for (k = 1; k < 64; k++)
      {
        int val = q[ZZ[k]];
        if (val == 0)
          {
            run++;
            continue;
          }
        while (run > 15)
          {
            put_bits(w, ac->code[0xf0], ac->size[0xf0]);   /* ZRL */
            run -= 16;
          }
        {
          int c = category(val);
          int m = val < 0 ? (val + (1 << c) - 1) : val;
          int sym = (run << 4) | c;
          put_bits(w, ac->code[sym], ac->size[sym]);
          put_bits(w, (uint32_t)m, c);
        }
        run = 0;
      }
    if (run > 0)
      {
        put_bits(w, ac->code[0x00], ac->size[0x00]);       /* EOB */
      }
  }
}

/* ---- main encoder ------------------------------------------------------ */

int jpeg_sw_encode_rgb565(const uint16_t *rgb565, int width, int height,
                          int quality, uint8_t *out, int out_cap)
{
  wr_t w;
  static huff_t hdc_l;
  static huff_t hac_l;
  static huff_t hdc_c;
  static huff_t hac_c;
  float M[8][8];
  int lq[64];
  int cq[64];
  int scale;
  int i;
  int mx;
  int my;
  int prev_dc_y = 0;
  int prev_dc_cb = 0;
  int prev_dc_cr = 0;

  if (rgb565 == NULL || out == NULL || width <= 0 || height <= 0)
    {
      return -1;
    }

  if (quality < 1)
    {
      quality = 1;
    }
  if (quality > 100)
    {
      quality = 100;
    }
  scale = quality < 50 ? (5000 / quality) : (200 - quality * 2);

  /* scaled quant tables (natural order) */
  for (i = 0; i < 64; i++)
    {
      int a = (LQT[i] * scale + 50) / 100;
      int b = (CQT[i] * scale + 50) / 100;
      lq[i] = a < 1 ? 1 : (a > 255 ? 255 : a);
      cq[i] = b < 1 ? 1 : (b > 255 ? 255 : b);
    }

  /* orthonormal DCT matrix M[k][n] */
  for (i = 0; i < 8; i++)
    {
      int n;
      float ck = (i == 0) ? 0.353553390593273762f : 0.5f;
      for (n = 0; n < 8; n++)
        {
          M[i][n] = ck * cosf((2.0f * n + 1.0f) * i * 3.14159265358979324f
                              / 16.0f);
        }
    }

  huff_build(&hdc_l, DC_L_BITS, DC_L_VAL);
  huff_build(&hac_l, AC_L_BITS, AC_L_VAL);
  huff_build(&hdc_c, DC_C_BITS, DC_C_VAL);
  huff_build(&hac_c, AC_C_BITS, AC_C_VAL);

  w.buf = out;
  w.cap = out_cap;
  w.len = 0;
  w.fail = 0;
  w.acc = 0;
  w.nacc = 0;

  /* ---- headers ---- */
  put_word(&w, 0xffd8);                          /* SOI */

  put_word(&w, 0xffe0);                           /* APP0 / JFIF */
  put_word(&w, 16);
  put_byte(&w, 'J'); put_byte(&w, 'F'); put_byte(&w, 'I'); put_byte(&w, 'F');
  put_byte(&w, 0);
  put_byte(&w, 1); put_byte(&w, 1);               /* version 1.1 */
  put_byte(&w, 0);                                /* units */
  put_word(&w, 1); put_word(&w, 1);               /* density */
  put_byte(&w, 0); put_byte(&w, 0);               /* thumb 0x0 */

  put_word(&w, 0xffdb);                           /* DQT (both tables) */
  put_word(&w, 2 + (1 + 64) * 2);
  put_byte(&w, 0x00);                             /* table 0, 8-bit */
  for (i = 0; i < 64; i++)
    {
      put_byte(&w, (uint8_t)lq[ZZ[i]]);
    }
  put_byte(&w, 0x01);                             /* table 1, 8-bit */
  for (i = 0; i < 64; i++)
    {
      put_byte(&w, (uint8_t)cq[ZZ[i]]);
    }

  put_word(&w, 0xffc0);                           /* SOF0 baseline */
  put_word(&w, 17);
  put_byte(&w, 8);                                /* precision */
  put_word(&w, (uint16_t)height);
  put_word(&w, (uint16_t)width);
  put_byte(&w, 3);                                /* components */
  put_byte(&w, 1); put_byte(&w, 0x11); put_byte(&w, 0);  /* Y  1x1 qt0 */
  put_byte(&w, 2); put_byte(&w, 0x11); put_byte(&w, 1);  /* Cb 1x1 qt1 */
  put_byte(&w, 3); put_byte(&w, 0x11); put_byte(&w, 1);  /* Cr 1x1 qt1 */

  /* DHT x4 */
  put_word(&w, 0xffc4); put_word(&w, 2 + 1 + 16 + 12);
  put_byte(&w, 0x00);
  for (i = 0; i < 16; i++) put_byte(&w, DC_L_BITS[i]);
  for (i = 0; i < 12; i++) put_byte(&w, DC_L_VAL[i]);

  put_word(&w, 0xffc4); put_word(&w, 2 + 1 + 16 + 162);
  put_byte(&w, 0x10);
  for (i = 0; i < 16; i++) put_byte(&w, AC_L_BITS[i]);
  for (i = 0; i < 162; i++) put_byte(&w, AC_L_VAL[i]);

  put_word(&w, 0xffc4); put_word(&w, 2 + 1 + 16 + 12);
  put_byte(&w, 0x01);
  for (i = 0; i < 16; i++) put_byte(&w, DC_C_BITS[i]);
  for (i = 0; i < 12; i++) put_byte(&w, DC_C_VAL[i]);

  put_word(&w, 0xffc4); put_word(&w, 2 + 1 + 16 + 162);
  put_byte(&w, 0x11);
  for (i = 0; i < 16; i++) put_byte(&w, AC_C_BITS[i]);
  for (i = 0; i < 162; i++) put_byte(&w, AC_C_VAL[i]);

  put_word(&w, 0xffda);                           /* SOS */
  put_word(&w, 12);
  put_byte(&w, 3);
  put_byte(&w, 1); put_byte(&w, 0x00);            /* Y  dc0 ac0 */
  put_byte(&w, 2); put_byte(&w, 0x11);            /* Cb dc1 ac1 */
  put_byte(&w, 3); put_byte(&w, 0x11);            /* Cr dc1 ac1 */
  put_byte(&w, 0); put_byte(&w, 63); put_byte(&w, 0);

  /* ---- entropy-coded MCUs (8x8, 4:4:4) ---- */
  for (my = 0; my < height; my += 8)
    {
      for (mx = 0; mx < width; mx += 8)
        {
          float yb[64];
          float cb[64];
          float cr[64];
          int r;
          int c;
          for (r = 0; r < 8; r++)
            {
              int sy = my + r;
              if (sy >= height) sy = height - 1;
              for (c = 0; c < 8; c++)
                {
                  int sx = mx + c;
                  int R;
                  int G;
                  int B;
                  uint16_t px;
                  if (sx >= width) sx = width - 1;
                  px = rgb565[sy * width + sx];
                  R = ((px >> 11) & 0x1f) * 255 / 31;
                  G = ((px >> 5) & 0x3f) * 255 / 63;
                  B = (px & 0x1f) * 255 / 31;
                  yb[r * 8 + c] =  0.299f * R + 0.587f * G + 0.114f * B - 128.0f;
                  cb[r * 8 + c] = -0.168736f * R - 0.331264f * G + 0.5f * B;
                  cr[r * 8 + c] =  0.5f * R - 0.418688f * G - 0.081312f * B;
                }
            }
          encode_block(&w, yb, lq, &hdc_l, &hac_l, M, &prev_dc_y);
          encode_block(&w, cb, cq, &hdc_c, &hac_c, M, &prev_dc_cb);
          encode_block(&w, cr, cq, &hdc_c, &hac_c, M, &prev_dc_cr);
          if (w.fail)
            {
              return -1;
            }
        }
    }

  flush_bits(&w);
  put_word(&w, 0xffd9);                            /* EOI */

  return w.fail ? -1 : w.len;
}
