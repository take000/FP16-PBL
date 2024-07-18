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
        Uint w;
    } flo;
    struct base {
        Uint frac : 10;
        Uint exp : 5;
        Uint s : 1;
    } base;
} in1, in2, in3, out, org;

void radix4(Uint *pp, Uint *ps, Uint a, Uint b) {
    printf("\na:%x, b:%x", a, b);
    switch (b) {
    case 0:
        *pp = 0;
        *ps = 0;
        break;
    case 1:
        *pp = a & 0xfff;
        *ps = 0;
        break;
    case 2:
        *pp = a & 0xfff;
        *ps = 0;
        break;
    case 3:
        *pp = a << 1 & 0xfff;
        *ps = 0;
        break;
    case 4:
        *pp = ~(a << 1) & 0xfff;
        *ps = 1;
        break;
    case 5:
        *pp = ~a & 0xfff;
        *ps = 1;
        break;
    case 6:
        *pp = ~a & 0xfff;
        *ps = 1;
        break;
    default:
        *pp = ~0 & 0xfff;
        *ps = 1;
        break;
    }
}

void partial_product(Uint *pp, Uint *ps, Uint a, Uint b, Uint pos) {
    Uint tp, ts;

    radix4(&tp, &ts, a, b);
    printf("\ntp:%x, ts:%x", tp, ts);
    switch (pos) {
    case 0:
        *pp = ((~ts & 1) << 14) | (ts << 13) | (ts << 12) | tp;
        printf("\npp:%x", ((~ts & 1) << 14) | (ts << 13) | (ts << 12) | tp);
        *ps = ts;
        break;
    case 1:
    case 2:
    case 3:
        *pp = (1 << 13) | ((~ts & 1) << 12) | tp;
        printf("\npp:%x", (1 << 13) | ((~ts & 1) << 12) | tp);
        *ps = ts;
        break;
    case 4:
        *pp = ((~ts & 1) << 12) | tp;
        printf("\npp:%x", ((~ts & 1) << 12) | tp);
        *ps = ts;
        break;
    default:
        *pp = tp;
        printf("\npp:%x", tp);
        *ps = ts;
        break;
    }
}

void csa_line(Ull *co, Ull *s, Ull a, Ull b, Ull c) {
    *s = a ^ b ^ c;
    *co = ((a & b) | (b & c) | (c & a)) << 1;
}

int hard16(Uint i1, Uint i2, Uint i3, short *o, Uint debug) {
    int op = 3;
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

    Uint tp;
    Uint ps[12]; /* partial_sign */
    Ull pp[12];  /* partial_product */
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
        Ull csa_s : 12 + PEXT;
        Ull csa_c : 12 + PEXT;
        Uint exp : 6;
        Uint s : 1;
    } ex1_d; /* csa */

    struct fadd_s {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac : 12 + PEXT; /* aligned to ex1_d */
        Uint exp : 6;
        Uint s : 1;
    } fadd_s1;

    struct fadd_w {
        Uint exp_comp : 1;
        Uint exp_diff : 6;
        Uint align_exp : 6;
        Ull s1_align_frac : 12 + PEXT;
        Ull s2_align_frac : 12 + PEXT;
        Ull s3_align_frac : 12 + PEXT;
    } fadd_w;

    struct ex2_d {
        Uint nan : 1;
        Uint inf : 1;
        Uint zero : 1;
        Ull frac0 : 13 + PEXT; /* 12bit */
        Ull frac1 : 12 + PEXT; /* 11bit */
        Ull frac2 : 13 + PEXT; /* 12bit */
        Ull frac : 13 + PEXT;  /* 12bit */
        Uint exp : 6;
        Uint s : 1;
    } ex2_d;

    struct ex3_w {
        Uint lzc : 5;
    } ex3_w;

    struct ex3_d {
        Uint frac : 10;
        Uint exp : 5;
        Uint s : 1;
    } ex3_d;

    s1.s = (op == 1) ? 0 : in1.base.s;
    s1.exp = (op == 1) ? 0 : in1.base.exp;
    s1.frac = (op == 1) ? 0 : (in1.base.exp == 0) ? (0 << 10) | in1.base.frac
                                                  : (1 << 10) | in1.base.frac;
    s1.zero = (op == 1) ? 1 : (in1.base.exp == 0) && (in1.base.frac == 0);
    s1.inf = (op == 1) ? 0 : (in1.base.exp == 31) && (in1.base.frac == 0);
    s1.nan = (op == 1) ? 0 : (in1.base.exp == 31) && (in1.base.frac != 0);
    s2.s = in2.base.s;
    s2.exp = in2.base.exp;
    s2.frac = (in2.base.exp == 0) ? (0 << 10) | in2.base.frac : (1 << 10) | in2.base.frac;
    s2.zero = (in2.base.exp == 0) && (in2.base.frac == 0);
    s2.inf = (in2.base.exp == 31) && (in2.base.frac == 0);
    s2.nan = (in2.base.exp == 31) && (in2.base.frac != 0);
    s3.s = (op == 2) ? 0 : in3.base.s;
    s3.exp = (op == 2) ? 15 : in3.base.exp;
    s3.frac = (op == 2) ? (1 << 10) : (in3.base.exp == 0) ? (0 << 10) | in3.base.frac
                                                          : (1 << 10) | in3.base.frac;
    s3.zero = (op == 2) ? 0 : (in3.base.exp == 0) && (in3.base.frac == 0);
    s3.inf = (op == 2) ? 0 : (in3.base.exp == 31) && (in3.base.frac == 0);
    s3.nan = (op == 2) ? 0 : (in3.base.exp == 31) && (in3.base.frac != 0);

    printf("\ns1.zero: %x , s1.inf: %x , s1.nan: %x , s1.s: %x , s1.exp: %x , s1.frac: %x \n", s1.zero, s1.inf, s1.nan, s1.s, s1.exp, s1.frac);
    printf("\ns2.zero: %x , s2.inf: %x , s2.nan: %x , s2.s: %x , s2.exp: %x , s2.frac: %x \n", s2.zero, s2.inf, s2.nan, s2.s, s2.exp, s2.frac);
    printf("\ns3.zero: %x , s3.inf: %x , s3.nan: %x , s3.s: %x , s3.exp: %x , s3.frac: %x \n", s3.zero, s3.inf, s3.nan, s3.s, s3.exp, s3.frac);

    // org.flo.w = in1.flo.w + in2.flo.w * in3.flo.w;
    // if (debug) {
    //     printf("//--hard16--\n");
    //     printf("//s1: %04.4x %f\n", in1.raw.w, in1.flo.w);
    //     printf("//s2: %04.4x %f\n", in2.raw.w, in2.flo.w);
    //     printf("//s3: %04.4x %f\n", in3.raw.w, in3.flo.w);
    //     printf("//d : %04.4x %f\n", org.raw.w, org.flo.w);
    // }

    /* nan  * any  -> nan */
    /* inf  * zero -> nan */
    /* inf  * (~zero & ~nan) -> inf */
    /* zero * (~inf  & ~nan) -> zero */
    ex1_d.s = s2.s ^ s3.s;
    ex1_d.exp = ((0 << 5) | s2.exp) + ((0 << 5) | s3.exp) < 15 ? 0 : ((0 << 5) | s2.exp) + ((0 << 5) | s3.exp) - 15;

    /**************************************************************************************************************/
    /***  partial product  ****************************************************************************************/
    /**************************************************************************************************************/
    /*ex1_d.frac = (Ull)s2.frac * (Ull)s3.frac;*/
    partial_product(&tp, &ps[0], s2.frac, (s3.frac << 1) & 7, 0);
    pp[0] = (Ull)tp; // if (debug) {printf("pp[ 0]=%04.4x_%08.8x ps[ 0]=%d\n", (Uint)(pp[ 0]>>32), (Uint)pp[ 0], ps[ 0]);} /*1,0,-1*/
    partial_product(&tp, &ps[1], s2.frac, (s3.frac >> 1) & 7, 1);
    pp[1] = ((Ull)tp << 2) | (Ull)ps[0]; // if (debug) {printf("pp[ 1]=%04.4x_%08.8x ps[ 1]=%d\n", (Uint)(pp[ 1]>>32), (Uint)pp[ 1], ps[ 1]);} /*3,2, 1*/
    partial_product(&tp, &ps[2], s2.frac, (s3.frac >> 3) & 7, 2);
    pp[2] = ((Ull)tp << 4) | ((Ull)ps[1] << 2); // if (debug) {printf("pp[ 2]=%04.4x_%08.8x ps[ 2]=%d\n", (Uint)(pp[ 2]>>32), (Uint)pp[ 2], ps[ 2]);} /*5,4, 3*/
    partial_product(&tp, &ps[3], s2.frac, (s3.frac >> 5) & 7, 3);
    pp[3] = ((Ull)tp << 6) | ((Ull)ps[2] << 4); // if (debug) {printf("pp[ 3]=%04.4x_%08.8x ps[ 3]=%d\n", (Uint)(pp[ 3]>>32), (Uint)pp[ 3], ps[ 3]);} /*7,6, 5*/
    partial_product(&tp, &ps[4], s2.frac, (s3.frac >> 7) & 7, 4);
    pp[4] = ((Ull)tp << 8) | ((Ull)ps[3] << 6); // if (debug) {printf("pp[ 4]=%04.4x_%08.8x ps[ 4]=%d\n", (Uint)(pp[ 4]>>32), (Uint)pp[ 4], ps[ 4]);} /*9,8, 7*/
    partial_product(&tp, &ps[5], s2.frac, (s3.frac >> 9) & 7, 5);
    pp[5] = ((Ull)tp << 10) | ((Ull)ps[4] << 8); // if (debug) {printf("pp[ 5]=%04.4x_%08.8x ps[ 5]=%d\n", (Uint)(pp[ 5]>>32), (Uint)pp[ 5], ps[ 5]);} /*11,10,9*/

    Ull x1 = (pp[0] + pp[1] + pp[2] + pp[3] + pp[4] + pp[5]);
    printf("//x1(sum of pp)=%08.8x_%08.8x ->>10 %08.8x\n", (Uint)(x1 >> 16), (Uint)x1, (Uint)(x1 >> 10));
    printf("\n%llx\n", x1);
    printf("%x", ex1_d.exp);
    Ull x2 = (Ull)s2.frac * (Ull)s3.frac;
    printf("//x2(s2 * s3)  =%08.8x_%08.8x ->>10 %08.8x\n", (Uint)(x2 >> 16), (Uint)x2, (Uint)(x2 >> 10));
    printf("\n%llx\n", x2);

    // if (debug) {
    //     printf("//x1(sum of pp)=%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x1 >> 32), (Uint)x1, (Uint)(x1 >> 23));
    // }
    // Ull x2 = (Ull)s2.frac * (Ull)s3.frac;
    // if (debug) {
    //     printf("//x2(s2 * s3)  =%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x2 >> 32), (Uint)x2, (Uint)(x2 >> 23));
    // }

    /**************************************************************************************************************/
    /***  csa tree  ***********************************************************************************************/
    /**************************************************************************************************************/
    csa_line(&C1[0], &S1[0], pp[0], pp[1], pp[2]);
    csa_line(&C1[1], &S1[1], pp[3], pp[4], pp[5]);

    printf("\nS1[0]==%llx\n", (Ull)S1[0]);
    printf("\nS1[1]==%llx\n", (Ull)S1[1]);

    printf("\nC1[0]==%llx\n", (Ull)C1[0]);
    printf("\nC1[1]==%llx\n", (Ull)C1[1]);

    csa_line(&C2[0], &S2[0], S1[0], C1[0], S1[1]);
    csa_line(&C2[1], &S2[1], C1[1], 0, 0);

    csa_line(&C3[0], &S3[0], S2[0], C2[0], S2[1]);
    csa_line(&C3[1], &S3[1], C2[1], 0, 0);

    csa_line(&C4, &S4, S3[0], C3[0], S3[1]);
    csa_line(&C5, &S5, S4, C4, C3[1]);

    ex1_d.csa_s = S5; // sum
    ex1_d.csa_c = C5; // carry

    ex1_d.zero = (s2.zero && !s3.inf && !s3.nan) || (s3.zero && !s2.inf && !s2.nan);
    ex1_d.inf = (s2.inf && !s3.zero && !s3.nan) || (s3.inf && !s2.zero && !s2.nan);
    ex1_d.nan = s2.nan || s3.nan || (s2.inf && s3.zero) || (s3.inf && s2.zero);

    printf("\nex1_d.zero: %x , ex1_d.inf: %x , ex1_d.nan: %x , ex1_d.s: %x , ex1_d.exp: %x , ex1_d.csa_c: %x , ex1_d.csa_s: %x \n", ex1_d.zero, ex1_d.inf, ex1_d.nan, ex1_d.s, ex1_d.exp, ex1_d.csa_c, ex1_d.csa_s);

    // if (debug) {
    //     printf("//S5           =%08.8x_%08.8x\n", (Uint)(S5 >> 32), (Uint)S5);
    //     printf("//C5           =%08.8x_%08.8x\n", (Uint)(C5 >> 32), (Uint)C5);
    //     printf("//++(48bit)    =%08.8x_%08.8x\n", (Uint)((C5 + S5) >> 32), (Uint)(C5 + S5));
    //     printf("//csa_s        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_s >> 32), (Uint)ex1_d.csa_s);
    //     printf("//csa_c        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_c >> 32), (Uint)ex1_d.csa_c);
    //     printf("//ex1_d: %x %02.2x +=%08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)(ex1_d.csa_c + ex1_d.csa_s) >> 32), (Uint)(ex1_d.csa_c + ex1_d.csa_s));
    // }

    /**************************************************************************************************************/
    /***  3in-csa  ************************************************************************************************/
    /**************************************************************************************************************/
    fadd_s1.s = s1.s;
    fadd_s1.exp = (0 < s1.exp && s1.exp < 31) ? (s1.exp - 1) : s1.exp;
    fadd_s1.frac = (0 < s1.exp && s1.exp < 31) ? (Ull)s1.frac << (10 + 1) : (Ull)s1.frac << 10;
    fadd_s1.zero = s1.zero;
    fadd_s1.inf = s1.inf;
    fadd_s1.nan = s1.nan;

    printf("\nfadd_s1.zero: %x , fadd_s1.inf: %x , fadd_s1.nan: %x , fadd_s1.s: %x , fadd_s1.exp: %x , fadd_s1.frac: %x \n", fadd_s1.zero, fadd_s1.inf, fadd_s1.nan, fadd_s1.s, fadd_s1.exp, fadd_s1.frac);

    /* nan  + any  -> nan */
    /* inf  + -inf -> nan */
    /* inf  + (~-inf & ~nan) -> inf */
    /* -inf + (~inf  & ~nan) -> inf */
    fadd_w.exp_comp = fadd_s1.exp > ex1_d.exp ? 1 : 0;
    fadd_w.exp_diff = fadd_w.exp_comp ? (fadd_s1.exp - ex1_d.exp) : (ex1_d.exp - fadd_s1.exp);
    if (fadd_w.exp_diff > 22)
        fadd_w.exp_diff = 22;
    fadd_w.align_exp = fadd_w.exp_comp ? fadd_s1.exp : ex1_d.exp;
    fadd_w.s1_align_frac = fadd_s1.frac >> (fadd_w.exp_comp ? 0 : fadd_w.exp_diff);
    fadd_w.s2_align_frac = (Ull)ex1_d.csa_s >> (ex1_d.zero ? 22 : fadd_w.exp_comp ? fadd_w.exp_diff
                                                                                  : 0);
    fadd_w.s3_align_frac = (Ull)ex1_d.csa_c >> (ex1_d.zero ? 22 : fadd_w.exp_comp ? fadd_w.exp_diff
                                                                                  : 0);
    // if (debug) {
    //     printf("//fadd_s1: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s1.s, fadd_s1.exp, (Uint)((Ull)fadd_s1.frac >> 32), (Uint)fadd_s1.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s1_align_frac >> 32), (Uint)fadd_w.s1_align_frac);
    //     printf("//csa_s: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_s >> 32), (Uint)ex1_d.csa_s, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s2_align_frac >> 32), (Uint)fadd_w.s2_align_frac);
    //     printf("//csa_c: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_c >> 32), (Uint)ex1_d.csa_c, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s3_align_frac >> 32), (Uint)fadd_w.s3_align_frac);
    // }

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

    // if (debug) {
    //     printf("//C6[0]=%08.8x_%08.8x(a+c+s)\n", (Uint)(C6[0] >> 32), (Uint)C6[0]);
    //     printf("//S6[0]=%08.8x_%08.8x(a+c+s)\n", (Uint)(S6[0] >> 32), (Uint)S6[0]);
    //     printf("//C6[1]=%08.8x_%08.8x(a-c-s)\n", (Uint)(C6[1] >> 32), (Uint)C6[1]);
    //     printf("//S6[1]=%08.8x_%08.8x(a-c-s)\n", (Uint)(S6[1] >> 32), (Uint)S6[1]);
    //     printf("//C7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(C7[1] >> 32), (Uint)C7[1]);
    //     printf("//S7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(S7[1] >> 32), (Uint)S7[1]);
    //     printf("//C6[2]=%08.8x_%08.8x(c+s-a)\n", (Uint)(C6[2] >> 32), (Uint)C6[2]);
    //     printf("//S6[2]=%08.8x_%08.8x(c+s-a)\n", (Uint)(S6[2] >> 32), (Uint)S6[2]);
    //     printf("//C7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(C7[2] >> 32), (Uint)C7[2]);
    //     printf("//S7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(S7[2] >> 32), (Uint)S7[2]);
    // }

    /**************************************************************************************************************/
    /***  2in-add  ************************************************************************************************/
    /**************************************************************************************************************/
    ex2_d.frac0 = C6[0] + S6[0]; /* 12bit */
    ex2_d.frac1 = C7[1] + S7[1]; /* 11bit */
    ex2_d.frac2 = C7[2] + S7[2]; /* 12bit */

    // if (debug) {
    //     printf("//ex2_d.frac0=%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac0 >> 32), (Uint)ex2_d.frac0);
    //     printf("//ex2_d.frac1=%08.8x_%08.8x(a-c-s)\n", (Uint)((Ull)ex2_d.frac1 >> 32), (Uint)ex2_d.frac1);
    //     printf("//ex2_d.frac2=%08.8x_%08.8x(c+s-a)\n", (Uint)((Ull)ex2_d.frac2 >> 32), (Uint)ex2_d.frac2);
    // }

    ex2_d.s = (fadd_s1.s == ex1_d.s) ? fadd_s1.s : (ex2_d.frac2 & 0x400000LL) ? fadd_s1.s
                                                                              : ex1_d.s;
    ex2_d.exp = fadd_w.align_exp;
    ex2_d.frac = (fadd_s1.s == ex1_d.s) ? ex2_d.frac0 : (ex2_d.frac2 & 0x400000LL) ? ex2_d.frac1
                                                                                   : ex2_d.frac2 & 0xffffffffffffLL;
    /* 12bit */
    ex2_d.zero = ex2_d.frac == 0;
    ex2_d.inf = (!fadd_s1.s && fadd_s1.inf && !(ex1_d.s && ex1_d.inf) && !ex1_d.nan) || (fadd_s1.s && fadd_s1.inf && !(!ex1_d.s && ex1_d.inf) && !ex1_d.nan) || (!ex1_d.s && ex1_d.inf && !(fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan) || (ex1_d.s && ex1_d.inf && !(!fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan);
    ex2_d.nan = fadd_s1.nan || ex1_d.nan;
    printf("\nex2_d.zero: %x , ex2_d.inf: %x , ex2_d.nan: %x , ex2_d.s: %x , ex2_d.exp: %x , ex2_d.frac: %x , ex2_d.frac0: %x , ex2_d.frac1: %x , ex2_d.frac2: %x\n", ex2_d.zero, ex2_d.inf, ex2_d.nan, ex2_d.s, ex2_d.exp, ex2_d.frac, ex2_d.frac0, ex2_d.frac1, ex2_d.frac2);

    // if (debug) {
    //     printf("//ex2_d.frac =%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac >> 32), (Uint)ex2_d.frac);
    // }
#define FLOAT_PZERO 0x0000
#define FLOAT_NZERO 0x8000
#define FLOAT_PINF 0x7f80
#define FLOAT_NINF 0xff80
#define FLOAT_NAN 0xffc0

    /**************************************************************************************************************/
    /***  normalize  **********************************************************************************************/
    /**************************************************************************************************************/
#if 1
    ex3_w.lzc = (ex2_d.frac & 0x400000LL)   ? 30
                : (ex2_d.frac & 0x200000LL) ? 31
                : (ex2_d.frac & 0x100000LL) ? 0
                : (ex2_d.frac & 0x080000LL) ? 1
                : (ex2_d.frac & 0x040000LL) ? 2
                : (ex2_d.frac & 0x020000LL) ? 3
                : (ex2_d.frac & 0x010000LL) ? 4
                : (ex2_d.frac & 0x008000LL) ? 5
                : (ex2_d.frac & 0x004000LL) ? 6
                : (ex2_d.frac & 0x002000LL) ? 7
                : (ex2_d.frac & 0x001000LL) ? 8
                : (ex2_d.frac & 0x000800LL) ? 9
                : (ex2_d.frac & 0x000400LL) ? 10
                : (ex2_d.frac & 0x000200LL) ? 11
                : (ex2_d.frac & 0x000100LL) ? 12
                : (ex2_d.frac & 0x000080LL) ? 13
                : (ex2_d.frac & 0x000040LL) ? 14
                : (ex2_d.frac & 0x000020LL) ? 15
                : (ex2_d.frac & 0x000010LL) ? 16
                : (ex2_d.frac & 0x000008LL) ? 17
                : (ex2_d.frac & 0x000004LL) ? 18
                : (ex2_d.frac & 0x000002LL) ? 19
                : (ex2_d.frac & 0x000001LL) ? 20
                                            : 21;
    // if (debug) {
    //     printf("//ex2:%x %x %08.8x_%08.8x ", ex2_d.s, ex2_d.exp, (Uint)((Ull)ex2_d.frac >> 32), (Uint)ex2_d.frac);
    // }
    printf("\nlzc==%d\n", ex3_w.lzc);
    if (ex2_d.nan) {
        ex3_d.s = 1;
        ex3_d.frac = 0x200;
        ex3_d.exp = 0x1f;

    } else if (ex2_d.inf) {
        ex3_d.s = ex2_d.s;
        ex3_d.frac = 0x000;
        ex3_d.exp = 0x1f;
    } else if (ex3_w.lzc == 30) {
        if (debug) {
            printf("lzc==%d\n", ex3_w.lzc);
        }
        if (ex2_d.exp >= 29) {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = 0x000;
            ex3_d.exp = 0x1f;
        } else {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = (ex2_d.frac >> 2) >> 10; // ★★★ガード対応必要
            ex3_d.exp = ex2_d.exp + 2;
        }
    } else if (ex3_w.lzc == 31) {
        if (debug) {
            printf("lzc==%d\n", ex3_w.lzc);
        }
        if (ex2_d.exp >= 30) {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = 0x000;
            ex3_d.exp = 0x1f;
        } else {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = (ex2_d.frac >> 1) >> 10; // ★★★ガード対応必要
            ex3_d.exp = ex2_d.exp + 1;
        }
    } else if (ex3_w.lzc <= 20) { // ■■■
        if (debug) {
            printf("lzc==%d\n", ex3_w.lzc);
        }
        if (ex2_d.exp >= ex3_w.lzc + 31) {
            ex3_d.s = ex2_d.s;
            ex3_d.frac = 0x000;
            ex3_d.exp = 0x1f;
        } else if (ex2_d.exp <= ex3_w.lzc) { /* subnormal num */
            ex3_d.s = ex2_d.s;
            ex3_d.frac = (ex2_d.frac << ex2_d.exp) >> 10; // ■■■
            ex3_d.exp = 0x00;
        } else { /* normalized num */
            ex3_d.s = ex2_d.s;
            ex3_d.frac = (ex2_d.frac << ex3_w.lzc) >> 10; // ■■■
            ex3_d.exp = ex2_d.exp - ex3_w.lzc;
        }
#define NO_GUARD_BITS
#ifndef NO_GUARD_BITS
        int f_ulp = (ex2_d.frac << (32 - ex3_w.lzc)) >> 31 & 1;
        int f_g = (ex2_d.frac << (31 - ex3_w.lzc)) >> 31 & 1;
        int f_r = (ex2_d.frac << (30 - ex3_w.lzc)) >> 31 & 1;
        int f_s = ((ex2_d.frac << (32 - ex3_w.lzc)) & 0xffffffff) != 0;
        switch (f_ulp << 3 | f_g << 2 | f_r << 1 | f_s) {
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
            if (debug)
                printf("//ex3:%x %x %x++ -> ", ex3_d.s, ex3_d.exp, ex3_d.frac);
            ex3_d.frac++;
            if (debug)
                printf("%x\n", ex3_d.frac);
            break;
        }
#endif
    } else { /* zero */
        // if (debug) {
        //     printf("zero\n");
        // }
        ex3_d.s = 0;
        ex3_d.frac = 0x000;
        ex3_d.exp = 0x00;
    }
#endif

    // if (debug) {
    //     printf("//ex3:%x %x %x\n", ex3_d.s, ex3_d.exp, ex3_d.frac);
    // }

    out.raw.w = (ex3_d.s << 15) | (ex3_d.exp << 10) | (ex3_d.frac);
    org.flo.w = i1 + i2 * i3;
    printf("\n\n%x", out.raw.w);
    /*
    Uint diff = out.raw.w > org.raw.w ? out.raw.w - org.raw.w : org.raw.w - out.raw.w;

    if (!debug)
        sprintf(hardbuf32, "%8.8e:%08.8x %8.8e:%08.8x %8.8e:%08.8x ->%8.8e:%08.8x (%8.8e:%08.8x) %08.8x %s%s%s",
                in1.flo.w, in1.raw.w, in2.flo.w, in2.raw.w, in3.flo.w, in3.raw.w, out.flo.w, out.raw.w, org.flo.w, org.raw.w, diff,
                diff >= TH1 ? "H" : "",
                diff >= TH2 ? "H" : "",
                diff >= TH3 ? "H" : "");
    */

    *o = out.flo.w;
    /*
    if (testbench) {
        printf("CHECK_FPU(32'h%08.8x,32'h%08.8x,32'h%08.8x,32'h%08.8x);\n", in1.raw.w, in2.raw.w, in3.raw.w, out.raw.w);
    }
    */
    // return (diff);
}

int main() {
    short ans;

    // printf("%x\n", 0b0100000000000000);
    // hard16(0b0011110000000000, 0b0100000000000000, 0b0100001000000000, &ans, 1);

    // printf("\n%x\n\n", 0b0011110000000000);

    // hard16(0b0100011000000000, 0b0100010100000000, 0b0100010000000000, &ans, 0);
    // hard16(0b0011110000000000, 0b0100000000000000, 0b0100001000000000, &ans, 0); // 1*2+3
    hard16(0b0011110000000000, 0b0011110000000000, 0b0011110000000000, &ans, 0); // 1*1+1
    // hard16(0b0000000000000000, 0b0000000000000000, 0b0000000000000000, &ans, 0); // 0*0+0
    // hard16(0b0100000000000000, 0b0100000000000000, 0b0100000000000000, &ans, 0); // 2*2+2
    printf("\n\n%x", ans);

    return 0;
}