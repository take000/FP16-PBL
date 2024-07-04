#include <float.h>
#include <stdio.h>

/* 1<= PEXT <= 10 */
#define PEXT 1
typedef unsigned int Uint;
typedef unsigned long long Ull;

union fpn {
    struct raw {
        Uint w;
    } raw;
    struct flo {
        float w;
    } flo;
    struct base {
        Uint frac : 10;
        Uint exp : 5;
        Uint s : 1;
    } base;
} in1, in2, in3, out, org;

// i1 * i2 + i3
Uint soft16(Uint i1, Uint i2, Uint i3, Uint o* ,Uint o* ,int     debug) {    
    in1.flo.w = i1;
    in2.flo.w = i2;
    in3.flo.w = i3;

    struct src {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Uint frac : 11;
        Uint exp : 5;
        Uint s : 1;
    } s1, s2, s3;

    struct fmul_s {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Uint frac : 11;
        Uint exp : 5;
        Uint s : 1;
    } fmul_s1, fmul_s2;

    struct fmul_d {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac : 22; /* send upper 12bit to the next stage */
        Uint exp : 6;
        Uint s : 1;
    } fmul_d;

    struct fadd_s {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac : 12 + PEXT; /* aligned to fmul_d */
        Uint exp : 6;
        Uint s : 1;
    } fadd_s1, fadd_s2;

    struct fadd_w {
        Uint exp_comp : 1;
        Uint exp_diff : 6;
        Uint align_exp : 6;
        Ull s1_align_frac : 12 + PEXT;
        Ull s2_align_frac : 12 + PEXT;
    } fadd_w;

    struct fadd_d {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac : 13 + PEXT;
        Uint exp : 6;
        Uint s : 1;
    } fadd_d;

    struct ex1_d {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac : 13 + PEXT;
        Uint exp : 6;
        Uint s : 1;
    } ex1_d;

    struct ex2_w {
        Uint lzc : 6;
    } ex2_w;

    struct ex2_d {
        Uint frac : 10;
        Uint exp : 5;
        Uint s : 1;
    } ex2_d;

    s2.s = in2.base.s;
    s2.exp = in2.base.exp;
    s2.frac = (in2.base.exp == 0) ? (0 << 10) | in2.base.frac
                                  : (1 << 10) | in2.base.frac;
    s2.zero = (in2.base.exp == 0) && (in2.base.frac == 0);
    s2.inf = (in2.base.exp == 31) && (in2.base.frac == 0);
    s2.nan = (in2.base.exp == 31) && (in2.base.frac != 0);

    fmul_s1.s = s2.s;
    fmul_s1.exp = s2.exp;
    fmul_s1.frac = s2.frac;
    fmul_s1.zero = s2.zero;
    fmul_s1.inf = s2.inf;
    fmul_s1.nan = s2.nan;
    fmul_s2.s = s3.s;
    fmul_s2.exp = s3.exp;
    fmul_s2.frac = s3.frac;
    fmul_s2.zero = s3.zero;
    fmul_s2.inf = s3.inf;
    fmul_s2.nan = s3.nan;

    /* nan  * any  -> nan */
    /* inf  * zero -> nan */
    /* inf  * (~zero & ~nan) -> inf */
    /* zero * (~inf  & ~nan) -> zero */
    fmul_d.s = fmul_s1.s ^ fmul_s2.s;
    fmul_d.exp = ((0 << 5) | fmul_s1.exp) + ((0 << 5) | fmul_s2.exp) < 15 ? 0 : ((0 << 5) | fmul_s1.exp) + ((0 << 5) | fmul_s2.exp) - 15;
    fmul_d.frac = (Ull)fmul_s1.frac * (Ull)fmul_s2.frac;
    fmul_d.zero = (fmul_s1.zero && !fmul_s2.inf && !fmul_s2.nan) || (fmul_s2.zero && !fmul_s1.inf && !fmul_s1.nan);
    fmul_d.inf = (fmul_s1.inf && !fmul_s2.zero && !fmul_s2.nan) || (fmul_s2.inf && !fmul_s1.zero && !fmul_s1.nan);
    fmul_d.nan = fmul_s1.nan || fmul_s2.nan || (fmul_s1.inf && fmul_s2.zero) || (fmul_s2.inf && fmul_s1.zero);

    fadd_s1.s = s1.s;
    fadd_s1.exp = (0 < s1.exp && s1.exp < 31) ? (s1.exp - 1) : s1.exp;
    fadd_s1.frac = (0 < s1.exp && s1.exp < 31) ? (Ull)s1.frac << (PEXT + 1) : (Ull)s1.frac << PEXT;
    fadd_s1.zero = s1.zero;
    fadd_s1.inf = s1.inf;
    fadd_s1.nan = s1.nan;
    fadd_s2.s = fmul_d.s;
    fadd_s2.exp = fmul_d.exp;
    fadd_s2.frac = fmul_d.frac >> (10 - PEXT); // ★★★ガード対応必要
    fadd_s2.zero = fmul_d.zero;
    fadd_s2.inf = fmul_d.inf;
    fadd_s2.nan = fmul_d.nan;

    fadd_w.exp_comp = fadd_s1.exp > fadd_s2.exp ? 1 : 0;
    fadd_w.exp_diff = fadd_w.exp_comp ? (fadd_s1.exp - fadd_s2.exp) : (fadd_s2.exp - fadd_s1.exp);
    if (fadd_w.exp_diff > 12 + PEXT) fadd_w.exp_diff = 12 + PEXT;
    fadd_w.align_exp = fadd_w.exp_comp ? fadd_s1.exp : fadd_s2.exp;
    fadd_w.s1_align_frac = fadd_s1.frac >> (fadd_w.exp_comp ? 0 : fadd_w.exp_diff);
    fadd_w.s2_align_frac = fadd_s2.frac >> (fadd_w.exp_comp ? fadd_w.exp_diff : 0);

    fadd_d.s           = (fadd_s1.s==fadd_s2.s) ? fadd_s1.s : (fadd_w.s1_align_frac>fadd_w.s2_align_frac) ? fadd_s1.s : fadd_s2.s;
    fadd_d.exp         = fadd_w.align_exp;
    fadd_d.frac        = (fadd_s1.s == fadd_s2.s)                    ? (Ull)fadd_w.s1_align_frac+(Ull)fadd_w.s2_align_frac :
                        (fadd_w.s1_align_frac>fadd_w.s2_align_frac) ? (Ull)fadd_w.s1_align_frac-(Ull)fadd_w.s2_align_frac :
                                                                        (Ull)fadd_w.s2_align_frac-(Ull)fadd_w.s1_align_frac ;
    fadd_d.zero        = fadd_d.frac==0;
    fadd_d.inf         = (!fadd_s1.s && fadd_s1.inf && !(fadd_s2.s && fadd_s2.inf) && !fadd_s2.nan) || (fadd_s1.s && fadd_s1.inf && !(!fadd_s2.s && fadd_s2.inf) && !fadd_s2.nan) ||
                        (!fadd_s2.s && fadd_s2.inf && !(fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan) || (fadd_s2.s && fadd_s2.inf && !(!fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan) ;
    fadd_d.nan         = fadd_s1.nan || fadd_s2.nan;

    ex1_d.s            = fadd_d.s;
    ex1_d.exp          = fadd_d.exp;
    ex1_d.frac         = fadd_d.frac;
    ex1_d.zero         = fadd_d.zero;
    ex1_d.inf          = fadd_d.inf;
    ex1_d.nan          = fadd_d.nan;

#if 1
  ex2_w.lzc          = (ex1_d.frac & 0x2000000LL<<PEXT)?62 :
                       (ex1_d.frac & 0x1000000LL<<PEXT)?63 :
                       (ex1_d.frac & 0x0800000LL<<PEXT)? 0 :
                       (ex1_d.frac & 0x0400000LL<<PEXT)? 1 :
                       (ex1_d.frac & 0x0200000LL<<PEXT)? 2 :
                       (ex1_d.frac & 0x0100000LL<<PEXT)? 3 :
                       (ex1_d.frac & 0x0080000LL<<PEXT)? 4 :
                       (ex1_d.frac & 0x0040000LL<<PEXT)? 5 :
                       (ex1_d.frac & 0x0020000LL<<PEXT)? 6 :
                       (ex1_d.frac & 0x0010000LL<<PEXT)? 7 :
                       (ex1_d.frac & 0x0008000LL<<PEXT)? 8 :
                       (ex1_d.frac & 0x0004000LL<<PEXT)? 9 :
                       (ex1_d.frac & 0x0002000LL<<PEXT)?10 :
                       (ex1_d.frac & 0x0001000LL<<PEXT)?11 :
                       (ex1_d.frac & 0x0000800LL<<PEXT)?12 :
                       (ex1_d.frac & 0x0000400LL<<PEXT)?13 :
                       (ex1_d.frac & 0x0000200LL<<PEXT)?14 :
                       (ex1_d.frac & 0x0000100LL<<PEXT)?15 :
                       (ex1_d.frac & 0x0000080LL<<PEXT)?16 :
                       (ex1_d.frac & 0x0000040LL<<PEXT)?17 :
                       (ex1_d.frac & 0x0000020LL<<PEXT)?18 :
                       (ex1_d.frac & 0x0000010LL<<PEXT)?19 :
                       (ex1_d.frac & 0x0000008LL<<PEXT)?20 :
                       (ex1_d.frac & 0x0000004LL<<PEXT)?21 :
                       (ex1_d.frac & 0x0000002LL<<PEXT)?22 :
                       (ex1_d.frac & 0x0000001LL<<PEXT)?23 :
#if (PEXT>= 1)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 1)?24 :
#endif
#if (PEXT>= 2)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 2)?25 :
#endif
#if (PEXT>= 3)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 3)?26 :
#endif
#if (PEXT>= 4)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 4)?27 :
#endif
#if (PEXT>= 5)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 5)?28 :
#endif
#if (PEXT>= 6)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 6)?29 :
#endif
#if (PEXT>= 7)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 7)?30 :
#endif
#if (PEXT>= 8)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 8)?31 :
#endif
#if (PEXT>= 9)
                       (ex1_d.frac & 0x0000001LL<<PEXT- 9)?32 :
#endif
#if (PEXT>=10)
                       (ex1_d.frac & 0x0000001LL<<PEXT-10)?33 :
#endif
#if (PEXT>=11)
                       (ex1_d.frac & 0x0000001LL<<PEXT-11)?34 :
#endif
#if (PEXT>=12)
                       (ex1_d.frac & 0x0000001LL<<PEXT-12)?35 :
#endif
#if (PEXT>=13)
                       (ex1_d.frac & 0x0000001LL<<PEXT-13)?36 :
#endif
#if (PEXT>=14)
                       (ex1_d.frac & 0x0000001LL<<PEXT-14)?37 :
#endif
#if (PEXT>=15)
                       (ex1_d.frac & 0x0000001LL<<PEXT-15)?38 :
#endif
#if (PEXT>=16)
                       (ex1_d.frac & 0x0000001LL<<PEXT-16)?39 :
#endif
#if (PEXT>=17)
                       (ex1_d.frac & 0x0000001LL<<PEXT-17)?40 :
#endif
#if (PEXT>=18)
                       (ex1_d.frac & 0x0000001LL<<PEXT-18)?41 :
#endif
#if (PEXT>=19)
                       (ex1_d.frac & 0x0000001LL<<PEXT-19)?42 :
#endif
#if (PEXT>=20)
                       (ex1_d.frac & 0x0000001LL<<PEXT-20)?43 :
#endif
#if (PEXT>=21)
                       (ex1_d.frac & 0x0000001LL<<PEXT-21)?44 :
#endif
#if (PEXT>=22)
                       (ex1_d.frac & 0x0000001LL<<PEXT-22)?45 :
#endif
#if (PEXT>=23)
                       (ex1_d.frac & 0x0000001LL<<PEXT-22)?46 :
#endif
                                                       24+PEXT;
  if (info) {
    printf("//ex1:%x %x %08.8x_%08.8x ", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.frac>>32), (Uint)ex1_d.frac);
  }

  if (ex1_d.nan) {
    ex2_d.s    = 1;
    ex2_d.frac = 0x400000;
    ex2_d.exp  = 0xff;

  }
  else if (ex1_d.inf) {
    ex2_d.s    = ex1_d.s;
    ex2_d.frac = 0x000000;
    ex2_d.exp  = 0xff;
  }
  else if (ex2_w.lzc == 62) {
    if (info) {
      printf("lzc==%d\n", ex2_w.lzc);
    }
    if (ex1_d.exp >= 253) {
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = 0x000000;
      ex2_d.exp  = 0xff;
    }
    else {
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = ex1_d.frac>>(2+PEXT); //★★★ガード対応必要
      ex2_d.exp  = ex1_d.exp + 2;
    }
  }
  else if (ex2_w.lzc == 63) {
    if (info) {
      printf("lzc==%d\n", ex2_w.lzc);
    }
    if (ex1_d.exp >= 254) {
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = 0x000000;
      ex2_d.exp  = 0xff;
    }
    else {
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = ex1_d.frac>>(1+PEXT); //★★★ガード対応必要
      ex2_d.exp  = ex1_d.exp + 1;
    }
  }
  else if (ex2_w.lzc <= (23+PEXT)) {
    if (info) {
      printf("lzc==%d\n", ex2_w.lzc);
    }
    if (ex1_d.exp >= ex2_w.lzc + 255) {
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = 0x000000;
      ex2_d.exp  = 0xff;
    }
    else if (ex1_d.exp <= ex2_w.lzc) { /* subnormal num */
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = (ex1_d.frac<<ex1_d.exp)>>PEXT;
      ex2_d.exp  = 0x00;
    }
    else { /* normalized num */
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = (ex1_d.frac<<ex2_w.lzc)>>PEXT;
      ex2_d.exp  = ex1_d.exp - ex2_w.lzc;
    }
#define NO_GUARD_BITS
#ifndef NO_GUARD_BITS
    int f_ulp = (ex1_d.frac<<ex2_w.lzc)>> PEXT   &1;
    int f_g   = (ex1_d.frac<<ex2_w.lzc)>>(PEXT-1)&1;
    int f_r   = (ex1_d.frac<<ex2_w.lzc)>>(PEXT-2)&1;
    int f_s   =((ex1_d.frac<<ex2_w.lzc)&(0xfffffffffffLL>>(46-PEXT))!=0;
    switch (f_ulp<<3|f_g<<2|f_r<<1|f_s) {
    case 0: case 1: case 2: case 3: case 4: /* ulp|G|R|S */
    case 8: case 9: case 10: case 11:
      break;
    case 5: case 6: case 7: /* ulp++ */
    case 12: case 13: case 14: case 15: default:
      if (info)
	printf("//ex2:%x %x %x++ -> ", ex2_d.s, ex2_d.exp, ex2_d.frac);
      ex2_d.frac++;
      if (info)
	printf("%x\n", ex2_d.frac);
      break;
    }
#endif
  }
  else { /* zero */
    if (info) {
      printf("zero\n");
    }
    ex2_d.s    = 0;
    ex2_d.frac = 0x000000;
    ex2_d.exp  = 0x00;
  }
#endif

  if (info) {
    printf("//ex2:%x %x %x\n", ex2_d.s, ex2_d.exp, ex2_d.frac);
  }

  out.raw.w  = (ex2_d.s<<31)|(ex2_d.exp<<23)|(ex2_d.frac);
  org.flo.w  = i1+i2*i3;
  Uint diff = out.raw.w>org.raw.w ? out.raw.w-org.raw.w : org.raw.w-out.raw.w;

  if (!info)
    sprintf(softbuf32, "%8.8e:%08.8x %8.8e:%08.8x %8.8e:%08.8x ->%8.8e:%08.8x (%8.8e:%08.8x) %08.8x %s%s%s",
           in1.flo.w, in1.raw.w, in2.flo.w, in2.raw.w, in3.flo.w, in3.raw.w, out.flo.w, out.raw.w, org.flo.w, org.raw.w, diff,
           diff>=TH1 ? "S":"",
           diff>=TH2 ? "S":"",
           diff>=TH3 ? "S":""
           );
  *o = out.flo.w;
  return(diff);

}