
/* 逆行列計算を使った,fpu精度テスト                      */
/*                          Copyright (C) 2013- by NAIST */
/*                           Primary writer: Y.Nakashima */
/*                                  nakashim@is.naist.jp */

/* 1<= PEXT <= 23 */
#define PEXT 1
// #define PEXT 23

#ifndef UTYPEDEF
#define UTYPEDEF
typedef unsigned char Uchar;
typedef unsigned short Ushort;
typedef unsigned int Uint;
typedef unsigned long long Ull;
typedef long long int Sll;
#if __AARCH64EL__ == 1
typedef long double Dll;
#else
typedef struct {
    Ull u[2];
} Dll;
#endif
#endif

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

union fpn {
    struct raw {
        Uint w;
    } raw;
    struct flo {
        float w;
    } flo;
    struct base {
        Uint frac : 23;
        Uint exp : 8;
        Uint s : 1;
    } base;
} in1, in2, in3, out, org;

void radix4(Uint *pp, Uint *ps, Uint a, Uint b) {
    switch (b) {
    case 0:
        *pp = 0;
        *ps = 0;
        break;
    case 1:
        *pp = a & 0x1ffffff;
        *ps = 0;
        break;
    case 2:
        *pp = a & 0x1ffffff;
        *ps = 0;
        break;
    case 3:
        *pp = a << 1 & 0x1ffffff;
        *ps = 0;
        break;
    case 4:
        *pp = ~(a << 1) & 0x1ffffff;
        *ps = 1;
        break;
    case 5:
        *pp = ~a & 0x1ffffff;
        *ps = 1;
        break;
    case 6:
        *pp = ~a & 0x1ffffff;
        *ps = 1;
        break;
    default:
        *pp = ~0 & 0x1ffffff;
        *ps = 1;
        break;
    }
}

void partial_product(Uint *pp, Uint *ps, Uint a, Uint b, Uint pos) {
    /* switch (pos) */
    /* case 0:    "~s  s  s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
    /* case 1-10: "    1 ~s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
    /* case 11:   "      ~s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
    /* case 12:   "         24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
    Uint tp, ts;

    radix4(&tp, &ts, a, b);
    switch (pos) {
    case 0:
        *pp = ((~ts & 1) << 27) | (ts << 26) | (ts << 25) | tp;
        *ps = ts;
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
        *pp = (1 << 26) | ((~ts & 1) << 25) | tp;
        *ps = ts;
        break;
    case 11:
        *pp = ((~ts & 1) << 25) | tp;
        *ps = ts;
        break;
    default:
        *pp = tp;
        *ps = ts;
        break;
    }
}

void csa_line(Ull *co, Ull *s, Ull a, Ull b, Ull c) {
    *s = a ^ b ^ c;
    *co = ((a & b) | (b & c) | (c & a)) << 1;
}

void hard32(Uint info, float i1, float i2, float i3, float *o, Uint testbench) {
    int op = 3;
    in1.flo.w = i1;
    in2.flo.w = i2;
    in3.flo.w = i3;
    /* op=1:fmul (0.0 + s2 *  s3)  */
    /* op=2:fadd (s1  + s2 * 1.0) */
    /* op=3:fma3 (s1  + s2 *  s3)  */

    /* op=1:fmul, 2:fadd, 3:fma3 */
    struct src {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Uint frac : 24;
        Uint exp : 8;
        Uint s : 1;
    } s1, s2, s3; /* s1 + s2 * s3 */

    Uint tp;
    Uint ps[13]; /* partial_sign */
    Ull pp[13];  /* partial_product */
    Ull S1[4];   /* stage-1 */
    Ull C1[4];   /* stage-1 */
    Ull S2[3];   /* stage-2 */
    Ull C2[3];   /* stage-2 */
    Ull S3[2];   /* stage-3 */
    Ull C3[2];   /* stage-3 */
    Ull S4;      /* stage-4 */
    Ull C4;      /* stage-4 */
    Ull S5;      /* stage-5 */
    Ull C5;      /* stage-5 */
    Ull S6[3];   /* stage-6 */
    Ull C6[3];   /* stage-6 */
    Ull S7[3];   /* stage-6 */
    Ull C7[3];   /* stage-6 */

    struct ex1_d {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull csa_s : 25 + PEXT; // ■■■
        Ull csa_c : 25 + PEXT; // ■■■
        Uint exp : 9;
        Uint s : 1;
    } ex1_d; /* csa */

    struct fadd_s {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac : 25 + PEXT; /* ■■■aligned to ex1_d */
        Uint exp : 9;
        Uint s : 1;
    } fadd_s1;

    struct fadd_w {
        Uint exp_comp : 1;
        Uint exp_diff : 9;
        Uint align_exp : 9;
        Ull s1_align_frac : 25 + PEXT; // ■■■
        Ull s2_align_frac : 25 + PEXT; // ■■■
        Ull s3_align_frac : 25 + PEXT; // ■■■
    } fadd_w;

    struct ex2_d {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac0 : 26 + PEXT; /* 26bit */ // ■■■
        Ull frac1 : 25 + PEXT; /* 25bit */ // ■■■
        Ull frac2 : 26 + PEXT; /* 26bit */ // ■■■
        Ull frac : 26 + PEXT; /* 26bit */  // ■■■
        Uint exp : 9;
        Uint s : 1;
    } ex2_d;

    struct ex3_w {
        Uint lzc : 6;
    } ex3_w;

    struct ex3_d {
        Uint frac : 23;
        Uint exp : 8;
        Uint s : 1;
    } ex3_d;

    s1.s = (op == 1) ? 0 : in1.base.s;
    s1.exp = (op == 1) ? 0 : in1.base.exp;
    s1.frac = (op == 1) ? 0 : (in1.base.exp == 0) ? (0 << 23) | in1.base.frac
                                                  : (1 << 23) | in1.base.frac;
    s1.zero = (op == 1) ? 1 : (in1.base.exp == 0) && (in1.base.frac == 0);
    s1.inf = (op == 1) ? 0 : (in1.base.exp == 255) && (in1.base.frac == 0);
    s1.nan = (op == 1) ? 0 : (in1.base.exp == 255) && (in1.base.frac != 0);
    s2.s = in2.base.s;
    s2.exp = in2.base.exp;
    s2.frac = (in2.base.exp == 0) ? (0 << 23) | in2.base.frac : (1 << 23) | in2.base.frac;
    s2.zero = (in2.base.exp == 0) && (in2.base.frac == 0);
    s2.inf = (in2.base.exp == 255) && (in2.base.frac == 0);
    s2.nan = (in2.base.exp == 255) && (in2.base.frac != 0);
    s3.s = (op == 2) ? 0 : in3.base.s;
    s3.exp = (op == 2) ? 127 : in3.base.exp;
    s3.frac = (op == 2) ? (1 << 23) : (in3.base.exp == 0) ? (0 << 23) | in3.base.frac
                                                          : (1 << 23) | in3.base.frac;
    s3.zero = (op == 2) ? 0 : (in3.base.exp == 0) && (in3.base.frac == 0);
    s3.inf = (op == 2) ? 0 : (in3.base.exp == 255) && (in3.base.frac == 0);
    s3.nan = (op == 2) ? 0 : (in3.base.exp == 255) && (in3.base.frac != 0);

    org.flo.w = in1.flo.w + in2.flo.w * in3.flo.w;
    if (info) {
        printf("//--hard32--\n");
        printf("//s1: %08.8x %f\n", in1.raw.w, in1.flo.w);
        printf("//s2: %08.8x %f\n", in2.raw.w, in2.flo.w);
        printf("//s3: %08.8x %f\n", in3.raw.w, in3.flo.w);
        printf("//d : %08.8x %f\n", org.raw.w, org.flo.w);
    }

    /* nan  * any  -> nan */
    /* inf  * zero -> nan */
    /* inf  * (~zero & ~nan) -> inf */
    /* zero * (~inf  & ~nan) -> zero */
    ex1_d.s = s2.s ^ s3.s;
    ex1_d.exp = ((0 << 8) | s2.exp) + ((0 << 8) | s3.exp) < 127 ? 0 : ((0 << 8) | s2.exp) + ((0 << 8) | s3.exp) - 127;

    /**************************************************************************************************************/
    /***  partial product  ****************************************************************************************/
    /**************************************************************************************************************/
    /*ex1_d.frac = (Ull)s2.frac * (Ull)s3.frac;*/
    partial_product(&tp, &ps[0], s2.frac, (s3.frac << 1) & 7, 0);
    pp[0] = (Ull)tp; // if (info) {printf("pp[ 0]=%04.4x_%08.8x ps[ 0]=%d\n", (Uint)(pp[ 0]>>32), (Uint)pp[ 0], ps[ 0]);} /*1,0,-1*/
    partial_product(&tp, &ps[1], s2.frac, (s3.frac >> 1) & 7, 1);
    pp[1] = ((Ull)tp << 2) | (Ull)ps[0]; // if (info) {printf("pp[ 1]=%04.4x_%08.8x ps[ 1]=%d\n", (Uint)(pp[ 1]>>32), (Uint)pp[ 1], ps[ 1]);} /*3,2, 1*/
    partial_product(&tp, &ps[2], s2.frac, (s3.frac >> 3) & 7, 2);
    pp[2] = ((Ull)tp << 4) | ((Ull)ps[1] << 2); // if (info) {printf("pp[ 2]=%04.4x_%08.8x ps[ 2]=%d\n", (Uint)(pp[ 2]>>32), (Uint)pp[ 2], ps[ 2]);} /*5,4, 3*/
    partial_product(&tp, &ps[3], s2.frac, (s3.frac >> 5) & 7, 3);
    pp[3] = ((Ull)tp << 6) | ((Ull)ps[2] << 4); // if (info) {printf("pp[ 3]=%04.4x_%08.8x ps[ 3]=%d\n", (Uint)(pp[ 3]>>32), (Uint)pp[ 3], ps[ 3]);} /*7,6, 5*/
    partial_product(&tp, &ps[4], s2.frac, (s3.frac >> 7) & 7, 4);
    pp[4] = ((Ull)tp << 8) | ((Ull)ps[3] << 6); // if (info) {printf("pp[ 4]=%04.4x_%08.8x ps[ 4]=%d\n", (Uint)(pp[ 4]>>32), (Uint)pp[ 4], ps[ 4]);} /*9,8, 7*/
    partial_product(&tp, &ps[5], s2.frac, (s3.frac >> 9) & 7, 5);
    pp[5] = ((Ull)tp << 10) | ((Ull)ps[4] << 8); // if (info) {printf("pp[ 5]=%04.4x_%08.8x ps[ 5]=%d\n", (Uint)(pp[ 5]>>32), (Uint)pp[ 5], ps[ 5]);} /*11,10,9*/
    partial_product(&tp, &ps[6], s2.frac, (s3.frac >> 11) & 7, 6);
    pp[6] = ((Ull)tp << 12) | ((Ull)ps[5] << 10); // if (info) {printf("pp[ 6]=%04.4x_%08.8x ps[ 6]=%d\n", (Uint)(pp[ 6]>>32), (Uint)pp[ 6], ps[ 6]);} /*13,12,11*/
    partial_product(&tp, &ps[7], s2.frac, (s3.frac >> 13) & 7, 7);
    pp[7] = ((Ull)tp << 14) | ((Ull)ps[6] << 12); // if (info) {printf("pp[ 7]=%04.4x_%08.8x ps[ 7]=%d\n", (Uint)(pp[ 7]>>32), (Uint)pp[ 7], ps[ 7]);} /*15,14,13*/
    partial_product(&tp, &ps[8], s2.frac, (s3.frac >> 15) & 7, 8);
    pp[8] = ((Ull)tp << 16) | ((Ull)ps[7] << 14); // if (info) {printf("pp[ 8]=%04.4x_%08.8x ps[ 8]=%d\n", (Uint)(pp[ 8]>>32), (Uint)pp[ 8], ps[ 8]);} /*17,16,15*/
    partial_product(&tp, &ps[9], s2.frac, (s3.frac >> 17) & 7, 9);
    pp[9] = ((Ull)tp << 18) | ((Ull)ps[8] << 16); // if (info) {printf("pp[ 9]=%04.4x_%08.8x ps[ 9]=%d\n", (Uint)(pp[ 9]>>32), (Uint)pp[ 9], ps[ 9]);} /*19,18,17*/
    partial_product(&tp, &ps[10], s2.frac, (s3.frac >> 19) & 7, 10);
    pp[10] = ((Ull)tp << 20) | ((Ull)ps[9] << 18); // if (info) {printf("pp[10]=%04.4x_%08.8x ps[10]=%d\n", (Uint)(pp[10]>>32), (Uint)pp[10], ps[10]);} /*21,20,19*/
    partial_product(&tp, &ps[11], s2.frac, (s3.frac >> 21) & 7, 11);
    pp[11] = ((Ull)tp << 22) | ((Ull)ps[10] << 20); // if (info) {printf("pp[11]=%04.4x_%08.8x ps[11]=%d\n", (Uint)(pp[11]>>32), (Uint)pp[11], ps[11]);} /**23,22,21*/
    partial_product(&tp, &ps[12], s2.frac, (s3.frac >> 23) & 7, 12);
    pp[12] = ((Ull)tp << 24) | ((Ull)ps[11] << 22); // if (info) {printf("pp[12]=%04.4x_%08.8x ps[12]=%d\n", (Uint)(pp[12]>>32), (Uint)pp[12], ps[12]);} /*25,24,*23*/

    Ull x1 = (pp[0] + pp[1] + pp[2] + pp[3] + pp[4] + pp[5] + pp[6] + pp[7] + pp[8] + pp[9] + pp[10] + pp[11] + pp[12]);
    if (info) {
        printf("//x1(sum of pp)=%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x1 >> 32), (Uint)x1, (Uint)(x1 >> 23));
    }
    Ull x2 = (Ull)s2.frac * (Ull)s3.frac;
    if (info) {
        printf("//x2(s2 * s3)  =%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x2 >> 32), (Uint)x2, (Uint)(x2 >> 23));
    }

    /**************************************************************************************************************/
    /***  csa tree  ***********************************************************************************************/
    /**************************************************************************************************************/
    csa_line(&C1[0], &S1[0], pp[0], pp[1], pp[2]);
    csa_line(&C1[1], &S1[1], pp[3], pp[4], pp[5]);
    csa_line(&C1[2], &S1[2], pp[6], pp[7], pp[8]);
    csa_line(&C1[3], &S1[3], pp[9], pp[10], pp[11]);

    csa_line(&C2[0], &S2[0], S1[0], C1[0], S1[1]);
    csa_line(&C2[1], &S2[1], C1[1], S1[2], C1[2]);
    csa_line(&C2[2], &S2[2], S1[3], C1[3], pp[12]);

    csa_line(&C3[0], &S3[0], S2[0], C2[0], S2[1]);
    csa_line(&C3[1], &S3[1], C2[1], S2[2], C2[2]);

    csa_line(&C4, &S4, S3[0], C3[0], S3[1]);
    csa_line(&C5, &S5, S4, C4, C3[1]);

    ex1_d.csa_s = S5 >> (23 - PEXT); // sum   ■■■ガード対応必要
    ex1_d.csa_c = C5 >> (23 - PEXT); // carry ■■■ガード対応必要

    ex1_d.zero = (s2.zero && !s3.inf && !s3.nan) || (s3.zero && !s2.inf && !s2.nan);
    ex1_d.inf = (s2.inf && !s3.zero && !s3.nan) || (s3.inf && !s2.zero && !s2.nan);
    ex1_d.nan = s2.nan || s3.nan || (s2.inf && s3.zero) || (s3.inf && s2.zero);

    if (info) {
        printf("//S5           =%08.8x_%08.8x\n", (Uint)(S5 >> 32), (Uint)S5);
        printf("//C5           =%08.8x_%08.8x\n", (Uint)(C5 >> 32), (Uint)C5);
        printf("//++(48bit)    =%08.8x_%08.8x\n", (Uint)((C5 + S5) >> 32), (Uint)(C5 + S5));
        printf("//csa_s        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_s >> 32), (Uint)ex1_d.csa_s);
        printf("//csa_c        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_c >> 32), (Uint)ex1_d.csa_c);
        printf("//ex1_d: %x %02.2x +=%08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)(ex1_d.csa_c + ex1_d.csa_s) >> 32), (Uint)(ex1_d.csa_c + ex1_d.csa_s));
    }

    /**************************************************************************************************************/
    /***  3in-csa  ************************************************************************************************/
    /**************************************************************************************************************/
    fadd_s1.s = s1.s;
    fadd_s1.exp = (0 < s1.exp && s1.exp < 255) ? (s1.exp - 1) : s1.exp;                              // ■■■
    fadd_s1.frac = (0 < s1.exp && s1.exp < 255) ? (Ull)s1.frac << (PEXT + 1) : (Ull)s1.frac << PEXT; // ■■■
    fadd_s1.zero = s1.zero;
    fadd_s1.inf = s1.inf;
    fadd_s1.nan = s1.nan;

    /* nan  + any  -> nan */
    /* inf  + -inf -> nan */
    /* inf  + (~-inf & ~nan) -> inf */
    /* -inf + (~inf  & ~nan) -> inf */
    fadd_w.exp_comp = fadd_s1.exp > ex1_d.exp ? 1 : 0;
    fadd_w.exp_diff = fadd_w.exp_comp ? (fadd_s1.exp - ex1_d.exp) : (ex1_d.exp - fadd_s1.exp);
    if (fadd_w.exp_diff > (25 + PEXT)) fadd_w.exp_diff = (25 + PEXT); // ■■■
    fadd_w.align_exp = fadd_w.exp_comp ? fadd_s1.exp : ex1_d.exp;
    fadd_w.s1_align_frac = fadd_s1.frac >> (fadd_w.exp_comp ? 0 : fadd_w.exp_diff);
    fadd_w.s2_align_frac = ex1_d.csa_s >> (ex1_d.zero ? (25 + PEXT) : fadd_w.exp_comp ? fadd_w.exp_diff
                                                                                      : 0);
    fadd_w.s3_align_frac = ex1_d.csa_c >> (ex1_d.zero ? (25 + PEXT) : fadd_w.exp_comp ? fadd_w.exp_diff
                                                                                      : 0);

    if (info) {
        printf("//fadd_s1: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s1.s, fadd_s1.exp, (Uint)((Ull)fadd_s1.frac >> 32), (Uint)fadd_s1.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s1_align_frac >> 32), (Uint)fadd_w.s1_align_frac);
        printf("//csa_s: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_s >> 32), (Uint)ex1_d.csa_s, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s2_align_frac >> 32), (Uint)fadd_w.s2_align_frac);
        printf("//csa_c: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_c >> 32), (Uint)ex1_d.csa_c, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s3_align_frac >> 32), (Uint)fadd_w.s3_align_frac);
    }

    /*ex2_d.frac0       =  fadd_w.s1_align_frac+ (fadd_w.s2_align_frac+fadd_w.s3_align_frac);                        */
    /*ex2_d.frac1       =  fadd_w.s1_align_frac+~(fadd_w.s2_align_frac+fadd_w.s3_align_frac)+1;                      */
    /*ex2_d.frac2       = ~fadd_w.s1_align_frac+ (fadd_w.s2_align_frac+fadd_w.s3_align_frac)+1;                      */
    /*ex2_d.frac        = (fadd_s1.s==ex1_d.s) ? ex2_d.frac0 : (ex2_d.frac2 & 0x2000000) ? ex2_d.frac1 : ex2_d.frac2;*/
    /*printf("ex2d.frac0: %08.8x\n", ex2_d.frac0);*/
    /*printf("ex2d.frac1: %08.8x\n", ex2_d.frac1);*/
    /*printf("ex2d.frac2: %08.8x\n", ex2_d.frac2);*/
    /*printf("ex2d.frac:  %08.8x\n", ex2_d.frac );*/
    csa_line(&C6[0], &S6[0], fadd_w.s1_align_frac, fadd_w.s2_align_frac, fadd_w.s3_align_frac);
    csa_line(&C6[1], &S6[1], fadd_w.s1_align_frac, ~(Ull)fadd_w.s2_align_frac, ~(Ull)fadd_w.s3_align_frac);
    csa_line(&C7[1], &S7[1], C6[1] | 1LL, S6[1], 1LL);
    csa_line(&C6[2], &S6[2], ~(Ull)fadd_w.s1_align_frac, fadd_w.s2_align_frac, fadd_w.s3_align_frac);
    csa_line(&C7[2], &S7[2], C6[2] | 1LL, S6[2], 0LL);

    if (info) {
        printf("//C6[0]=%08.8x_%08.8x(a+c+s)\n", (Uint)(C6[0] >> 32), (Uint)C6[0]);
        printf("//S6[0]=%08.8x_%08.8x(a+c+s)\n", (Uint)(S6[0] >> 32), (Uint)S6[0]);
        printf("//C6[1]=%08.8x_%08.8x(a-c-s)\n", (Uint)(C6[1] >> 32), (Uint)C6[1]);
        printf("//S6[1]=%08.8x_%08.8x(a-c-s)\n", (Uint)(S6[1] >> 32), (Uint)S6[1]);
        printf("//C7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(C7[1] >> 32), (Uint)C7[1]);
        printf("//S7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(S7[1] >> 32), (Uint)S7[1]);
        printf("//C6[2]=%08.8x_%08.8x(c+s-a)\n", (Uint)(C6[2] >> 32), (Uint)C6[2]);
        printf("//S6[2]=%08.8x_%08.8x(c+s-a)\n", (Uint)(S6[2] >> 32), (Uint)S6[2]);
        printf("//C7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(C7[2] >> 32), (Uint)C7[2]);
        printf("//S7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(S7[2] >> 32), (Uint)S7[2]);
    }

    /**************************************************************************************************************/
    /***  2in-add  ************************************************************************************************/
    /**************************************************************************************************************/
    ex2_d.frac0 = C6[0] + S6[0]; /* 26bit */
    ex2_d.frac1 = C7[1] + S7[1]; /* 25bit */
    ex2_d.frac2 = C7[2] + S7[2]; /* 26bit */

    if (info) {
        printf("//ex2_d.frac0=%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac0 >> 32), (Uint)ex2_d.frac0);
        printf("//ex2_d.frac1=%08.8x_%08.8x(a-c-s)\n", (Uint)((Ull)ex2_d.frac1 >> 32), (Uint)ex2_d.frac1);
        printf("//ex2_d.frac2=%08.8x_%08.8x(c+s-a)\n", (Uint)((Ull)ex2_d.frac2 >> 32), (Uint)ex2_d.frac2);
    }

    ex2_d.s = (fadd_s1.s == ex1_d.s) ? fadd_s1.s : (ex2_d.frac2 & (0x2000000LL << PEXT)) ? fadd_s1.s
                                                                                         : ex1_d.s; // ■■■
    ex2_d.exp = fadd_w.align_exp;
    ex2_d.frac = (fadd_s1.s == ex1_d.s) ? ex2_d.frac0 : (ex2_d.frac2 & (0x2000000LL << PEXT)) ? ex2_d.frac1
                                                                                              : ex2_d.frac2 & (0xffffffffffffLL >> (23 - PEXT));
    /* 26bit */ // ■■■
    ex2_d.zero = ex2_d.frac == 0;
    ex2_d.inf = (!fadd_s1.s && fadd_s1.inf && !(ex1_d.s && ex1_d.inf) && !ex1_d.nan) || (fadd_s1.s && fadd_s1.inf && !(!ex1_d.s && ex1_d.inf) && !ex1_d.nan) || (!ex1_d.s && ex1_d.inf && !(fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan) || (ex1_d.s && ex1_d.inf && !(!fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan);
    ex2_d.nan = fadd_s1.nan || ex1_d.nan;

    if (info) {
        printf("//ex2_d.frac =%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac >> 32), (Uint)ex2_d.frac);
    }

#define FLOAT_PZERO 0x00000000
#define FLOAT_NZERO 0x80000000
#define FLOAT_PINF 0x7f800000
#define FLOAT_NINF 0xff800000
#define FLOAT_NAN 0xffc00000

    /**************************************************************************************************************/
    /***  normalize  **********************************************************************************************/
    /**************************************************************************************************************/
#if 1
    ex3_w.lzc = (ex2_d.frac & 0x2000000LL << PEXT) ? 62 : (ex2_d.frac & 0x1000000LL << PEXT) ? 63
                                                      : (ex2_d.frac & 0x0800000LL << PEXT)   ? 0
                                                      : (ex2_d.frac & 0x0400000LL << PEXT)   ? 1
                                                      : (ex2_d.frac & 0x0200000LL << PEXT)   ? 2
                                                      : (ex2_d.frac & 0x0100000LL << PEXT)   ? 3
                                                      : (ex2_d.frac & 0x0080000LL << PEXT)   ? 4
                                                      : (ex2_d.frac & 0x0040000LL << PEXT)   ? 5
                                                      : (ex2_d.frac & 0x0020000LL << PEXT)   ? 6
                                                      : (ex2_d.frac & 0x0010000LL << PEXT)   ? 7
                                                      : (ex2_d.frac & 0x0008000LL << PEXT)   ? 8
                                                      : (ex2_d.frac & 0x0004000LL << PEXT)   ? 9
                                                      : (ex2_d.frac & 0x0002000LL << PEXT)   ? 10
                                                      : (ex2_d.frac & 0x0001000LL << PEXT)   ? 11
                                                      : (ex2_d.frac & 0x0000800LL << PEXT)   ? 12
                                                      : (ex2_d.frac & 0x0000400LL << PEXT)   ? 13
                                                      : (ex2_d.frac & 0x0000200LL << PEXT)   ? 14
                                                      : (ex2_d.frac & 0x0000100LL << PEXT)   ? 15
                                                      : (ex2_d.frac & 0x0000080LL << PEXT)   ? 16
                                                      : (ex2_d.frac & 0x0000040LL << PEXT)   ? 17
                                                      : (ex2_d.frac & 0x0000020LL << PEXT)   ? 18
                                                      : (ex2_d.frac & 0x0000010LL << PEXT)   ? 19
                                                      : (ex2_d.frac & 0x0000008LL << PEXT)   ? 20
                                                      : (ex2_d.frac & 0x0000004LL << PEXT)   ? 21
                                                      : (ex2_d.frac & 0x0000002LL << PEXT)   ? 22
                                                      : (ex2_d.frac & 0x0000001LL << PEXT)   ? 23
                                                      :
#if (PEXT >= 1)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 1) ? 24
                                                      :
#endif
#if (PEXT >= 2)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 2) ? 25
                                                      :
#endif
#if (PEXT >= 3)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 3) ? 26
                                                      :
#endif
#if (PEXT >= 4)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 4) ? 27
                                                      :
#endif
#if (PEXT >= 5)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 5) ? 28
                                                      :
#endif
#if (PEXT >= 6)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 6) ? 29
                                                      :
#endif
#if (PEXT >= 7)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 7) ? 30
                                                      :
#endif
#if (PEXT >= 8)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 8) ? 31
                                                      :
#endif
#if (PEXT >= 9)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 9) ? 32
                                                      :
#endif
#if (PEXT >= 10)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 10) ? 33
                                                      :
#endif
#if (PEXT >= 11)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 11) ? 34
                                                      :
#endif
#if (PEXT >= 12)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 12) ? 35
                                                      :
#endif
#if (PEXT >= 13)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 13) ? 36
                                                      :
#endif
#if (PEXT >= 14)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 14) ? 37
                                                      :
#endif
#if (PEXT >= 15)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 15) ? 38
                                                      :
#endif
#if (PEXT >= 16)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 16) ? 39
                                                      :
#endif
#if (PEXT >= 17)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 17) ? 40
                                                      :
#endif
#if (PEXT >= 18)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 18) ? 41
                                                      :
#endif
#if (PEXT >= 19)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 19) ? 42
                                                      :
#endif
#if (PEXT >= 20)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 20) ? 43
                                                      :
#endif
#if (PEXT >= 21)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 21) ? 44
                                                      :
#endif
#if (PEXT >= 22)
                                                      (ex2_d.frac & 0x0000001LL << PEXT - 22) ? 45
                                                                                              :
#endif
                                                                                              24 + PEXT;
    if (info) {
        printf("//ex2:%x %x %08.8x_%08.8x ", ex2_d.s, ex2_d.exp, (Uint)((Ull)ex2_d.frac >> 32), (Uint)ex2_d.frac);
    }

    if (ex2_d.nan) {
        ex3_d.s = 1;
        ex3_d.frac = 0x400000;
        ex3_d.exp = 0xff;

    } else if (ex2_d.inf) {
        ex3_d.s = ex2_d.s;
        ex3_d.frac = 0x000000;
        ex3_d.exp = 0xff;
    } else if (ex3_w.lzc == 62) {
        if (info) {
            printf("lzc==%d\n", ex3_w.lzc);
        }
        if (ex2_d.exp >= 253) {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = 0x000000;
            ex3_d.exp = 0xff;
        } else {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = ex2_d.frac >> (2 + PEXT); // ■■■ガード対応必要
            ex3_d.exp = ex2_d.exp + 2;
        }
    } else if (ex3_w.lzc == 63) {
        if (info) {
            printf("lzc==%d\n", ex3_w.lzc);
        }
        if (ex2_d.exp >= 254) {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = 0x000000;
            ex3_d.exp = 0xff;
        } else {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = ex2_d.frac >> (1 + PEXT); // ■■■ガード対応必要
            ex3_d.exp = ex2_d.exp + 1;
        }
    } else if (ex3_w.lzc <= (23 + PEXT)) { // ■■■
        if (info) {
            printf("lzc==%d\n", ex3_w.lzc);
        }
        if (ex2_d.exp >= ex3_w.lzc + 255) {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = 0x000000;
            ex3_d.exp = 0xff;
        } else if (ex2_d.exp <= ex3_w.lzc) { /* subnormal num */
            ex3_d.s = ex2_d.s;
            ex3_d.frac = (ex2_d.frac << ex2_d.exp) >> PEXT; // ■■■
            ex3_d.exp = 0x00;
        } else { /* normalized num */
            ex3_d.s = ex2_d.s;
            ex3_d.frac = (ex2_d.frac << ex3_w.lzc) >> PEXT; // ■■■
            ex3_d.exp = ex2_d.exp - ex3_w.lzc;
        }
#define NO_GUARD_BITS
#ifndef NO_GUARD_BITS
        int f_ulp = (ex2_d.frac << ex3_w.lzc) >> PEXT & 1;
        int f_g = (ex2_d.frac << ex3_w.lzc) >> (PEXT - 1) & 1;
        int f_r = (ex2_d.frac << ex3_w.lzc) >> (PEXT - 2) & 1;
    int f_s   =((ex2_d.frac<<ex3_w.lzc)&(0xfffffffffffLL>>(46-PEXT))!=0;
    switch (f_ulp<<3|f_g<<2|f_r<<1|f_s) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4: /* ulp|G|R|S */
        case 8:
        case 9:
        case 10:
        case 11:
            break;
        case 5:
        case 6:
        case 7: /* ulp++ */
        case 12:
        case 13:
        case 14:
        case 15:
        default:
            if (info)
                printf("//ex3:%x %x %x++ -> ", ex3_d.s, ex3_d.exp, ex3_d.frac);
            ex3_d.frac++;
            if (info)
                printf("%x\n", ex3_d.frac);
            break;
    }
#endif
    } else { /* zero */
        if (info) {
            printf("zero\n");
        }
        ex3_d.s = 0;
        ex3_d.frac = 0x000000;
        ex3_d.exp = 0x00;
    }
#endif

    if (info) {
        printf("//ex3:%x %x %x\n", ex3_d.s, ex3_d.exp, ex3_d.frac);
    }

    out.raw.w = (ex3_d.s << 31) | (ex3_d.exp << 23) | (ex3_d.frac);
    org.flo.w = i1 + i2 * i3;
    Uint diff = out.raw.w > org.raw.w ? out.raw.w - org.raw.w : org.raw.w - out.raw.w;

    // if (!info)
    //     sprintf(hardbuf32, "%8.8e:%08.8x %8.8e:%08.8x %8.8e:%08.8x ->%8.8e:%08.8x (%8.8e:%08.8x) %08.8x %s%s%s",
    //             in1.flo.w, in1.raw.w, in2.flo.w, in2.raw.w, in3.flo.w, in3.raw.w, out.flo.w, out.raw.w, org.flo.w, org.raw.w, diff,
    //             diff >= TH1 ? "H" : "",
    //             diff >= TH2 ? "H" : "",
    //             diff >= TH3 ? "H" : "");
    *o = out.flo.w;

    if (testbench) {
        printf("CHECK_FPU(32'h%08.8x,32'h%08.8x,32'h%08.8x,32'h%08.8x);\n", in1.raw.w, in2.raw.w, in3.raw.w, out.raw.w);
    }

    return (diff);
}

void main() {
    float a = 1.0, b = 1.0, c = 1.0;
    float *ans;
    hard32(1, a, b, c, &ans, 0);
    printf("%f", ans);
}