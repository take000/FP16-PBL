
/* 逆行列計算を使った,fpu精度テスト                      */
/*                          Copyright (C) 2013- by NAIST */
/*                           Primary writer: Y.Nakashima */
/*                                  nakashim@is.naist.jp */

/* 1<= PEXT <= 23 */
#define PEXT 1
//#define PEXT 23

#ifndef UTYPEDEF
#define UTYPEDEF
typedef unsigned char      Uchar;
typedef unsigned short     Ushort;
typedef unsigned int       Uint;
typedef unsigned long long Ull;
typedef long long int      Sll;
#if __AARCH64EL__ == 1
typedef long double Dll;
#else
typedef struct {Ull u[2];} Dll;
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#ifndef ARMSIML
#include <unistd.h>
#include <sys/times.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xdbe.h>
#endif

Ull nanosec_sav;
Ull nanosec;
reset_nanosec()
{
  int i;
  nanosec = 0;
  struct timespec ts;
  clock_gettime(0, &ts); /*CLOCK_REALTIME*/
  nanosec_sav = 1000000000*ts.tv_sec + ts.tv_nsec;
}
get_nanosec(int class)
{
  Ull nanosec_now;
  struct timespec ts;
  clock_gettime(0, &ts); /*CLOCK_REALTIME*/
  nanosec_now = 1000000000*ts.tv_sec + ts.tv_nsec;
  nanosec += nanosec_now - nanosec_sav;
  nanosec_sav = nanosec_now;
}
show_nanosec()
{
  printf("nanosec: ARM:%llu\n", nanosec);
}

#if !defined(ARMSIML)
/***********/
/* for X11 */
/***********/
Display              *disp;          /* display we're sending to */
int                  scrn;           /* screen we're sending to */

typedef struct {
  unsigned int  width;  /* width of image in pixels */
  unsigned int  height; /* height of image in pixels */
  unsigned char *data;  /* data rounded to full byte for each row */
} Image;

typedef struct {
  Display  *disp;       /* destination display */
  int       scrn;       /* destination screen */
  int       depth;      /* depth of drawable we want/have */
  int       dpixlen;    /* bitsPerPixelAtDepth */
  Drawable  drawable;   /* drawable to send image to */
  Colormap  cmap;       /* colormap used for image */
  GC        gc;         /* cached gc for sending image */
  XImage   *ximage;     /* ximage structure */
} XImageInfo;

union {
  XEvent              event;
  XAnyEvent           any;
  XButtonEvent        button;
  XExposeEvent        expose;
  XMotionEvent        motion;
  XResizeRequestEvent resize;
  XClientMessageEvent message;
} event;

unsigned int          redvalue[256], greenvalue[256], bluevalue[256];
XImageInfo            ximageinfo;
Image                 imageinfo;  /* image that will be sent to the display */
unsigned int          bitsPerPixelAtDepth();
void                  imageInWindow();
void                  bestVisual();

#define TRUE_RED(PIXVAL)      (((PIXVAL) & 0xff0000) >> 16)
#define TRUE_GREEN(PIXVAL)    (((PIXVAL) &   0xff00) >>  8)
#define TRUE_BLUE(PIXVAL)     (((PIXVAL) &     0xff)      )

void x11_open(int width, int height, int screen_wd, int screen_ht)
{
  if (!(disp = XOpenDisplay(NULL))) {
    printf("%s: Cannot open display\n", XDisplayName(NULL));
    exit(1);
  }
  scrn = DefaultScreen(disp);
  imageinfo.width = width*screen_wd;
  imageinfo.height= height*screen_ht;
  /*imageinfo.data  = malloc(sizeof(Uint)*imageinfo.width*imageinfo.height);*/
  imageInWindow(&ximageinfo, disp, scrn, &imageinfo);
}

void x11_update()
{
  XPutImage(ximageinfo.disp, ximageinfo.drawable, ximageinfo.gc,
            ximageinfo.ximage, 0, 0, 0, 0, imageinfo.width, imageinfo.height);
}

int x11_checkevent()
{
  static int stop = 0;

  x11_update();
  while (XPending(disp)) {
    XNextEvent(disp, &event.event);
    switch (event.any.type) {
    case KeyPress:
      stop = 1-stop;
      if   (stop) printf("-stopped- (type any key to continue)\n");
      else        printf("-running-\n");
      break;
    default:
      break;
    }
  }
  return (stop);
}

void x11_close()
{
  XCloseDisplay(disp);
}

void imageInWindow(ximageinfo, disp, scrn, image)
     XImageInfo   *ximageinfo;
     Display      *disp;
     int           scrn;
     Image        *image;
{
  Window                ViewportWin;
  Visual               *visual;
  unsigned int          depth;
  unsigned int          dpixlen;
  XSetWindowAttributes  swa_view;
  XSizeHints            sh;
  unsigned int pixval;
  unsigned int redcolors, greencolors, bluecolors;
  unsigned int redstep, greenstep, bluestep;
  unsigned int redbottom, greenbottom, bluebottom;
  unsigned int redtop, greentop, bluetop;
  XColor        xcolor;
  unsigned int  a;
  XGCValues gcv;

  bestVisual(disp, scrn, &visual, &depth);
  dpixlen = (bitsPerPixelAtDepth(disp, depth) + 7) / 8;

  ximageinfo->disp    = disp;
  ximageinfo->scrn    = scrn;
  ximageinfo->depth   = depth;
  ximageinfo->dpixlen = dpixlen;
  ximageinfo->drawable= None;
  ximageinfo->gc      = NULL;
  ximageinfo->ximage  = XCreateImage(disp, visual, depth, ZPixmap, 0,
                                     NULL, image->width, image->height,
                                     8, 0);
  ximageinfo->ximage->data= (char*)malloc(image->width * image->height * dpixlen);
  ximageinfo->ximage->byte_order= MSBFirst; /* trust me, i know what
                                             * i'm talking about */

  if (visual == DefaultVisual(disp, scrn))
    ximageinfo->cmap= DefaultColormap(disp, scrn);
  else
    ximageinfo->cmap= XCreateColormap(disp, RootWindow(disp, scrn), visual, AllocNone);

  redcolors= greencolors= bluecolors= 1;
  for (pixval= 1; pixval; pixval <<= 1) {
    if (pixval & visual->red_mask)
      redcolors <<= 1;
    if (pixval & visual->green_mask)
      greencolors <<= 1;
    if (pixval & visual->blue_mask)
      bluecolors <<= 1;
  }

  redtop   = 0;
  greentop = 0;
  bluetop  = 0;
  redstep  = 256 / redcolors;
  greenstep= 256 / greencolors;
  bluestep = 256 / bluecolors;
  redbottom= greenbottom= bluebottom= 0;
  for (a= 0; a < visual->map_entries; a++) {
    if (redbottom < 256)
      redtop= redbottom + redstep;
    if (greenbottom < 256)
      greentop= greenbottom + greenstep;
    if (bluebottom < 256)
      bluetop= bluebottom + bluestep;

    xcolor.flags= DoRed | DoGreen | DoBlue;
    xcolor.red  = (redtop - 1) << 8;
    xcolor.green= (greentop - 1) << 8;
    xcolor.blue = (bluetop - 1) << 8;
    XAllocColor(disp, ximageinfo->cmap, &xcolor);

    while ((redbottom < 256) && (redbottom < redtop))
      redvalue[redbottom++]= xcolor.pixel & visual->red_mask;
    while ((greenbottom < 256) && (greenbottom < greentop))
      greenvalue[greenbottom++]= xcolor.pixel & visual->green_mask;
    while ((bluebottom < 256) && (bluebottom < bluetop))
      bluevalue[bluebottom++]= xcolor.pixel & visual->blue_mask;
  }

  swa_view.background_pixel= WhitePixel(disp,scrn);
  swa_view.backing_store= WhenMapped;
  swa_view.cursor= XCreateFontCursor(disp, XC_watch);
  swa_view.event_mask= ButtonPressMask | Button1MotionMask | KeyPressMask |
    StructureNotifyMask | EnterWindowMask | LeaveWindowMask | ExposureMask;
  swa_view.save_under= False;
  swa_view.bit_gravity= NorthWestGravity;
  swa_view.save_under= False;
  swa_view.colormap= ximageinfo->cmap;
  swa_view.border_pixel= 0;
  ViewportWin= XCreateWindow(disp, RootWindow(disp, scrn), 0, 0,
                             image->width, image->height, 0,
                             DefaultDepth(disp, scrn), InputOutput,
                             DefaultVisual(disp, scrn),
                             CWBackingStore | CWBackPixel |
                             CWEventMask | CWSaveUnder,
                             &swa_view);
  ximageinfo->drawable= ViewportWin;

  gcv.function= GXcopy;
  ximageinfo->gc= XCreateGC(ximageinfo->disp, ximageinfo->drawable, GCFunction, &gcv);

  sh.width= image->width;
  sh.height= image->height;
  sh.min_width= image->width;
  sh.min_height= image->height;
  sh.max_width= image->width;
  sh.max_height= image->height;
  sh.width_inc= 1;
  sh.height_inc= 1;
  sh.flags= PMinSize | PMaxSize | PResizeInc | PSize;
  XSetNormalHints(disp, ViewportWin, &sh);

  XStoreName(disp, ViewportWin, "rsim");
  XMapWindow(disp, ViewportWin);
  XSync(disp,False);
}

void bestVisual(disp, scrn, rvisual, rdepth)
     Display       *disp;
     int            scrn;
     Visual       **rvisual;
     unsigned int  *rdepth;
{
  unsigned int  depth, a;
  Screen       *screen;
  XVisualInfo template, *info;
  int nvisuals;

  /* figure out the best depth the server supports.  note that some servers
   * (such as the HP 11.3 server) actually say they support some depths but
   * have no visuals that support that depth.  seems silly to me....
   */
  depth = 0;
  screen= ScreenOfDisplay(disp, scrn);
  for (a= 0; a < screen->ndepths; a++) {
    if (screen->depths[a].nvisuals &&
        ((!depth ||
          ((depth < 24) && (screen->depths[a].depth > depth)) ||
          ((screen->depths[a].depth >= 24) &&
           (screen->depths[a].depth < depth)))))
      depth= screen->depths[a].depth;
  }
  template.screen= scrn;
  template.class= TrueColor;
  template.depth= depth;
  if (! (info= XGetVisualInfo(disp, VisualScreenMask | VisualClassMask | VisualDepthMask, &template, &nvisuals)))
    *rvisual= NULL; /* no visuals of this depth */
  else {
    *rvisual= info->visual;
    XFree((char *)info);
  }
  *rdepth= depth;
}

unsigned int bitsPerPixelAtDepth(disp, depth)
     Display      *disp;
     unsigned int  depth;
{
  XPixmapFormatValues *xf;
  unsigned int nxf, a;

  xf = XListPixmapFormats(disp, (int *)&nxf);
  for (a = 0; a < nxf; a++)
    if (xf[a].depth == depth)
      return(xf[a].bits_per_pixel);

  fprintf(stderr, "bitsPerPixelAtDepth: Can't find pixmap depth info!\n");
  exit(1);
}
#endif

Uchar* membase;

sysinit(memsize, alignment) Uint memsize, alignment;
{
  membase = (void*)malloc(memsize+alignment);
  if ((int)membase & (alignment-1))
    membase = (void*)(((int)membase & ~(alignment-1))+alignment);
}

/* LMM:16KB, RMM:64KB: M/NCHIP=124 M/NCHIP/RMGRP=31 */
/*#define M 4096*/
#define M 128
#define RMGRP 1
/*#define NCHIP 4*/
#define NCHIP 1
/*#define W 1*/
#define H 1
volatile float *A0;  /*[M][M];*/
volatile float *A;   /*[M][M];*/
volatile Uint  *p;   /*[M];*/
volatile float *inv0;/*[M][M];*/
volatile float *inv1;/*[M][M];*/
volatile float *b;   /*[M][M];*/
volatile float *x;   /*[M][M];*/
volatile float *C;   /*[M][M];*/
int top, blk, h;
int count0, count1, count2;

#define MAXINT (~(1<<(sizeof(int)*8-1)))
#define ERRTH  (2.0E-2)
#define TH1 0x2
#define TH2 0xff
#define TH3 0xfff

#define abs(a)     ((a)>  0 ? (a) :-(a)    )
#define sub0(a, b) ((a)<=(b)? (0) : (a)-(b))
#define max(a, b)  ((a)>=(b)? (a) : (b)    )

int soft32(Uint, float, float, float, float *);
int hard32(Uint, float, float, float, float *, Uint);
int soft64(Uint, float, float, float, float *);
int hard64(Uint, float, float, float, float *);
char softbuf32[1024];
char hardbuf32[1024];
char softbuf64[1024];
char hardbuf64[1024];

#define  WD      M
#define  HT      M
#define  BITMAP  (WD*HT)
#define  SCRWD   4
#define  SCRHT   2

void BGR_to_X(int id, Uint *from)
{
  int i, j;
  Uint *to;

  to = (Uint*)(ximageinfo.ximage->data)+BITMAP*SCRWD*(id/SCRWD)+WD*(id%SCRWD);
  for (i=0; i<HT; i++,to+=WD*(SCRWD-1)) {
    for (j=0; j<WD; j++)
      *to++ = *from++;
  }
}

int main(int argc, char **argv)
{
  int i, j, k;
  int testbench = 0;

  for(argc--,argv++;argc;argc--,argv++){
    if(**argv == '-'){
      switch(*(*argv+1)){
      case 't':
	testbench = 1;
	break;
      default:
	fprintf(stderr, "Usage: ./fpu                 ... w/o testbench\n");
	fprintf(stderr, "       ./fpu -t > tb_fpu.dat ... w/  testbench\n");
	exit(1);
	break;
      }
    }
  }

  sysinit(M*M*sizeof(float)
         +(M+RMGRP)*M*sizeof(float)
         +(M+M)*sizeof(Uint) /*奇数では×*/
         +M*M*sizeof(float)
         +M*M*sizeof(float)
         +M*M*sizeof(float)
         +M*M*sizeof(float)
         +M*M*sizeof(float),32);
  printf("//membase: %08.8x\n", (Uint)membase);
  A0  = (float*)membase;
  A   = (float*)((Uchar*)A0  + M*M*sizeof(float));
  p   = (Uint*) ((Uchar*)A   +(M+RMGRP)*M*sizeof(float));
  inv0= (float*)((Uchar*)p   +(M+M)*sizeof(Uint));
  inv1= (float*)((Uchar*)inv0+ M*M*sizeof(float));
  b   = (float*)((Uchar*)inv1+ M*M*sizeof(float));
  x   = (float*)((Uchar*)b   + M*M*sizeof(float));
  C   = (float*)((Uchar*)x   + M*M*sizeof(float));
  printf("//A0  : %08.8x\n", A0);
  printf("//A   : %08.8x\n", A);
  printf("//p   : %08.8x\n", p);
  printf("//inv0: %08.8x\n", inv0);
  printf("//inv1: %08.8x\n", inv1);
  printf("//b   : %08.8x\n", b);
  printf("//x   : %08.8x\n", x);
  printf("//C   : %08.8x\n", C);

  x11_open(WD, HT, SCRWD, SCRHT); /*sh_video->disp_w, sh_video->disp_h, # rows of output_screen*/

  srand(100);
  /*  入力行列を作成  */
  for (i=0; i<M; i++) {
    for (j=0; j<M; j++)
#if 1
      A[i*M+j] = A0[i*M+j] = (float)(i%M+j);
#else
      A[i*M+j] = A0[i*M+j] = (float)(i%120+j);
#endif
  }
  A[0] = A0[0] = 1;
  for (j=1;j<M;j++)
    A[j*M+j] = A0[j*M+j] = 3;

  orig();

  for (i=0; i<M; i++) { for (j=0; j<M; j++) A[i*M+j] = A0[i*M+j]; }

  imax(testbench);

  BGR_to_X(0, (Uint*)A0);   /* oginal 32bit input */
  BGR_to_X(1, (Uint*)inv0); /* oginal  8bit input */
  BGR_to_X(2, (Uint*)inv1); /* 32bit FMA */

  /* 検算 */
  for (i=0; i<M; i++) {
    for (j=0; j<M; j++) {
      for (k=0; k<M; k++) {
        if (k==0) C[i*M+j]  = A0[i*M+k] * inv1[k*M+j];
        else      C[i*M+j] += A0[i*M+k] * inv1[k*M+j];
      }
      if (i == j && fabsf(C[i*M+j]-1.0)>ERRTH) {
        count2++;
        fprintf(stderr, "A*A'!=E C[%d][%d]=%f\n", i, j, C[i*M+j]);
      }
      else if (i != j && (fabsf(C[i*M+j])>ERRTH)) {
        count2++;
        fprintf(stderr, "A*A'!=E C[%d][%d]=%f\n", i, j, C[i*M+j]);
      }
    }
  }
  if (count2)
    fprintf(stderr, "A*A'!=E (ERRTH=%f) Num of diffs: %d\n", ERRTH, count2);
  else
    fprintf(stderr, "A*A'==E (ERRTH=%f) Confirmed\n", ERRTH);

  fprintf(stderr, "==== Normal end. Type any in ImageWin ====\n");
  while (!x11_checkevent());
}

orig()
{
  int i, j, k;
  float pmax;

  printf("//<<<ORIG>>>\n");

  /* LU分解 */
  for (i=0; i<M+1; i++)
    p[i] = i;
  for (i=0; i<M; i++) {
    pmax = 0.0;
    k = -1;
    for (j=i; j<M; j++) {
      if (pmax < fabsf(A[p[j]*M+i])) {
        pmax = fabsf(A[p[j]*M+i]);
        k = j;
      }
    }
    if (k == -1) {
      printf("//can't solve\n");
      exit(1);
    }
    j = p[k]; p[k] = p[i]; p[i] = j;
    A[p[i]*M+i] = 1.0/A[p[i]*M+i];
    for (j=i+1; j<M; j++) {
      A[p[j]*M+i] *= A[p[i]*M+i];
      for (k=i+1; k<M; k++)
        A[p[j]*M+k] -= (float)A[p[j]*M+i]*(float)A[p[i]*M+k];
    }
  }

  /* 逆行列求める */
  for (i=0; i<M; i++) {
    for (j=0; j<M; j++)
      b[p[j]] = (i==j)?1.0:0.0;
    for (j=i+1; j<M; j++) { /* 逆行列(b[]=E)の場合,k<iではb[]==0なのでj=i+1から開始 */
      for (k=i; k<j; k++) /* 逆行列(b[]=E)の場合,k<iではb[]==0なのでk=iから開始 */
        b[p[j]] -= (float)A[p[j]*M+k]*(float)b[p[k]];
    }
    for (j=M-1; j>=0; j--) {
      for (k=M-1; k>j; k--)
        b[p[j]] -= A[p[j]*M+k]*x[k];
      inv0[j*M+p[i]] = x[j] = (float)b[p[j]]*(float)A[p[j]*M+j];
    }
  }
}

imax(Uint testbench)
{
  int i, j, k, diff1, diff2, diff3, diff4;
  float pmax, tmp;
  float tmp1, tmp2, tmp3, tmp4;

  printf("//<<<IMAX>>>\n");

  /* LU分解 */
  for (i=0; i<M+1; i++)
    p[i] = i;
  for (i=0; i<M; i++) {
    pmax = 0.0;
    k = -1;
    for (j=i; j<M; j++) {
      if (pmax < fabsf(A[p[j]*M+i])) {
        pmax = fabsf(A[p[j]*M+i]);
        k = j;
      }
    }
    if (k == -1) {
      printf("//can't solve\n");
      exit(1);
    }
    j = p[k]; p[k] = p[i]; p[i] = j;
    for (j=0; j<M; j++) { /* real pivotting */            /*★*/
      tmp = A[k*M+j]; A[k*M+j] = A[i*M+j]; A[i*M+j] = tmp;/*★*/
    }                                                     /*★*/
    A[i*M+i] = 1.0/A[i*M+i];
    for (j=i+1; j<M; j++) /* 行方向 */
      A[j*M+i] *= A[i*M+i];
    /* FPGA実機でj-loopの最終(len=1)が動かないので,ついでにARMのほうが速そうなlenをARMで実行 2019/3/1 Nakashima */
    for (j=i+1; j<M; j++) { /* 行方向 */
      for (k=i+1; k<M; k++) { /* 最内列方向 */
	/*A[j*M+k] -= A[j*M+i]*A[i*M+k];*/
        diff1 = soft32(0, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp1);
        diff2 = hard32(0, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp2, testbench);
        diff3 = soft64(0, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp3);
        diff4 = hard64(0, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp4);
        if (diff1>=TH3 || diff2>=TH3 || diff3>=TH1 || diff4>=TH1) {
	  printf("//==1[%d,%d,%d]==\n", i, j, k);
          printf("//%s\n//%s%c\n", softbuf32, hardbuf32, diff1!=diff2?'*':' ');
          printf("//%s\n//%s%c\n", softbuf64, hardbuf64, diff3!=diff4?'*':' ');
#if 1
          soft32(1, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp1);
          hard32(1, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp2, 0);
          soft64(1, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp3);
          hard64(1, A[j*M+k], -A[j*M+i], A[i*M+k], &tmp4);
#endif
        }
#if 1
        A[j*M+k] = tmp2;
#else
        A[j*M+k] = A[j*M+k] +  -A[j*M+i] * A[i*M+k];
#endif
      }
    }
  }

  /* 前進消去 */
  for (i=0; i<M; i++) {
    for (j=0; j<M; j++)
      b[i*M+j] = (i==j)?1.0:0.0;
  }
  for (i=0; i<M; i++) {
    for (j=i+1; j<M; j++) { /* 逆行列(b[]=E)の場合,k<iではb[]==0なのでj=i+1から開始 */
      for (k=i; k<j; k++) { /* 逆行列(b[]=E)の場合,k<iではb[]==0なのでk=iから開始 */
	/*b[i*M+j] -= A[j*M+k]*b[i*M+k];*/
        diff1 = soft32(0, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp1);
        diff2 = hard32(0, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp2, testbench);
        diff3 = soft64(0, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp3);
        diff4 = hard64(0, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp4);
        if (diff1>=TH3 || diff2>=TH3 || diff3>=TH1 || diff4>=TH1) {
	  printf("//==2[%d,%d,%d]==\n", i, j, k);
          printf("//%s\n//%s%c\n", softbuf32, hardbuf32, diff1!=diff2?'*':' ');
          printf("//%s\n//%s%c\n", softbuf64, hardbuf64, diff3!=diff4?'*':' ');
#if 1
          soft32(1, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp1);
          hard32(1, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp2, 0);
          soft64(1, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp3);
          hard64(1, b[i*M+j], -A[j*M+k], b[i*M+k], &tmp4);
#endif
        }
#if 1
        b[i*M+j] = tmp2;
#else
        b[i*M+j] = b[i*M+j] + -A[j*M+k] * b[i*M+j];
#endif
      }
    }
  }

  /* 後退代入 */
  for (i=0; i<M; i++) {
    for (j=M-1; j>=0; j--) {
      if (j<M-1) {
	for (k=M-1; k>j; k--) { /* 最内列方向 */
	  /*b[i*M+j] -= A[j*M+k]*x[i*M+k];*/
	  diff1 = soft32(0, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp1);
	  diff2 = hard32(0, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp2, testbench);
	  diff3 = soft64(0, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp3);
	  diff4 = hard64(0, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp4);
	  if (diff1>=TH3 || diff2>=TH3 || diff3>=TH1 || diff4>=TH1) {
	    printf("//==3[%d,%d,%d]==\n", i, j, k);
	    printf("//%s\n//%s%c\n", softbuf32, hardbuf32, diff1!=diff2?'*':' ');
	    printf("//%s\n//%s%c\n", softbuf64, hardbuf64, diff3!=diff4?'*':' ');
#if 1
	    soft32(1, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp1);
	    hard32(1, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp2, 0);
	    soft64(1, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp3);
	    hard64(1, b[i*M+j], -A[j*M+k], x[i*M+k], &tmp4);
#endif
	  }
#if 1
	  b[i*M+j] = tmp2;
#else
	  b[i*M+j] = b[i*M+j] + -A[p[j]*M+k] * x[k];
#endif
	}
      } /* if (j<M-1) */
      inv1[j*M+p[i]] = x[i*M+j] = A[j*M+j]*b[i*M+j]; /* PIOにてLMMのx[i*M+j]を直接更新 */
    } /* j-loop */
  }
}

/******************************************************************************************************************************************/
/*** IEEE Floating point            *******************************************************************************************************/
/******************************************************************************************************************************************/

union fpn {
  struct raw {
    Uint w;
  } raw;
  struct flo {
    float w;
  } flo;
  struct base {
    Uint  frac : 23;
    Uint  exp  :  8;
    Uint  s    :  1;
  } base;
} in1, in2, in3, out, org;

radix4(Uint *pp, Uint *ps, Uint a, Uint b)
{
  switch (b) {
  case 0:  *pp =   0;                   *ps = 0; break;
  case 1:  *pp =   a    & 0x1ffffff;    *ps = 0; break;
  case 2:  *pp =   a    & 0x1ffffff;    *ps = 0; break;
  case 3:  *pp =   a<<1 & 0x1ffffff;    *ps = 0; break;
  case 4:  *pp = ~(a<<1)& 0x1ffffff;    *ps = 1; break;
  case 5:  *pp =  ~a    & 0x1ffffff;    *ps = 1; break;
  case 6:  *pp =  ~a    & 0x1ffffff;    *ps = 1; break;
  default: *pp =  ~0    & 0x1ffffff;    *ps = 1; break;
  }
}

partial_product(Uint *pp, Uint *ps, Uint a, Uint b, Uint pos)
{
  /* switch (pos) */
  /* case 0:    "~s  s  s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
  /* case 1-10: "    1 ~s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
  /* case 11:   "      ~s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
  /* case 12:   "         24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0" */
  Uint tp, ts;

  radix4(&tp, &ts, a, b);
  switch (pos) {
  case  0: *pp = ((~ts&1)<<27)|(ts<<26)|(  ts   <<25)|tp; *ps = ts; break;
  case  1: case  2: case  3: case  4: case  5: case  6: case  7: case  8: case  9:
  case 10: *pp =               ( 1<<26)|((~ts&1)<<25)|tp; *ps = ts; break;
  case 11: *pp =                        ((~ts&1)<<25)|tp; *ps = ts; break;
  default: *pp =                                      tp; *ps = ts; break;
  }
}

csa_line(Ull *co, Ull *s, Ull a, Ull b, Ull c)
{
  *s  = a ^ b ^ c;
  *co = ((a & b)|(b & c)|(c & a))<<1;
}

soft32(Uint info, float i1, float i2, float i3, float *o)
{
  int op = 3;
  in1.flo.w = i1;
  in2.flo.w = i2;
  in3.flo.w = i3;

  /* op=1:fmul, 2:fadd, 3:fma3 */
  struct src {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Uint frac : 24;
    Uint exp  :  8;
    Uint s    :  1;
  } s1, s2, s3;

  struct fmul_s {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Uint frac : 24;
    Uint exp  :  8;
    Uint s    :  1;
  } fmul_s1, fmul_s2;

  struct fmul_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 48; /* send upper 25bit to the next stage */
    Uint exp  :  9;
    Uint s    :  1;
  } fmul_d;

  struct fadd_s {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 25+PEXT; /* aligned to fmul_d */
    Uint exp  :  9;
    Uint s    :  1;
  } fadd_s1, fadd_s2;

  struct fadd_w {
    Uint exp_comp  :  1;
    Uint exp_diff  :  9;
    Uint align_exp :  9;
    Ull  s1_align_frac : 25+PEXT;
    Ull  s2_align_frac : 25+PEXT;
  } fadd_w;

  struct fadd_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 26+PEXT;
    Uint exp  :  9;
    Uint s    :  1;
  } fadd_d;

  struct ex1_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 26+PEXT;
    Uint exp  :  9;
    Uint s    :  1;
  } ex1_d;

  struct ex2_w {
    Uint lzc  :  6;
  } ex2_w;

  struct ex2_d {
    Uint frac : 23;
    Uint exp  :  8;
    Uint s    :  1;
  } ex2_d;

  s1.s    = (op==1)?0:in1.base.s;
  s1.exp  = (op==1)?0:in1.base.exp;
  s1.frac = (op==1)?0:(in1.base.exp==  0)?(0<<23)|in1.base.frac:(1<<23)|in1.base.frac;
  s1.zero = (op==1)?1:(in1.base.exp==  0) && (in1.base.frac==0);
  s1.inf  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac==0);
  s1.nan  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac!=0);
  s2.s    = in2.base.s;
  s2.exp  = in2.base.exp;
  s2.frac = (in2.base.exp==  0)?(0<<23)|in2.base.frac:(1<<23)|in2.base.frac;
  s2.zero = (in2.base.exp==  0) && (in2.base.frac==0);
  s2.inf  = (in2.base.exp==255) && (in2.base.frac==0);
  s2.nan  = (in2.base.exp==255) && (in2.base.frac!=0);
  s3.s    = (op==2)?0      :in3.base.s;
  s3.exp  = (op==2)?127    :in3.base.exp;
  s3.frac = (op==2)?(1<<23):(in3.base.exp==  0)?(0<<23)|in3.base.frac:(1<<23)|in3.base.frac;
  s3.zero = (op==2)?0      :(in3.base.exp==  0) && (in3.base.frac==0);
  s3.inf  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac==0);
  s3.nan  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac!=0);

  org.flo.w = in1.flo.w+in2.flo.w*in3.flo.w;
  if (info) {
    printf("//--soft32--\n");
    printf("//s1: %08.8x %f\n", in1.raw.w, in1.flo.w);
    printf("//s2: %08.8x %f\n", in2.raw.w, in2.flo.w);
    printf("//s3: %08.8x %f\n", in3.raw.w, in3.flo.w);
    printf("//d : %08.8x %f\n", org.raw.w, org.flo.w);
  }

  fmul_s1.s    = s2.s;
  fmul_s1.exp  = s2.exp;
  fmul_s1.frac = s2.frac;
  fmul_s1.zero = s2.zero;
  fmul_s1.inf  = s2.inf;
  fmul_s1.nan  = s2.nan;
  fmul_s2.s    = s3.s;
  fmul_s2.exp  = s3.exp;
  fmul_s2.frac = s3.frac;
  fmul_s2.zero = s3.zero;
  fmul_s2.inf  = s3.inf;
  fmul_s2.nan  = s3.nan;

  /* nan  * any  -> nan */
  /* inf  * zero -> nan */
  /* inf  * (~zero & ~nan) -> inf */
  /* zero * (~inf  & ~nan) -> zero */
  fmul_d.s    = fmul_s1.s ^ fmul_s2.s;
  fmul_d.exp  = ((0<<8)|fmul_s1.exp) + ((0<<8)|fmul_s2.exp) < 127 ? 0 :
                ((0<<8)|fmul_s1.exp) + ((0<<8)|fmul_s2.exp) - 127;
  fmul_d.frac = (Ull)fmul_s1.frac * (Ull)fmul_s2.frac;
  fmul_d.zero = (fmul_s1.zero && !fmul_s2.inf && !fmul_s2.nan) || (fmul_s2.zero && !fmul_s1.inf && !fmul_s1.nan);
  fmul_d.inf  = (fmul_s1.inf && !fmul_s2.zero && !fmul_s2.nan) || (fmul_s2.inf && !fmul_s1.zero && !fmul_s1.nan);
  fmul_d.nan  = fmul_s1.nan || fmul_s2.nan || (fmul_s1.inf && fmul_s2.zero) || (fmul_s2.inf && fmul_s1.zero);

  if (info) {
    printf("//fmul_s1: %x %x %x\n", fmul_s1.s, fmul_s1.exp, fmul_s1.frac);
    printf("//fmul_s2: %x %x %x\n", fmul_s2.s, fmul_s2.exp, fmul_s2.frac);
    printf("//fmul_d:  %x %x %08.8x_%08.8x\n", fmul_d.s, fmul_d.exp, (Uint)(fmul_d.frac>>32), (Uint)fmul_d.frac);
  }

  fadd_s1.s    = s1.s;
  fadd_s1.exp  = (0<s1.exp&&s1.exp<255)?(s1.exp-1):s1.exp;
  fadd_s1.frac = (0<s1.exp&&s1.exp<255)?(Ull)s1.frac<<(PEXT+1):(Ull)s1.frac<<PEXT;
  fadd_s1.zero = s1.zero;
  fadd_s1.inf  = s1.inf;
  fadd_s1.nan  = s1.nan;
  fadd_s2.s    = fmul_d.s;
  fadd_s2.exp  = fmul_d.exp;
  fadd_s2.frac = fmul_d.frac>>(23-PEXT); //★★★ガード対応必要
  fadd_s2.zero = fmul_d.zero;
  fadd_s2.inf  = fmul_d.inf;
  fadd_s2.nan  = fmul_d.nan;

  /* nan  + any  -> nan */
  /* inf  + -inf -> nan */
  /* inf  + (~-inf & ~nan) -> inf */
  /* -inf + (~inf  & ~nan) -> inf */
  fadd_w.exp_comp      = fadd_s1.exp>fadd_s2.exp?1:0;
  fadd_w.exp_diff      = fadd_w.exp_comp?(fadd_s1.exp-fadd_s2.exp):(fadd_s2.exp-fadd_s1.exp);
  if (fadd_w.exp_diff>25+PEXT) fadd_w.exp_diff=25+PEXT;
  fadd_w.align_exp     = fadd_w.exp_comp?fadd_s1.exp:fadd_s2.exp;
  fadd_w.s1_align_frac = fadd_s1.frac>>(fadd_w.exp_comp?0:fadd_w.exp_diff);
  fadd_w.s2_align_frac = fadd_s2.frac>>(fadd_w.exp_comp?fadd_w.exp_diff:0);

  if (info) {
    printf("//fadd_s1: %x %x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s1.s, fadd_s1.exp, (Uint)((Ull)fadd_s1.frac>>32), (Uint)fadd_s1.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s1_align_frac>>32), (Uint)fadd_w.s1_align_frac);
    printf("//fadd_s2: %x %x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s2.s, fadd_s2.exp, (Uint)((Ull)fadd_s2.frac>>32), (Uint)fadd_s2.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s2_align_frac>>32), (Uint)fadd_w.s2_align_frac);
  }

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

#define FLOAT_PZERO 0x00000000
#define FLOAT_NZERO 0x80000000
#define FLOAT_PINF  0x7f800000
#define FLOAT_NINF  0xff800000
#define FLOAT_NAN   0xffc00000

  /* normalize */

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

/* radix-4 modified booth (unsigned A[23:0]*B[23:0] -> C[47:1]+S[46:0] */
/*                             0 0 B[23:................................0] 0 */
/*                                                                  B[ 1:-1] */
/*                                                               B[ 3: 1]    */
/*                                                            B[ 5: 3]       */
/*                                                         B[ 7: 5]          */
/*                                                      B[ 9: 7]             */
/*                                                   B[11: 9]                */
/*                                                B[13:11]                   */
/*                                             B[15:13]                      */
/*                                          B[17:15]                         */
/*                                       B[19:17]                            */
/*                                    B[21:19]                               */
/*                                 B[23:21]                                  */
/*                              B[25:23]                                     */
/*         switch (B[2j+1:2j-1])                                             */
/*         case 0: pp[j][47:2j] =   0;  ... single=0;double=0;neg=0          */
/*         case 1: pp[j][47:2j] =   A;  ... single=1;double=0;neg=0          */
/*         case 2: pp[j][47:2j] =   A;  ... single=1;double=0;neg=0          */
/*         case 3: pp[j][47:2j] =  2A;  ... single=0;double=1;neg=0          */
/*         case 4: pp[j][47:2j] = -2A;  ... single=0;double=1;neg=1          */
/*         case 5: pp[j][47:2j] =  -A;  ... single=1;double=0;neg=1          */
/*         case 6: pp[j][47:2j] =  -A;  ... single=1;double=0;neg=1          */
/*         case 7: pp[j][47:2j] =   0;  ... single=0;double=0;neg=1          */
/*            j= 0の場合, pp[ 0][47: 0] 符号拡張                             */
/*            j=12の場合, pp[12][47:24](符号拡張不要)                        */
/*                                   single = B[2j] ^ B[2j-1];               */
/*                                   double = ~(single | ~(B[2j+1] ^ B[2j]));*/
/*                                   s(neg) = B[2j+1];                       */
/*                                   pp[j+1][2j]= s(neg);                    */
/*                                   j= 0の場合, pp[ 1][ 0]にs               */
/*                                   j=11の場合, pp[12][22]にs               */

/*  --stage-1 (13in)---------------------------------------------------------------------------------------------------------------------------------------*/
/*  pp[ 0]                                                             ~s  s  s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  pp[ 1]                                                           1 ~s 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2     s */
/*  pp[ 2]                                                     1 ~s 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4     s       */
/*                                                             |  | HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA *//* HA,24FA,HA,FA,2HA */
/*  S1[0]                                                     30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C1[0]                                                        29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1    */
/*                                                                                                                                                         */
/*  pp[ 3]                                               1 ~s 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6     s             */
/*  pp[ 4]                                         1 ~s 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8     s     |             */
/*  pp[ 5]                                   1 ~s 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10     s           |             */
/*                                           |  | HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA     |             *//* 2HA,23FA,HA,FA,2HA */
/*  S1[1]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4             */
/*  C1[1]                                      35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                      */
/*                                                                                                                                                         */
/*  pp[ 6]                             1 ~s 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12     s                               */
/*  pp[ 7]                       1 ~s 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14     s     |                               */
/*  pp[ 8]                 1 ~s 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16     s           |                               */
/*                         |  | HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA     |                               *//* 2HA,23FA,HA,FA,2HA */
/*  S1[2]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10                               */
/*  C1[2]                    41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13                                        */
/*                                                                                                                                                         */
/*  pp[ 9]           1 ~s 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18     s                                                 */
/*  pp[10]     1 ~s 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20     s     |                                                 */
/*  pp[11] ~s 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22     s           |                                                 */
/*          | HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA     |                                                 *//* 2HA,23FA,HA,FA,2HA */
/*  S1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16                                                 */
/*  C1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19                                                          */
/*                                                                                                                                                         */
/*  pp[12] 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24     s                                                                   */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-2 (9in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S1[0]                                                     30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C1[0]                                                        29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  | */
/*  S1[1]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4*          | */
/*                                           |  |  |  |  |  | HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA  | *//* HA,26FA,3HA */
/*  S2[0]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C2[0]                                                  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2       */
/*                                                                                                                                                         */
/*  C1[1]                                      35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                      */
/*  S1[2]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10* |  |  |                      */
/*  C1[2]                  | 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13           |  |  |                      */
/*                         | HA HA HA HA HA HA FA FA FA FA FA FA fA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA  |  |  |                      *//* 6HA,22FA,3HA */
/*  S2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                      */
/*  C2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                                  */
/*                                                                                                                                                         */
/*  S1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16*                                                */
/*  C1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19  |  |  |                                                 */
/*  pp[12] 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24     s*          |  |  |                                                 */
/*         FA FA FA FA FA fA FA FA FA FA FA FA FA FA FA FA FA FA fA FA FA FA FA FA HA FA HA HA HA  |  |  |                                                 *//* 22FA,HA,FA,3HA */
/*  S2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16                                                 */
/*  C2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20                                                             */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-3 (6in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S2[0]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C2[0]                                                  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  |  | */
/*  S2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                 |  | */
/*                         |  |  |  |  |  | HA HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA  |  | *//* 5HA,25FA,5HA */
/*  S3[0]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C3[0]                                37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3          */
/*                                                                                                                                                         */
/*  C2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                                  */
/*  S2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16  |  |  |  |  |                                  */
/*  C2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20              |  |  |  |  |                                  */
/*         HA HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA  |  |  |  |  |                                  *//* 5HA,23FA,4HA */
/*  S3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                                  */
/*  C3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17                                                    */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-4 (4in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S3[0]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C3[0]                                37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  |  |  | */
/*  S3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                          |  |  | */
/*          |  |  |  |  | HA HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA HA HA HA  |  |  | *//* 5HA,27FA,8HA */
/*  S4     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C4                 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4             */
/*                                                                                                                                                         */
/*  C3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17                                                    */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-5 (3in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S4     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C4                 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  |  |  |  | */
/*  C3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17                                         |  |  |  | */
/*         HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA HA HA HA HA HA HA HA HA  |  |  |  | *//* 4HA,27FA,13HA */
/*  S5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5                */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-6 (2in+fadd) シフト調整後----------------------------------------------------------------------------------------------------------------------*/
/*  S5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  |  |  |  |  | */
/*  AD     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24                                                           |  |  |  |  | */
/*         FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA  |  |  |  |  | *//* 24FA,19HA */
/*  S6     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C6     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6                   */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/

hard32(Uint info, float i1, float i2, float i3, float *o, Uint testbench)
{
  int op = 3;
  in1.flo.w = i1;
  in2.flo.w = i2;
  in3.flo.w = i3;
  /* op=1:fmul (0.0 + s2 *  s3)  */
  /* op=2:fadd (s1  + s2 * 1.0) */
  /* op=3:fma3 (s1  + s2 *  s3)  */

  /* op=1:fmul, 2:fadd, 3:fma3 */
  struct src {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Uint frac : 24;
    Uint exp  :  8;
    Uint s    :  1;
  } s1, s2, s3; /* s1 + s2 * s3 */

  Uint tp;
  Uint ps[13]; /* partial_sign */
  Ull  pp[13]; /* partial_product */
  Ull  S1[4];  /* stage-1 */
  Ull  C1[4];  /* stage-1 */
  Ull  S2[3];  /* stage-2 */
  Ull  C2[3];  /* stage-2 */
  Ull  S3[2];  /* stage-3 */
  Ull  C3[2];  /* stage-3 */
  Ull  S4;     /* stage-4 */
  Ull  C4;     /* stage-4 */
  Ull  S5;     /* stage-5 */
  Ull  C5;     /* stage-5 */
  Ull  S6[3];  /* stage-6 */
  Ull  C6[3];  /* stage-6 */
  Ull  S7[3];  /* stage-6 */
  Ull  C7[3];  /* stage-6 */

  struct ex1_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  csa_s: 25+PEXT; //■■■
    Ull  csa_c: 25+PEXT; //■■■
    Uint exp  :  9;
    Uint s    :  1;
  } ex1_d; /* csa */

  struct fadd_s {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 25+PEXT; /* ■■■aligned to ex1_d */
    Uint exp  :  9;
    Uint s    :  1;
  } fadd_s1;

  struct fadd_w {
    Uint exp_comp  :  1;
    Uint exp_diff  :  9;
    Uint align_exp :  9;
    Ull  s1_align_frac : 25+PEXT; //■■■
    Ull  s2_align_frac : 25+PEXT; //■■■
    Ull  s3_align_frac : 25+PEXT; //■■■
  } fadd_w;

  struct ex2_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac0: 26+PEXT; /* 26bit */ //■■■
    Ull  frac1: 25+PEXT; /* 25bit */ //■■■
    Ull  frac2: 26+PEXT; /* 26bit */ //■■■
    Ull  frac : 26+PEXT; /* 26bit */ //■■■
    Uint exp  :  9;
    Uint s    :  1;
  } ex2_d;

  struct ex3_w {
    Uint lzc  :  6;
  } ex3_w;

  struct ex3_d {
    Uint frac : 23;
    Uint exp  :  8;
    Uint s    :  1;
  } ex3_d;

  s1.s    = (op==1)?0:in1.base.s;
  s1.exp  = (op==1)?0:in1.base.exp;
  s1.frac = (op==1)?0:(in1.base.exp==  0)?(0<<23)|in1.base.frac:(1<<23)|in1.base.frac;
  s1.zero = (op==1)?1:(in1.base.exp==  0) && (in1.base.frac==0);
  s1.inf  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac==0);
  s1.nan  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac!=0);
  s2.s    = in2.base.s;
  s2.exp  = in2.base.exp;
  s2.frac = (in2.base.exp==  0)?(0<<23)|in2.base.frac:(1<<23)|in2.base.frac;
  s2.zero = (in2.base.exp==  0) && (in2.base.frac==0);
  s2.inf  = (in2.base.exp==255) && (in2.base.frac==0);
  s2.nan  = (in2.base.exp==255) && (in2.base.frac!=0);
  s3.s    = (op==2)?0      :in3.base.s;
  s3.exp  = (op==2)?127    :in3.base.exp;
  s3.frac = (op==2)?(1<<23):(in3.base.exp==  0)?(0<<23)|in3.base.frac:(1<<23)|in3.base.frac;
  s3.zero = (op==2)?0      :(in3.base.exp==  0) && (in3.base.frac==0);
  s3.inf  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac==0);
  s3.nan  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac!=0);

  org.flo.w = in1.flo.w+in2.flo.w*in3.flo.w;
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
  ex1_d.s    = s2.s ^ s3.s;
  ex1_d.exp  = ((0<<8)|s2.exp) + ((0<<8)|s3.exp) < 127 ? 0 :
               ((0<<8)|s2.exp) + ((0<<8)|s3.exp) - 127;

  /**************************************************************************************************************/
  /***  partial product  ****************************************************************************************/
  /**************************************************************************************************************/
  /*ex1_d.frac = (Ull)s2.frac * (Ull)s3.frac;*/
  partial_product(&tp, &ps[ 0], s2.frac, (s3.frac<< 1)&7,  0); pp[ 0] =  (Ull)tp;                        //if (info) {printf("pp[ 0]=%04.4x_%08.8x ps[ 0]=%d\n", (Uint)(pp[ 0]>>32), (Uint)pp[ 0], ps[ 0]);} /*1,0,-1*/
  partial_product(&tp, &ps[ 1], s2.frac, (s3.frac>> 1)&7,  1); pp[ 1] = ((Ull)tp<< 2)| (Ull)ps[ 0];      //if (info) {printf("pp[ 1]=%04.4x_%08.8x ps[ 1]=%d\n", (Uint)(pp[ 1]>>32), (Uint)pp[ 1], ps[ 1]);} /*3,2, 1*/
  partial_product(&tp, &ps[ 2], s2.frac, (s3.frac>> 3)&7,  2); pp[ 2] = ((Ull)tp<< 4)|((Ull)ps[ 1]<< 2); //if (info) {printf("pp[ 2]=%04.4x_%08.8x ps[ 2]=%d\n", (Uint)(pp[ 2]>>32), (Uint)pp[ 2], ps[ 2]);} /*5,4, 3*/
  partial_product(&tp, &ps[ 3], s2.frac, (s3.frac>> 5)&7,  3); pp[ 3] = ((Ull)tp<< 6)|((Ull)ps[ 2]<< 4); //if (info) {printf("pp[ 3]=%04.4x_%08.8x ps[ 3]=%d\n", (Uint)(pp[ 3]>>32), (Uint)pp[ 3], ps[ 3]);} /*7,6, 5*/
  partial_product(&tp, &ps[ 4], s2.frac, (s3.frac>> 7)&7,  4); pp[ 4] = ((Ull)tp<< 8)|((Ull)ps[ 3]<< 6); //if (info) {printf("pp[ 4]=%04.4x_%08.8x ps[ 4]=%d\n", (Uint)(pp[ 4]>>32), (Uint)pp[ 4], ps[ 4]);} /*9,8, 7*/
  partial_product(&tp, &ps[ 5], s2.frac, (s3.frac>> 9)&7,  5); pp[ 5] = ((Ull)tp<<10)|((Ull)ps[ 4]<< 8); //if (info) {printf("pp[ 5]=%04.4x_%08.8x ps[ 5]=%d\n", (Uint)(pp[ 5]>>32), (Uint)pp[ 5], ps[ 5]);} /*11,10,9*/
  partial_product(&tp, &ps[ 6], s2.frac, (s3.frac>>11)&7,  6); pp[ 6] = ((Ull)tp<<12)|((Ull)ps[ 5]<<10); //if (info) {printf("pp[ 6]=%04.4x_%08.8x ps[ 6]=%d\n", (Uint)(pp[ 6]>>32), (Uint)pp[ 6], ps[ 6]);} /*13,12,11*/
  partial_product(&tp, &ps[ 7], s2.frac, (s3.frac>>13)&7,  7); pp[ 7] = ((Ull)tp<<14)|((Ull)ps[ 6]<<12); //if (info) {printf("pp[ 7]=%04.4x_%08.8x ps[ 7]=%d\n", (Uint)(pp[ 7]>>32), (Uint)pp[ 7], ps[ 7]);} /*15,14,13*/
  partial_product(&tp, &ps[ 8], s2.frac, (s3.frac>>15)&7,  8); pp[ 8] = ((Ull)tp<<16)|((Ull)ps[ 7]<<14); //if (info) {printf("pp[ 8]=%04.4x_%08.8x ps[ 8]=%d\n", (Uint)(pp[ 8]>>32), (Uint)pp[ 8], ps[ 8]);} /*17,16,15*/
  partial_product(&tp, &ps[ 9], s2.frac, (s3.frac>>17)&7,  9); pp[ 9] = ((Ull)tp<<18)|((Ull)ps[ 8]<<16); //if (info) {printf("pp[ 9]=%04.4x_%08.8x ps[ 9]=%d\n", (Uint)(pp[ 9]>>32), (Uint)pp[ 9], ps[ 9]);} /*19,18,17*/
  partial_product(&tp, &ps[10], s2.frac, (s3.frac>>19)&7, 10); pp[10] = ((Ull)tp<<20)|((Ull)ps[ 9]<<18); //if (info) {printf("pp[10]=%04.4x_%08.8x ps[10]=%d\n", (Uint)(pp[10]>>32), (Uint)pp[10], ps[10]);} /*21,20,19*/
  partial_product(&tp, &ps[11], s2.frac, (s3.frac>>21)&7, 11); pp[11] = ((Ull)tp<<22)|((Ull)ps[10]<<20); //if (info) {printf("pp[11]=%04.4x_%08.8x ps[11]=%d\n", (Uint)(pp[11]>>32), (Uint)pp[11], ps[11]);} /**23,22,21*/
  partial_product(&tp, &ps[12], s2.frac, (s3.frac>>23)&7, 12); pp[12] = ((Ull)tp<<24)|((Ull)ps[11]<<22); //if (info) {printf("pp[12]=%04.4x_%08.8x ps[12]=%d\n", (Uint)(pp[12]>>32), (Uint)pp[12], ps[12]);} /*25,24,*23*/

  Ull x1 = (pp[0]+pp[1]+pp[2]+pp[3]+pp[4]+pp[5]+pp[6]+pp[7]+pp[8]+pp[9]+pp[10]+pp[11]+pp[12]);
  if (info) { printf("//x1(sum of pp)=%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x1>>32), (Uint)x1, (Uint)(x1>>23));}
  Ull x2 = (Ull)s2.frac * (Ull)s3.frac;
  if (info) { printf("//x2(s2 * s3)  =%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x2>>32), (Uint)x2, (Uint)(x2>>23));}

  /**************************************************************************************************************/
  /***  csa tree  ***********************************************************************************************/
  /**************************************************************************************************************/
  csa_line(&C1[0], &S1[0], pp[ 0], pp[ 1], pp[ 2]);
  csa_line(&C1[1], &S1[1], pp[ 3], pp[ 4], pp[ 5]);
  csa_line(&C1[2], &S1[2], pp[ 6], pp[ 7], pp[ 8]);
  csa_line(&C1[3], &S1[3], pp[ 9], pp[10], pp[11]);

  csa_line(&C2[0], &S2[0], S1[ 0], C1[ 0], S1[ 1]);
  csa_line(&C2[1], &S2[1], C1[ 1], S1[ 2], C1[ 2]);
  csa_line(&C2[2], &S2[2], S1[ 3], C1[ 3], pp[12]);

  csa_line(&C3[0], &S3[0], S2[ 0], C2[ 0], S2[ 1]);
  csa_line(&C3[1], &S3[1], C2[ 1], S2[ 2], C2[ 2]);

  csa_line(&C4,    &S4,    S3[ 0], C3[ 0], S3[ 1]);
  csa_line(&C5,    &S5,    S4,     C4,     C3[ 1]);

  ex1_d.csa_s = S5>>(23-PEXT); // sum   ■■■ガード対応必要
  ex1_d.csa_c = C5>>(23-PEXT); // carry ■■■ガード対応必要

  ex1_d.zero = (s2.zero && !s3.inf && !s3.nan) || (s3.zero && !s2.inf && !s2.nan);
  ex1_d.inf  = (s2.inf && !s3.zero && !s3.nan) || (s3.inf && !s2.zero && !s2.nan);
  ex1_d.nan  = s2.nan || s3.nan || (s2.inf && s3.zero) || (s3.inf && s2.zero);

  if (info) {
    printf("//S5           =%08.8x_%08.8x\n", (Uint)(S5>>32), (Uint)S5);
    printf("//C5           =%08.8x_%08.8x\n", (Uint)(C5>>32), (Uint)C5);
    printf("//++(48bit)    =%08.8x_%08.8x\n", (Uint)((C5+S5)>>32), (Uint)(C5+S5));
    printf("//csa_s        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_s>>32), (Uint)ex1_d.csa_s);
    printf("//csa_c        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_c>>32), (Uint)ex1_d.csa_c);
    printf("//ex1_d: %x %02.2x +=%08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)(ex1_d.csa_c+ex1_d.csa_s)>>32), (Uint)(ex1_d.csa_c+ex1_d.csa_s));
  }

  /**************************************************************************************************************/
  /***  3in-csa  ************************************************************************************************/
  /**************************************************************************************************************/
  fadd_s1.s    = s1.s;
  fadd_s1.exp  = (0<s1.exp&&s1.exp<255)?(s1.exp-1):s1.exp; //■■■
  fadd_s1.frac = (0<s1.exp&&s1.exp<255)?(Ull)s1.frac<<(PEXT+1):(Ull)s1.frac<<PEXT; //■■■
  fadd_s1.zero = s1.zero;
  fadd_s1.inf  = s1.inf;
  fadd_s1.nan  = s1.nan;

  /* nan  + any  -> nan */
  /* inf  + -inf -> nan */
  /* inf  + (~-inf & ~nan) -> inf */
  /* -inf + (~inf  & ~nan) -> inf */
  fadd_w.exp_comp      = fadd_s1.exp>ex1_d.exp?1:0;
  fadd_w.exp_diff      = fadd_w.exp_comp?(fadd_s1.exp-ex1_d.exp):(ex1_d.exp-fadd_s1.exp);
  if (fadd_w.exp_diff>(25+PEXT)) fadd_w.exp_diff=(25+PEXT); //■■■
  fadd_w.align_exp     = fadd_w.exp_comp?fadd_s1.exp:ex1_d.exp;
  fadd_w.s1_align_frac = fadd_s1.frac>>(fadd_w.exp_comp?0:fadd_w.exp_diff);
  fadd_w.s2_align_frac = ex1_d.csa_s >>(ex1_d.zero?(25+PEXT):fadd_w.exp_comp?fadd_w.exp_diff:0);
  fadd_w.s3_align_frac = ex1_d.csa_c >>(ex1_d.zero?(25+PEXT):fadd_w.exp_comp?fadd_w.exp_diff:0);

  if (info) {
    printf("//fadd_s1: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s1.s, fadd_s1.exp, (Uint)((Ull)fadd_s1.frac>>32), (Uint)fadd_s1.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s1_align_frac>>32), (Uint)fadd_w.s1_align_frac);
    printf("//csa_s: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_s>>32), (Uint)ex1_d.csa_s, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s2_align_frac>>32), (Uint)fadd_w.s2_align_frac);
    printf("//csa_c: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_c>>32), (Uint)ex1_d.csa_c, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s3_align_frac>>32), (Uint)fadd_w.s3_align_frac);
  }

  /*ex2_d.frac0       =  fadd_w.s1_align_frac+ (fadd_w.s2_align_frac+fadd_w.s3_align_frac);                        */
  /*ex2_d.frac1       =  fadd_w.s1_align_frac+~(fadd_w.s2_align_frac+fadd_w.s3_align_frac)+1;                      */
  /*ex2_d.frac2       = ~fadd_w.s1_align_frac+ (fadd_w.s2_align_frac+fadd_w.s3_align_frac)+1;                      */
  /*ex2_d.frac        = (fadd_s1.s==ex1_d.s) ? ex2_d.frac0 : (ex2_d.frac2 & 0x2000000) ? ex2_d.frac1 : ex2_d.frac2;*/
  /*printf("ex2d.frac0: %08.8x\n", ex2_d.frac0);*/
  /*printf("ex2d.frac1: %08.8x\n", ex2_d.frac1);*/
  /*printf("ex2d.frac2: %08.8x\n", ex2_d.frac2);*/
  /*printf("ex2d.frac:  %08.8x\n", ex2_d.frac );*/
  csa_line(&C6[0], &S6[0],  fadd_w.s1_align_frac,  fadd_w.s2_align_frac,  fadd_w.s3_align_frac);
  csa_line(&C6[1], &S6[1],  fadd_w.s1_align_frac, ~(Ull)fadd_w.s2_align_frac, ~(Ull)fadd_w.s3_align_frac);
  csa_line(&C7[1], &S7[1],  C6[1]|1LL,             S6[1],                 1LL);
  csa_line(&C6[2], &S6[2], ~(Ull)fadd_w.s1_align_frac,  fadd_w.s2_align_frac,  fadd_w.s3_align_frac);
  csa_line(&C7[2], &S7[2],  C6[2]|1LL,             S6[2],                 0LL);

  if (info) {
    printf("//C6[0]=%08.8x_%08.8x(a+c+s)\n",   (Uint)(C6[0]>>32), (Uint)C6[0]);
    printf("//S6[0]=%08.8x_%08.8x(a+c+s)\n",   (Uint)(S6[0]>>32), (Uint)S6[0]);
    printf("//C6[1]=%08.8x_%08.8x(a-c-s)\n",   (Uint)(C6[1]>>32), (Uint)C6[1]);
    printf("//S6[1]=%08.8x_%08.8x(a-c-s)\n",   (Uint)(S6[1]>>32), (Uint)S6[1]);
    printf("//C7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(C7[1]>>32), (Uint)C7[1]);
    printf("//S7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(S7[1]>>32), (Uint)S7[1]);
    printf("//C6[2]=%08.8x_%08.8x(c+s-a)\n",   (Uint)(C6[2]>>32), (Uint)C6[2]);
    printf("//S6[2]=%08.8x_%08.8x(c+s-a)\n",   (Uint)(S6[2]>>32), (Uint)S6[2]);
    printf("//C7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(C7[2]>>32), (Uint)C7[2]);
    printf("//S7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(S7[2]>>32), (Uint)S7[2]);
  }

  /**************************************************************************************************************/
  /***  2in-add  ************************************************************************************************/
  /**************************************************************************************************************/
  ex2_d.frac0       =  C6[0]+S6[0]; /* 26bit */
  ex2_d.frac1       =  C7[1]+S7[1]; /* 25bit */
  ex2_d.frac2       =  C7[2]+S7[2]; /* 26bit */

  if (info) {
    printf("//ex2_d.frac0=%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac0>>32), (Uint)ex2_d.frac0);
    printf("//ex2_d.frac1=%08.8x_%08.8x(a-c-s)\n", (Uint)((Ull)ex2_d.frac1>>32), (Uint)ex2_d.frac1);
    printf("//ex2_d.frac2=%08.8x_%08.8x(c+s-a)\n", (Uint)((Ull)ex2_d.frac2>>32), (Uint)ex2_d.frac2);
  }
  
  ex2_d.s           = (fadd_s1.s==ex1_d.s) ? fadd_s1.s   : (ex2_d.frac2 & (0x2000000LL<<PEXT)) ? fadd_s1.s : ex1_d.s; //■■■
  ex2_d.exp         = fadd_w.align_exp;
  ex2_d.frac        = (fadd_s1.s==ex1_d.s) ? ex2_d.frac0 : (ex2_d.frac2 & (0x2000000LL<<PEXT)) ? ex2_d.frac1 : ex2_d.frac2 & (0xffffffffffffLL>>(23-PEXT)); /* 26bit */ //■■■
  ex2_d.zero        = ex2_d.frac==0;
  ex2_d.inf         = (!fadd_s1.s && fadd_s1.inf && !( ex1_d.s   && ex1_d.inf)   && !ex1_d.nan)
                   || ( fadd_s1.s && fadd_s1.inf && !(!ex1_d.s   && ex1_d.inf)   && !ex1_d.nan)
                   || (!ex1_d.s   && ex1_d.inf   && !( fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan)
                   || ( ex1_d.s   && ex1_d.inf   && !(!fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan) ;
  ex2_d.nan         = fadd_s1.nan || ex1_d.nan;

  if (info) {
    printf("//ex2_d.frac =%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac>>32), (Uint)ex2_d.frac);
  }

#define FLOAT_PZERO 0x00000000
#define FLOAT_NZERO 0x80000000
#define FLOAT_PINF  0x7f800000
#define FLOAT_NINF  0xff800000
#define FLOAT_NAN   0xffc00000

  /**************************************************************************************************************/
  /***  normalize  **********************************************************************************************/
  /**************************************************************************************************************/
#if 1
  ex3_w.lzc          = (ex2_d.frac & 0x2000000LL<<PEXT)?62 :
                       (ex2_d.frac & 0x1000000LL<<PEXT)?63 :
                       (ex2_d.frac & 0x0800000LL<<PEXT)? 0 :
                       (ex2_d.frac & 0x0400000LL<<PEXT)? 1 :
                       (ex2_d.frac & 0x0200000LL<<PEXT)? 2 :
                       (ex2_d.frac & 0x0100000LL<<PEXT)? 3 :
                       (ex2_d.frac & 0x0080000LL<<PEXT)? 4 :
                       (ex2_d.frac & 0x0040000LL<<PEXT)? 5 :
                       (ex2_d.frac & 0x0020000LL<<PEXT)? 6 :
                       (ex2_d.frac & 0x0010000LL<<PEXT)? 7 :
                       (ex2_d.frac & 0x0008000LL<<PEXT)? 8 :
                       (ex2_d.frac & 0x0004000LL<<PEXT)? 9 :
                       (ex2_d.frac & 0x0002000LL<<PEXT)?10 :
                       (ex2_d.frac & 0x0001000LL<<PEXT)?11 :
                       (ex2_d.frac & 0x0000800LL<<PEXT)?12 :
                       (ex2_d.frac & 0x0000400LL<<PEXT)?13 :
                       (ex2_d.frac & 0x0000200LL<<PEXT)?14 :
                       (ex2_d.frac & 0x0000100LL<<PEXT)?15 :
                       (ex2_d.frac & 0x0000080LL<<PEXT)?16 :
                       (ex2_d.frac & 0x0000040LL<<PEXT)?17 :
                       (ex2_d.frac & 0x0000020LL<<PEXT)?18 :
                       (ex2_d.frac & 0x0000010LL<<PEXT)?19 :
                       (ex2_d.frac & 0x0000008LL<<PEXT)?20 :
                       (ex2_d.frac & 0x0000004LL<<PEXT)?21 :
                       (ex2_d.frac & 0x0000002LL<<PEXT)?22 :
                       (ex2_d.frac & 0x0000001LL<<PEXT)?23 :
#if (PEXT>= 1)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 1)?24 :
#endif
#if (PEXT>= 2)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 2)?25 :
#endif
#if (PEXT>= 3)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 3)?26 :
#endif
#if (PEXT>= 4)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 4)?27 :
#endif
#if (PEXT>= 5)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 5)?28 :
#endif
#if (PEXT>= 6)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 6)?29 :
#endif
#if (PEXT>= 7)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 7)?30 :
#endif
#if (PEXT>= 8)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 8)?31 :
#endif
#if (PEXT>= 9)
                       (ex2_d.frac & 0x0000001LL<<PEXT- 9)?32 :
#endif
#if (PEXT>=10)
                       (ex2_d.frac & 0x0000001LL<<PEXT-10)?33 :
#endif
#if (PEXT>=11)
                       (ex2_d.frac & 0x0000001LL<<PEXT-11)?34 :
#endif
#if (PEXT>=12)
                       (ex2_d.frac & 0x0000001LL<<PEXT-12)?35 :
#endif
#if (PEXT>=13)
                       (ex2_d.frac & 0x0000001LL<<PEXT-13)?36 :
#endif
#if (PEXT>=14)
                       (ex2_d.frac & 0x0000001LL<<PEXT-14)?37 :
#endif
#if (PEXT>=15)
                       (ex2_d.frac & 0x0000001LL<<PEXT-15)?38 :
#endif
#if (PEXT>=16)
                       (ex2_d.frac & 0x0000001LL<<PEXT-16)?39 :
#endif
#if (PEXT>=17)
                       (ex2_d.frac & 0x0000001LL<<PEXT-17)?40 :
#endif
#if (PEXT>=18)
                       (ex2_d.frac & 0x0000001LL<<PEXT-18)?41 :
#endif
#if (PEXT>=19)
                       (ex2_d.frac & 0x0000001LL<<PEXT-19)?42 :
#endif
#if (PEXT>=20)
                       (ex2_d.frac & 0x0000001LL<<PEXT-20)?43 :
#endif
#if (PEXT>=21)
                       (ex2_d.frac & 0x0000001LL<<PEXT-21)?44 :
#endif
#if (PEXT>=22)
                       (ex2_d.frac & 0x0000001LL<<PEXT-22)?45 :
#endif
                                                       24+PEXT;
  if (info) {
    printf("//ex2:%x %x %08.8x_%08.8x ", ex2_d.s, ex2_d.exp, (Uint)((Ull)ex2_d.frac>>32), (Uint)ex2_d.frac);
  }

  if (ex2_d.nan) {
    ex3_d.s    = 1;
    ex3_d.frac = 0x400000;
    ex3_d.exp  = 0xff;

  }
  else if (ex2_d.inf) {
    ex3_d.s    = ex2_d.s;
    ex3_d.frac = 0x000000;
    ex3_d.exp  = 0xff;
  }
  else if (ex3_w.lzc == 62) {
    if (info) {
      printf("lzc==%d\n", ex3_w.lzc);
    }
    if (ex2_d.exp >= 253) {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = 0x000000;
      ex3_d.exp  = 0xff;
    }
    else {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = ex2_d.frac>>(2+PEXT); //■■■ガード対応必要
      ex3_d.exp  = ex2_d.exp + 2;
    }
  }
  else if (ex3_w.lzc == 63) {
    if (info) {
      printf("lzc==%d\n", ex3_w.lzc);
    }
    if (ex2_d.exp >= 254) {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = 0x000000;
      ex3_d.exp  = 0xff;
    }
    else {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = ex2_d.frac>>(1+PEXT); //■■■ガード対応必要
      ex3_d.exp  = ex2_d.exp + 1;
    }
  }
  else if (ex3_w.lzc <= (23+PEXT)) { //■■■
    if (info) {
      printf("lzc==%d\n", ex3_w.lzc);
    }
    if (ex2_d.exp >= ex3_w.lzc + 255) {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = 0x000000;
      ex3_d.exp  = 0xff;
    }
    else if (ex2_d.exp <= ex3_w.lzc) { /* subnormal num */
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = (ex2_d.frac<<ex2_d.exp)>>PEXT; //■■■
      ex3_d.exp  = 0x00;
    }
    else { /* normalized num */
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = (ex2_d.frac<<ex3_w.lzc)>>PEXT; //■■■
      ex3_d.exp  = ex2_d.exp - ex3_w.lzc;
    }
#define NO_GUARD_BITS
#ifndef NO_GUARD_BITS
    int f_ulp = (ex2_d.frac<<ex3_w.lzc)>> PEXT   &1;
    int f_g   = (ex2_d.frac<<ex3_w.lzc)>>(PEXT-1)&1;
    int f_r   = (ex2_d.frac<<ex3_w.lzc)>>(PEXT-2)&1;
    int f_s   =((ex2_d.frac<<ex3_w.lzc)&(0xfffffffffffLL>>(46-PEXT))!=0;
    switch (f_ulp<<3|f_g<<2|f_r<<1|f_s) {
    case 0: case 1: case 2: case 3: case 4: /* ulp|G|R|S */
    case 8: case 9: case 10: case 11:
      break;
    case 5: case 6: case 7: /* ulp++ */
    case 12: case 13: case 14: case 15: default:
      if (info)
	printf("//ex3:%x %x %x++ -> ", ex3_d.s, ex3_d.exp, ex3_d.frac);
      ex3_d.frac++;
      if (info)
	printf("%x\n", ex3_d.frac);
      break;
    }
#endif
  }
  else { /* zero */
    if (info) {
      printf("zero\n");
    }
    ex3_d.s    = 0;
    ex3_d.frac = 0x000000;
    ex3_d.exp  = 0x00;
  }
#endif

  if (info) {
    printf("//ex3:%x %x %x\n", ex3_d.s, ex3_d.exp, ex3_d.frac);
  }

  out.raw.w  = (ex3_d.s<<31)|(ex3_d.exp<<23)|(ex3_d.frac);
  org.flo.w  = i1+i2*i3;
  Uint diff = out.raw.w>org.raw.w ? out.raw.w-org.raw.w : org.raw.w-out.raw.w;

  if (!info)
    sprintf(hardbuf32, "%8.8e:%08.8x %8.8e:%08.8x %8.8e:%08.8x ->%8.8e:%08.8x (%8.8e:%08.8x) %08.8x %s%s%s",
           in1.flo.w, in1.raw.w, in2.flo.w, in2.raw.w, in3.flo.w, in3.raw.w, out.flo.w, out.raw.w, org.flo.w, org.raw.w, diff,
           diff>=TH1 ? "H":"",
           diff>=TH2 ? "H":"",
           diff>=TH3 ? "H":""
           );
  *o = out.flo.w;

  if (testbench) {
    printf("CHECK_FPU(32'h%08.8x,32'h%08.8x,32'h%08.8x,32'h%08.8x);\n", in1.raw.w, in2.raw.w, in3.raw.w, out.raw.w);
  }

  return(diff);
}

soft64(Uint info, float i1, float i2, float i3, float *o)
{
  int op = 3;
  in1.flo.w = i1;
  in2.flo.w = i2;
  in3.flo.w = i3;

  /* op=1:fmul, 2:fadd, 3:fma3 */
  struct src {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Uint frac : 24;
    Uint exp  :  8;
    Uint s    :  1;
  } s1, s2, s3;

  struct fmul_s {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Uint frac : 24;
    Uint exp  :  8;
    Uint s    :  1;
  } fmul_s1, fmul_s2;

  struct fmul_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 48; /* send upper 25bit to the next stage */
    Uint exp  :  9;
    Uint s    :  1;
  } fmul_d;

  struct fadd_s {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 48; /* aligned to fmul_d *///★★★25->48 s1は24bit<<23なので実際は47bit分
    Uint exp  :  9;
    Uint s    :  1;
  } fadd_s1, fadd_s2;

  struct fadd_w {
    Uint exp_comp  :  1;
    Uint exp_diff  :  9;
    Uint align_exp :  9;
    Ull  s1_align_frac : 48;//★★★25->48 s1は24bit<<23なので実際は47bit分
    Ull  s2_align_frac : 48;//★★★25->48 s2は48bit分ある
  } fadd_w;

  struct fadd_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 49;//★★★26->49
    Uint exp  :  9;
    Uint s    :  1;
  } fadd_d;

  struct ex1_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 49;//★★★26->49
    Uint exp  :  9;
    Uint s    :  1;
  } ex1_d;

  struct ex2_w {
    Uint lzc  :  6;//★★★5->6
  } ex2_w;

  struct ex2_d {
    Uint frac : 23;
    Uint exp  :  8;
    Uint s    :  1;
  } ex2_d;

  s1.s    = (op==1)?0:in1.base.s;
  s1.exp  = (op==1)?0:in1.base.exp;
  s1.frac = (op==1)?0:(in1.base.exp==  0)?(0<<23)|in1.base.frac:(1<<23)|in1.base.frac;
  s1.zero = (op==1)?1:(in1.base.exp==  0) && (in1.base.frac==0);
  s1.inf  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac==0);
  s1.nan  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac!=0);
  s2.s    = in2.base.s;
  s2.exp  = in2.base.exp;
  s2.frac = (in2.base.exp==  0)?(0<<23)|in2.base.frac:(1<<23)|in2.base.frac;
  s2.zero = (in2.base.exp==  0) && (in2.base.frac==0);
  s2.inf  = (in2.base.exp==255) && (in2.base.frac==0);
  s2.nan  = (in2.base.exp==255) && (in2.base.frac!=0);
  s3.s    = (op==2)?0      :in3.base.s;
  s3.exp  = (op==2)?127    :in3.base.exp;
  s3.frac = (op==2)?(1<<23):(in3.base.exp==  0)?(0<<23)|in3.base.frac:(1<<23)|in3.base.frac;
  s3.zero = (op==2)?0      :(in3.base.exp==  0) && (in3.base.frac==0);
  s3.inf  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac==0);
  s3.nan  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac!=0);

  org.flo.w = in1.flo.w+in2.flo.w*in3.flo.w;
  if (info) {
    printf("//--soft64--\n");
    printf("//s1: %08.8x %f\n", in1.raw.w, in1.flo.w);
    printf("//s2: %08.8x %f\n", in2.raw.w, in2.flo.w);
    printf("//s3: %08.8x %f\n", in3.raw.w, in3.flo.w);
    printf("//d : %08.8x %f\n", org.raw.w, org.flo.w);
  }

  fmul_s1.s    = s2.s;
  fmul_s1.exp  = s2.exp;
  fmul_s1.frac = s2.frac;
  fmul_s1.zero = s2.zero;
  fmul_s1.inf  = s2.inf;
  fmul_s1.nan  = s2.nan;
  fmul_s2.s    = s3.s;
  fmul_s2.exp  = s3.exp;
  fmul_s2.frac = s3.frac;
  fmul_s2.zero = s3.zero;
  fmul_s2.inf  = s3.inf;
  fmul_s2.nan  = s3.nan;

  /* nan  * any  -> nan */
  /* inf  * zero -> nan */
  /* inf  * (~zero & ~nan) -> inf */
  /* zero * (~inf  & ~nan) -> zero */
  fmul_d.s    = fmul_s1.s ^ fmul_s2.s;
  fmul_d.exp  = ((0<<8)|fmul_s1.exp) + ((0<<8)|fmul_s2.exp) < 127 ? 0 :
                ((0<<8)|fmul_s1.exp) + ((0<<8)|fmul_s2.exp) - 127;
  fmul_d.frac = (Ull)fmul_s1.frac * (Ull)fmul_s2.frac;
  fmul_d.zero = (fmul_s1.zero && !fmul_s2.inf && !fmul_s2.nan) || (fmul_s2.zero && !fmul_s1.inf && !fmul_s1.nan);
  fmul_d.inf  = (fmul_s1.inf && !fmul_s2.zero && !fmul_s2.nan) || (fmul_s2.inf && !fmul_s1.zero && !fmul_s1.nan);
  fmul_d.nan  = fmul_s1.nan || fmul_s2.nan || (fmul_s1.inf && fmul_s2.zero) || (fmul_s2.inf && fmul_s1.zero);

  if (info) {
    printf("//fmul_s1: %x %x %x\n", fmul_s1.s, fmul_s1.exp, fmul_s1.frac);
    printf("//fmul_s2: %x %x %x\n", fmul_s2.s, fmul_s2.exp, fmul_s2.frac);
    printf("//fmul_d:  %x %x %08.8x_%08.8x\n", fmul_d.s, fmul_d.exp, (Uint)(fmul_d.frac>>32), (Uint)fmul_d.frac);
  }

  fadd_s1.s    = s1.s;
  fadd_s1.exp  = (0<s1.exp&&s1.exp<255)?(s1.exp-1):s1.exp;
  fadd_s1.frac = (0<s1.exp&&s1.exp<255)?(Ull)s1.frac<<(23+1):(Ull)s1.frac<<23;
  fadd_s1.zero = s1.zero;
  fadd_s1.inf  = s1.inf;
  fadd_s1.nan  = s1.nan;
  fadd_s2.s    = fmul_d.s;
  fadd_s2.exp  = fmul_d.exp;
  fadd_s2.frac = fmul_d.frac; //★★★ガード対応必要 >>23無しなら不要
  fadd_s2.zero = fmul_d.zero;
  fadd_s2.inf  = fmul_d.inf;
  fadd_s2.nan  = fmul_d.nan;

  /* nan  + any  -> nan */
  /* inf  + -inf -> nan */
  /* inf  + (~-inf & ~nan) -> inf */
  /* -inf + (~inf  & ~nan) -> inf */
  fadd_w.exp_comp      = fadd_s1.exp>fadd_s2.exp?1:0;
  fadd_w.exp_diff      = fadd_w.exp_comp?(fadd_s1.exp-fadd_s2.exp):(fadd_s2.exp-fadd_s1.exp);
  if (fadd_w.exp_diff>48) fadd_w.exp_diff=48;//★★★25->48
  fadd_w.align_exp     = fadd_w.exp_comp?fadd_s1.exp:fadd_s2.exp;
  fadd_w.s1_align_frac = fadd_s1.frac>>(fadd_w.exp_comp?0:fadd_w.exp_diff);
  fadd_w.s2_align_frac = fadd_s2.frac>>(fadd_w.exp_comp?fadd_w.exp_diff:0);

  if (info) {
    printf("//fadd_s1: %x %x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s1.s, fadd_s1.exp, (Uint)((Ull)fadd_s1.frac>>32), (Uint)fadd_s1.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s1_align_frac>>32), (Uint)fadd_w.s1_align_frac);
    printf("//fadd_s2: %x %x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s2.s, fadd_s2.exp, (Uint)((Ull)fadd_s2.frac>>32), (Uint)fadd_s2.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s2_align_frac>>32), (Uint)fadd_w.s2_align_frac);
  }

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

#define FLOAT_PZERO 0x00000000
#define FLOAT_NZERO 0x80000000
#define FLOAT_PINF  0x7f800000
#define FLOAT_NINF  0xff800000
#define FLOAT_NAN   0xffc00000

  /* normalize */

#if 1
  ex2_w.lzc          = (ex1_d.frac & 0x1000000000000LL)?62 :
                       (ex1_d.frac & 0x0800000000000LL)?63 :
                       (ex1_d.frac & 0x0400000000000LL)? 0 :
                       (ex1_d.frac & 0x0200000000000LL)? 1 :
                       (ex1_d.frac & 0x0100000000000LL)? 2 :
                       (ex1_d.frac & 0x0080000000000LL)? 3 :
                       (ex1_d.frac & 0x0040000000000LL)? 4 :
                       (ex1_d.frac & 0x0020000000000LL)? 5 :
                       (ex1_d.frac & 0x0010000000000LL)? 6 :
                       (ex1_d.frac & 0x0008000000000LL)? 7 :
                       (ex1_d.frac & 0x0004000000000LL)? 8 :
                       (ex1_d.frac & 0x0002000000000LL)? 9 :
                       (ex1_d.frac & 0x0001000000000LL)?10 :
                       (ex1_d.frac & 0x0000800000000LL)?11 :
                       (ex1_d.frac & 0x0000400000000LL)?12 :
                       (ex1_d.frac & 0x0000200000000LL)?13 :
                       (ex1_d.frac & 0x0000100000000LL)?14 :
                       (ex1_d.frac & 0x0000080000000LL)?15 :
                       (ex1_d.frac & 0x0000040000000LL)?16 :
                       (ex1_d.frac & 0x0000020000000LL)?17 :
                       (ex1_d.frac & 0x0000010000000LL)?18 :
                       (ex1_d.frac & 0x0000008000000LL)?19 :
                       (ex1_d.frac & 0x0000004000000LL)?20 :
                       (ex1_d.frac & 0x0000002000000LL)?21 :
                       (ex1_d.frac & 0x0000001000000LL)?22 :
                       (ex1_d.frac & 0x0000000800000LL)?23 :
                       (ex1_d.frac & 0x0000000400000LL)?24 :
                       (ex1_d.frac & 0x0000000200000LL)?25 :
                       (ex1_d.frac & 0x0000000100000LL)?26 :
                       (ex1_d.frac & 0x0000000080000LL)?27 :
                       (ex1_d.frac & 0x0000000040000LL)?28 :
                       (ex1_d.frac & 0x0000000020000LL)?29 :
                       (ex1_d.frac & 0x0000000010000LL)?30 :
                       (ex1_d.frac & 0x0000000008000LL)?31 :
                       (ex1_d.frac & 0x0000000004000LL)?32 :
                       (ex1_d.frac & 0x0000000002000LL)?33 :
                       (ex1_d.frac & 0x0000000001000LL)?34 :
                       (ex1_d.frac & 0x0000000000800LL)?35 :
                       (ex1_d.frac & 0x0000000000400LL)?36 :
                       (ex1_d.frac & 0x0000000000200LL)?37 :
                       (ex1_d.frac & 0x0000000000100LL)?38 :
                       (ex1_d.frac & 0x0000000000080LL)?39 :
                       (ex1_d.frac & 0x0000000000040LL)?40 :
                       (ex1_d.frac & 0x0000000000020LL)?41 :
                       (ex1_d.frac & 0x0000000000010LL)?42 :
                       (ex1_d.frac & 0x0000000000008LL)?43 :
                       (ex1_d.frac & 0x0000000000004LL)?44 :
                       (ex1_d.frac & 0x0000000000002LL)?45 :
                       (ex1_d.frac & 0x0000000000001LL)?46 :
                                                        47 ;
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
  else if (ex2_w.lzc == 62) {//★★★
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
      ex2_d.frac = (ex1_d.frac>>2)>>23; //★★★ガード対応必要
      ex2_d.exp  = ex1_d.exp + 2;
    }
  }
  else if (ex2_w.lzc == 63) {//★★★
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
      ex2_d.frac = (ex1_d.frac>>1)>>23; //★★★ガード対応必要
      ex2_d.exp  = ex1_d.exp + 1;
    }
  }
  else if (ex2_w.lzc <= 46) {//★★★
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
      ex2_d.frac = (ex1_d.frac<<ex1_d.exp)>>23; //★★★ガード対応必要
      ex2_d.exp  = 0x00;
    }
    else { /* normalized num */
      ex2_d.s    = ex1_d.s;
      ex2_d.frac = (ex1_d.frac<<ex2_w.lzc)>>23; //★★★ガード対応必要
      ex2_d.exp  = ex1_d.exp - ex2_w.lzc;
    }
#define NO_GUARD_BITS
#ifndef NO_GUARD_BITS
    int f_ulp = (ex1_d.frac<<ex2_w.lzc)>>23&1;
    int f_g   = (ex1_d.frac<<ex2_w.lzc)>>22&1;
    int f_r   = (ex1_d.frac<<ex2_w.lzc)>>21&1;
    int f_s   =((ex1_d.frac<<ex2_w.lzc)&0x1fffff)!=0;
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
    sprintf(softbuf64, "%8.8e:%08.8x %8.8e:%08.8x %8.8e:%08.8x ->%8.8e:%08.8x (%8.8e:%08.8x) %08.8x %s%s%s",
           in1.flo.w, in1.raw.w, in2.flo.w, in2.raw.w, in3.flo.w, in3.raw.w, out.flo.w, out.raw.w, org.flo.w, org.raw.w, diff,
           diff>=TH1 ? "S":"",
           diff>=TH2 ? "S":"",
           diff>=TH3 ? "S":""
           );
  *o = out.flo.w;
  return(diff);
}

/* radix-4 modified booth (unsigned A[23:0]*B[23:0] -> C[47:1]+S[46:0] */
/*                             0 0 B[23:................................0] 0 */
/*                                                                  B[ 1:-1] */
/*                                                               B[ 3: 1]    */
/*                                                            B[ 5: 3]       */
/*                                                         B[ 7: 5]          */
/*                                                      B[ 9: 7]             */
/*                                                   B[11: 9]                */
/*                                                B[13:11]                   */
/*                                             B[15:13]                      */
/*                                          B[17:15]                         */
/*                                       B[19:17]                            */
/*                                    B[21:19]                               */
/*                                 B[23:21]                                  */
/*                              B[25:23]                                     */
/*         switch (B[2j+1:2j-1])                                             */
/*         case 0: pp[j][47:2j] =   0;  ... single=0;double=0;neg=0          */
/*         case 1: pp[j][47:2j] =   A;  ... single=1;double=0;neg=0          */
/*         case 2: pp[j][47:2j] =   A;  ... single=1;double=0;neg=0          */
/*         case 3: pp[j][47:2j] =  2A;  ... single=0;double=1;neg=0          */
/*         case 4: pp[j][47:2j] = -2A;  ... single=0;double=1;neg=1          */
/*         case 5: pp[j][47:2j] =  -A;  ... single=1;double=0;neg=1          */
/*         case 6: pp[j][47:2j] =  -A;  ... single=1;double=0;neg=1          */
/*         case 7: pp[j][47:2j] =   0;  ... single=0;double=0;neg=1          */
/*            j= 0の場合, pp[ 0][47: 0] 符号拡張                             */
/*            j=12の場合, pp[12][47:24](符号拡張不要)                        */
/*                                   single = B[2j] ^ B[2j-1];               */
/*                                   double = ~(single | ~(B[2j+1] ^ B[2j]));*/
/*                                   s(neg) = B[2j+1];                       */
/*                                   pp[j+1][2j]= s(neg);                    */
/*                                   j= 0の場合, pp[ 1][ 0]にs               */
/*                                   j=11の場合, pp[12][22]にs               */

/*  --stage-1 (13in)---------------------------------------------------------------------------------------------------------------------------------------*/
/*  pp[ 0]                                                             ~s  s  s 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  pp[ 1]                                                           1 ~s 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2     s */
/*  pp[ 2]                                                     1 ~s 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4     s       */
/*                                                             |  | HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA *//* HA,24FA,HA,FA,2HA */
/*  S1[0]                                                     30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C1[0]                                                        29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1    */
/*                                                                                                                                                         */
/*  pp[ 3]                                               1 ~s 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6     s             */
/*  pp[ 4]                                         1 ~s 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8     s     |             */
/*  pp[ 5]                                   1 ~s 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10     s           |             */
/*                                           |  | HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA     |             *//* 2HA,23FA,HA,FA,2HA */
/*  S1[1]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4             */
/*  C1[1]                                      35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                      */
/*                                                                                                                                                         */
/*  pp[ 6]                             1 ~s 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12     s                               */
/*  pp[ 7]                       1 ~s 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14     s     |                               */
/*  pp[ 8]                 1 ~s 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16     s           |                               */
/*                         |  | HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA     |                               *//* 2HA,23FA,HA,FA,2HA */
/*  S1[2]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10                               */
/*  C1[2]                    41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13                                        */
/*                                                                                                                                                         */
/*  pp[ 9]           1 ~s 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18     s                                                 */
/*  pp[10]     1 ~s 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20     s     |                                                 */
/*  pp[11] ~s 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22     s           |                                                 */
/*          | HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA FA HA HA     |                                                 *//* 2HA,23FA,HA,FA,2HA */
/*  S1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16                                                 */
/*  C1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19                                                          */
/*                                                                                                                                                         */
/*  pp[12] 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24     s                                                                   */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-2 (9in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S1[0]                                                     30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C1[0]                                                        29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  | */
/*  S1[1]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4*          | */
/*                                           |  |  |  |  |  | HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA  | *//* HA,26FA,3HA */
/*  S2[0]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C2[0]                                                  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2       */
/*                                                                                                                                                         */
/*  C1[1]                                      35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                      */
/*  S1[2]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10* |  |  |                      */
/*  C1[2]                  | 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13           |  |  |                      */
/*                         | HA HA HA HA HA HA FA FA FA FA FA FA fA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA  |  |  |                      *//* 6HA,22FA,3HA */
/*  S2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                      */
/*  C2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                                  */
/*                                                                                                                                                         */
/*  S1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16*                                                */
/*  C1[3]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19  |  |  |                                                 */
/*  pp[12] 47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24     s*          |  |  |                                                 */
/*         FA FA FA FA FA fA FA FA FA FA FA FA FA FA FA FA FA FA fA FA FA FA FA FA HA FA HA HA HA  |  |  |                                                 *//* 22FA,HA,FA,3HA */
/*  S2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16                                                 */
/*  C2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20                                                             */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-3 (6in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S2[0]                                   36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C2[0]                                                  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  |  | */
/*  S2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7                 |  | */
/*                         |  |  |  |  |  | HA HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA  |  | *//* 5HA,25FA,5HA */
/*  S3[0]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C3[0]                                37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3          */
/*                                                                                                                                                         */
/*  C2[1]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                                  */
/*  S2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16  |  |  |  |  |                                  */
/*  C2[2]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20              |  |  |  |  |                                  */
/*         HA HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA  |  |  |  |  |                                  *//* 5HA,23FA,4HA */
/*  S3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                                  */
/*  C3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17                                                    */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-4 (4in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S3[0]                 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C3[0]                                37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  |  |  | */
/*  S3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11                          |  |  | */
/*          |  |  |  |  | HA HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA HA HA HA  |  |  | *//* 5HA,27FA,8HA */
/*  S4     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C4                 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4             */
/*                                                                                                                                                         */
/*  C3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17                                                    */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-5 (3in)----------------------------------------------------------------------------------------------------------------------------------------*/
/*  S4     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C4                 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  |  |  |  | */
/*  C3[1]  47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17                                         |  |  |  | */
/*         HA HA HA HA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA HA HA HA HA HA HA HA HA  |  |  |  | *//* 4HA,27FA,13HA */
/*  S5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5                */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  --stage-6 (2in+fadd) シフト調整後----------------------------------------------------------------------------------------------------------------------*/
/*  S5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C5     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  |  |  |  |  | */
/*  AD     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24                                                           |  |  |  |  | */
/*         FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA FA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA HA  |  |  |  |  | *//* 24FA,19HA */
/*  S6     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 */
/*  C6     47 46 45 44 43 42 41 40 39 38 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6                   */
/*  -------------------------------------------------------------------------------------------------------------------------------------------------------*/

hard64(Uint info, float i1, float i2, float i3, float *o)
{
  int op = 3;
  in1.flo.w = i1;
  in2.flo.w = i2;
  in3.flo.w = i3;
  /* op=1:fmul (0.0 + s2 *  s3)  */
  /* op=2:fadd (s1  + s2 * 1.0) */
  /* op=3:fma3 (s1  + s2 *  s3)  */

  /* op=1:fmul, 2:fadd, 3:fma3 */
  struct src {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Uint frac : 24;
    Uint exp  :  8;
    Uint s    :  1;
  } s1, s2, s3; /* s1 + s2 * s3 */

  Uint tp;
  Uint ps[13]; /* partial_sign */
  Ull  pp[13]; /* partial_product */
  Ull  S1[4];  /* stage-1 */
  Ull  C1[4];  /* stage-1 */
  Ull  S2[3];  /* stage-2 */
  Ull  C2[3];  /* stage-2 */
  Ull  S3[2];  /* stage-3 */
  Ull  C3[2];  /* stage-3 */
  Ull  S4;     /* stage-4 */
  Ull  C4;     /* stage-4 */
  Ull  S5;     /* stage-5 */
  Ull  C5;     /* stage-5 */
  Ull  S6[3];  /* stage-6 */
  Ull  C6[3];  /* stage-6 */
  Ull  S7[3];  /* stage-6 */
  Ull  C7[3];  /* stage-6 */

  struct ex1_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  csa_s: 48;//★★★25->48
    Ull  csa_c: 48;//★★★25->48
    Uint exp  :  9;
    Uint s    :  1;
  } ex1_d; /* csa */

  struct fadd_s {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac : 48; /* aligned to ex1_d *///★★★25->48 s1は24bit<<23なので実際は47bit分
    Uint exp  :  9;
    Uint s    :  1;
  } fadd_s1;

  struct fadd_w {
    Uint exp_comp  :  1;
    Uint exp_diff  :  9;
    Uint align_exp :  9;
    Ull  s1_align_frac : 48;//★★★25->48 s1は24bit<<23なので実際は47bit分
    Ull  s2_align_frac : 48;//★★★25->48 csa_sは48bit分ある
    Ull  s3_align_frac : 48;//★★★25->48 csa_cは48bit分ある
  } fadd_w;

  struct ex2_d {
    Uint nan  :  1;
    Uint inf  :  1;
    Uint zero :  1;
    Ull  frac0: 49; /* 26bit *///★★★26->49 (a+b)は49bit
    Ull  frac1: 48; /* 25bit *///★★★25->48
    Ull  frac2: 49; /* 26bit *///★★★26->49 (a-b)か(b-a)判断のため1bit多い
    Ull  frac : 49; /* 26bit *///★★★26->49
    Uint exp  :  9;
    Uint s    :  1;
  } ex2_d;

  struct ex3_w {
    Uint lzc  :  6;//★★★5->6
  } ex3_w;

  struct ex3_d {
    Uint frac : 23;
    Uint exp  :  8;
    Uint s    :  1;
  } ex3_d;

  s1.s    = (op==1)?0:in1.base.s;
  s1.exp  = (op==1)?0:in1.base.exp;
  s1.frac = (op==1)?0:(in1.base.exp==  0)?(0<<23)|in1.base.frac:(1<<23)|in1.base.frac;
  s1.zero = (op==1)?1:(in1.base.exp==  0) && (in1.base.frac==0);
  s1.inf  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac==0);
  s1.nan  = (op==1)?0:(in1.base.exp==255) && (in1.base.frac!=0);
  s2.s    = in2.base.s;
  s2.exp  = in2.base.exp;
  s2.frac = (in2.base.exp==  0)?(0<<23)|in2.base.frac:(1<<23)|in2.base.frac;
  s2.zero = (in2.base.exp==  0) && (in2.base.frac==0);
  s2.inf  = (in2.base.exp==255) && (in2.base.frac==0);
  s2.nan  = (in2.base.exp==255) && (in2.base.frac!=0);
  s3.s    = (op==2)?0      :in3.base.s;
  s3.exp  = (op==2)?127    :in3.base.exp;
  s3.frac = (op==2)?(1<<23):(in3.base.exp==  0)?(0<<23)|in3.base.frac:(1<<23)|in3.base.frac;
  s3.zero = (op==2)?0      :(in3.base.exp==  0) && (in3.base.frac==0);
  s3.inf  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac==0);
  s3.nan  = (op==2)?0      :(in3.base.exp==255) && (in3.base.frac!=0);

  org.flo.w = in1.flo.w+in2.flo.w*in3.flo.w;
  if (info) {
    printf("//--hard64--\n");
    printf("//s1: %08.8x %f\n", in1.raw.w, in1.flo.w);
    printf("//s2: %08.8x %f\n", in2.raw.w, in2.flo.w);
    printf("//s3: %08.8x %f\n", in3.raw.w, in3.flo.w);
    printf("//d : %08.8x %f\n", org.raw.w, org.flo.w);
  }

  /* nan  * any  -> nan */
  /* inf  * zero -> nan */
  /* inf  * (~zero & ~nan) -> inf */
  /* zero * (~inf  & ~nan) -> zero */
  ex1_d.s    = s2.s ^ s3.s;
  ex1_d.exp  = ((0<<8)|s2.exp) + ((0<<8)|s3.exp) < 127 ? 0 :
               ((0<<8)|s2.exp) + ((0<<8)|s3.exp) - 127;

  /**************************************************************************************************************/
  /***  partial product  ****************************************************************************************/
  /**************************************************************************************************************/
  /*ex1_d.frac = (Ull)s2.frac * (Ull)s3.frac;*/
  partial_product(&tp, &ps[ 0], s2.frac, (s3.frac<< 1)&7,  0); pp[ 0] =  (Ull)tp;                        //if (info) {printf("pp[ 0]=%04.4x_%08.8x ps[ 0]=%d\n", (Uint)(pp[ 0]>>32), (Uint)pp[ 0], ps[ 0]);} /*1,0,-1*/
  partial_product(&tp, &ps[ 1], s2.frac, (s3.frac>> 1)&7,  1); pp[ 1] = ((Ull)tp<< 2)| (Ull)ps[ 0];      //if (info) {printf("pp[ 1]=%04.4x_%08.8x ps[ 1]=%d\n", (Uint)(pp[ 1]>>32), (Uint)pp[ 1], ps[ 1]);} /*3,2, 1*/
  partial_product(&tp, &ps[ 2], s2.frac, (s3.frac>> 3)&7,  2); pp[ 2] = ((Ull)tp<< 4)|((Ull)ps[ 1]<< 2); //if (info) {printf("pp[ 2]=%04.4x_%08.8x ps[ 2]=%d\n", (Uint)(pp[ 2]>>32), (Uint)pp[ 2], ps[ 2]);} /*5,4, 3*/
  partial_product(&tp, &ps[ 3], s2.frac, (s3.frac>> 5)&7,  3); pp[ 3] = ((Ull)tp<< 6)|((Ull)ps[ 2]<< 4); //if (info) {printf("pp[ 3]=%04.4x_%08.8x ps[ 3]=%d\n", (Uint)(pp[ 3]>>32), (Uint)pp[ 3], ps[ 3]);} /*7,6, 5*/
  partial_product(&tp, &ps[ 4], s2.frac, (s3.frac>> 7)&7,  4); pp[ 4] = ((Ull)tp<< 8)|((Ull)ps[ 3]<< 6); //if (info) {printf("pp[ 4]=%04.4x_%08.8x ps[ 4]=%d\n", (Uint)(pp[ 4]>>32), (Uint)pp[ 4], ps[ 4]);} /*9,8, 7*/
  partial_product(&tp, &ps[ 5], s2.frac, (s3.frac>> 9)&7,  5); pp[ 5] = ((Ull)tp<<10)|((Ull)ps[ 4]<< 8); //if (info) {printf("pp[ 5]=%04.4x_%08.8x ps[ 5]=%d\n", (Uint)(pp[ 5]>>32), (Uint)pp[ 5], ps[ 5]);} /*11,10,9*/
  partial_product(&tp, &ps[ 6], s2.frac, (s3.frac>>11)&7,  6); pp[ 6] = ((Ull)tp<<12)|((Ull)ps[ 5]<<10); //if (info) {printf("pp[ 6]=%04.4x_%08.8x ps[ 6]=%d\n", (Uint)(pp[ 6]>>32), (Uint)pp[ 6], ps[ 6]);} /*13,12,11*/
  partial_product(&tp, &ps[ 7], s2.frac, (s3.frac>>13)&7,  7); pp[ 7] = ((Ull)tp<<14)|((Ull)ps[ 6]<<12); //if (info) {printf("pp[ 7]=%04.4x_%08.8x ps[ 7]=%d\n", (Uint)(pp[ 7]>>32), (Uint)pp[ 7], ps[ 7]);} /*15,14,13*/
  partial_product(&tp, &ps[ 8], s2.frac, (s3.frac>>15)&7,  8); pp[ 8] = ((Ull)tp<<16)|((Ull)ps[ 7]<<14); //if (info) {printf("pp[ 8]=%04.4x_%08.8x ps[ 8]=%d\n", (Uint)(pp[ 8]>>32), (Uint)pp[ 8], ps[ 8]);} /*17,16,15*/
  partial_product(&tp, &ps[ 9], s2.frac, (s3.frac>>17)&7,  9); pp[ 9] = ((Ull)tp<<18)|((Ull)ps[ 8]<<16); //if (info) {printf("pp[ 9]=%04.4x_%08.8x ps[ 9]=%d\n", (Uint)(pp[ 9]>>32), (Uint)pp[ 9], ps[ 9]);} /*19,18,17*/
  partial_product(&tp, &ps[10], s2.frac, (s3.frac>>19)&7, 10); pp[10] = ((Ull)tp<<20)|((Ull)ps[ 9]<<18); //if (info) {printf("pp[10]=%04.4x_%08.8x ps[10]=%d\n", (Uint)(pp[10]>>32), (Uint)pp[10], ps[10]);} /*21,20,19*/
  partial_product(&tp, &ps[11], s2.frac, (s3.frac>>21)&7, 11); pp[11] = ((Ull)tp<<22)|((Ull)ps[10]<<20); //if (info) {printf("pp[11]=%04.4x_%08.8x ps[11]=%d\n", (Uint)(pp[11]>>32), (Uint)pp[11], ps[11]);} /**23,22,21*/
  partial_product(&tp, &ps[12], s2.frac, (s3.frac>>23)&7, 12); pp[12] = ((Ull)tp<<24)|((Ull)ps[11]<<22); //if (info) {printf("pp[12]=%04.4x_%08.8x ps[12]=%d\n", (Uint)(pp[12]>>32), (Uint)pp[12], ps[12]);} /*25,24,*23*/

  Ull x1 = (pp[0]+pp[1]+pp[2]+pp[3]+pp[4]+pp[5]+pp[6]+pp[7]+pp[8]+pp[9]+pp[10]+pp[11]+pp[12]);
  if (info) { printf("//x1(sum of pp)=%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x1>>32), (Uint)x1, (Uint)(x1>>23));}
  Ull x2 = (Ull)s2.frac * (Ull)s3.frac;
  if (info) { printf("//x2(s2 * s3)  =%08.8x_%08.8x ->>23 %08.8x\n", (Uint)(x2>>32), (Uint)x2, (Uint)(x2>>23));}

  /**************************************************************************************************************/
  /***  csa tree  ***********************************************************************************************/
  /**************************************************************************************************************/
  csa_line(&C1[0], &S1[0], pp[ 0], pp[ 1], pp[ 2]);
  csa_line(&C1[1], &S1[1], pp[ 3], pp[ 4], pp[ 5]);
  csa_line(&C1[2], &S1[2], pp[ 6], pp[ 7], pp[ 8]);
  csa_line(&C1[3], &S1[3], pp[ 9], pp[10], pp[11]);

  csa_line(&C2[0], &S2[0], S1[ 0], C1[ 0], S1[ 1]);
  csa_line(&C2[1], &S2[1], C1[ 1], S1[ 2], C1[ 2]);
  csa_line(&C2[2], &S2[2], S1[ 3], C1[ 3], pp[12]);

  csa_line(&C3[0], &S3[0], S2[ 0], C2[ 0], S2[ 1]);
  csa_line(&C3[1], &S3[1], C2[ 1], S2[ 2], C2[ 2]);

  csa_line(&C4,    &S4,    S3[ 0], C3[ 0], S3[ 1]);
  csa_line(&C5,    &S5,    S4,     C4,     C3[ 1]);

  ex1_d.csa_s = S5; // sum   ★★★ガード対応必要 >>32無しなら不要
  ex1_d.csa_c = C5; // carry ★★★ガード対応必要 >>32無しなら不要

  ex1_d.zero = (s2.zero && !s3.inf && !s3.nan) || (s3.zero && !s2.inf && !s2.nan);
  ex1_d.inf  = (s2.inf && !s3.zero && !s3.nan) || (s3.inf && !s2.zero && !s2.nan);
  ex1_d.nan  = s2.nan || s3.nan || (s2.inf && s3.zero) || (s3.inf && s2.zero);

  if (info) {
    printf("//S5           =%08.8x_%08.8x\n", (Uint)(S5>>32), (Uint)S5);
    printf("//C5           =%08.8x_%08.8x\n", (Uint)(C5>>32), (Uint)C5);
    printf("//++(48bit)    =%08.8x_%08.8x\n", (Uint)((C5+S5)>>32), (Uint)(C5+S5));
    printf("//csa_s        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_s>>32), (Uint)ex1_d.csa_s);
    printf("//csa_c        =%08.8x_%08.8x\n", (Uint)((Ull)ex1_d.csa_c>>32), (Uint)ex1_d.csa_c);
    printf("//ex1_d: %x %02.2x +=%08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)(ex1_d.csa_c+ex1_d.csa_s)>>32), (Uint)(ex1_d.csa_c+ex1_d.csa_s));
  }

  /**************************************************************************************************************/
  /***  3in-csa  ************************************************************************************************/
  /**************************************************************************************************************/
  fadd_s1.s    = s1.s;
  fadd_s1.exp  = (0<s1.exp&&s1.exp<255)?(s1.exp-1):s1.exp;
  fadd_s1.frac = (0<s1.exp&&s1.exp<255)?(Ull)s1.frac<<(23+1):(Ull)s1.frac<<23;//★★★0->23
  fadd_s1.zero = s1.zero;
  fadd_s1.inf  = s1.inf;
  fadd_s1.nan  = s1.nan;

  /* nan  + any  -> nan */
  /* inf  + -inf -> nan */
  /* inf  + (~-inf & ~nan) -> inf */
  /* -inf + (~inf  & ~nan) -> inf */
  fadd_w.exp_comp      = fadd_s1.exp>ex1_d.exp?1:0;
  fadd_w.exp_diff      = fadd_w.exp_comp?(fadd_s1.exp-ex1_d.exp):(ex1_d.exp-fadd_s1.exp);
  if (fadd_w.exp_diff>48) fadd_w.exp_diff=48;//★★★25->48
  fadd_w.align_exp     = fadd_w.exp_comp?fadd_s1.exp:ex1_d.exp;
  fadd_w.s1_align_frac = fadd_s1.frac>>(fadd_w.exp_comp?0:fadd_w.exp_diff);
  fadd_w.s2_align_frac = ex1_d.csa_s >>(ex1_d.zero?48:fadd_w.exp_comp?fadd_w.exp_diff:0);
  fadd_w.s3_align_frac = ex1_d.csa_c >>(ex1_d.zero?48:fadd_w.exp_comp?fadd_w.exp_diff:0);

  if (info) {
    printf("//fadd_s1: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", fadd_s1.s, fadd_s1.exp, (Uint)((Ull)fadd_s1.frac>>32), (Uint)fadd_s1.frac, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s1_align_frac>>32), (Uint)fadd_w.s1_align_frac);
    printf("//csa_s: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_s>>32), (Uint)ex1_d.csa_s, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s2_align_frac>>32), (Uint)fadd_w.s2_align_frac);
    printf("//csa_c: %x %02.2x %08.8x_%08.8x (%x)-> %x %08.8x_%08.8x\n", ex1_d.s, ex1_d.exp, (Uint)((Ull)ex1_d.csa_c>>32), (Uint)ex1_d.csa_c, fadd_w.exp_diff, fadd_w.align_exp, (Uint)((Ull)fadd_w.s3_align_frac>>32), (Uint)fadd_w.s3_align_frac);
  }

  /*ex2_d.frac0       =  fadd_w.s1_align_frac+ (fadd_w.s2_align_frac+fadd_w.s3_align_frac);                        */
  /*ex2_d.frac1       =  fadd_w.s1_align_frac+~(fadd_w.s2_align_frac+fadd_w.s3_align_frac)+1;                      */
  /*ex2_d.frac2       = ~fadd_w.s1_align_frac+ (fadd_w.s2_align_frac+fadd_w.s3_align_frac)+1;                      */
  /*ex2_d.frac        = (fadd_s1.s==ex1_d.s) ? ex2_d.frac0 : (ex2_d.frac2 & 0x2000000) ? ex2_d.frac1 : ex2_d.frac2;*/
  /*printf("ex2d.frac0: %08.8x\n", ex2_d.frac0);*/
  /*printf("ex2d.frac1: %08.8x\n", ex2_d.frac1);*/
  /*printf("ex2d.frac2: %08.8x\n", ex2_d.frac2);*/
  /*printf("ex2d.frac:  %08.8x\n", ex2_d.frac );*/
  csa_line(&C6[0], &S6[0],  fadd_w.s1_align_frac,  fadd_w.s2_align_frac,  fadd_w.s3_align_frac);
  csa_line(&C6[1], &S6[1],  fadd_w.s1_align_frac, ~(Ull)fadd_w.s2_align_frac, ~(Ull)fadd_w.s3_align_frac);
  csa_line(&C7[1], &S7[1],  C6[1]|1LL,             S6[1],                 1LL);
  csa_line(&C6[2], &S6[2], ~(Ull)fadd_w.s1_align_frac,  fadd_w.s2_align_frac,  fadd_w.s3_align_frac);
  csa_line(&C7[2], &S7[2],  C6[2]|1LL,             S6[2],                 0LL);

  if (info) {
    printf("//C6[0]=%08.8x_%08.8x(a+c+s)\n",   (Uint)(C6[0]>>32), (Uint)C6[0]);
    printf("//S6[0]=%08.8x_%08.8x(a+c+s)\n",   (Uint)(S6[0]>>32), (Uint)S6[0]);
    printf("//C6[1]=%08.8x_%08.8x(a-c-s)\n",   (Uint)(C6[1]>>32), (Uint)C6[1]);
    printf("//S6[1]=%08.8x_%08.8x(a-c-s)\n",   (Uint)(S6[1]>>32), (Uint)S6[1]);
    printf("//C7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(C7[1]>>32), (Uint)C7[1]);
    printf("//S7[1]=%08.8x_%08.8x(c6+s6+2)\n", (Uint)(S7[1]>>32), (Uint)S7[1]);
    printf("//C6[2]=%08.8x_%08.8x(c+s-a)\n",   (Uint)(C6[2]>>32), (Uint)C6[2]);
    printf("//S6[2]=%08.8x_%08.8x(c+s-a)\n",   (Uint)(S6[2]>>32), (Uint)S6[2]);
    printf("//C7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(C7[2]>>32), (Uint)C7[2]);
    printf("//S7[2]=%08.8x_%08.8x(c6+s6+1)\n", (Uint)(S7[2]>>32), (Uint)S7[2]);
  }

  /**************************************************************************************************************/
  /***  2in-add  ************************************************************************************************/
  /**************************************************************************************************************/
  ex2_d.frac0       =  C6[0]+S6[0]; /* 49bit */
  ex2_d.frac1       =  C7[1]+S7[1]; /* 48bit */
  ex2_d.frac2       =  C7[2]+S7[2]; /* 49bit */

  if (info) {
    printf("//ex2_d.frac0=%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac0>>32), (Uint)ex2_d.frac0);
    printf("//ex2_d.frac1=%08.8x_%08.8x(a-c-s)\n", (Uint)((Ull)ex2_d.frac1>>32), (Uint)ex2_d.frac1);
    printf("//ex2_d.frac2=%08.8x_%08.8x(c+s-a)\n", (Uint)((Ull)ex2_d.frac2>>32), (Uint)ex2_d.frac2);
  }

  ex2_d.s           = (fadd_s1.s==ex1_d.s) ? fadd_s1.s   : (ex2_d.frac2 & 0x1000000000000LL) ? fadd_s1.s : ex1_d.s;
  ex2_d.exp         = fadd_w.align_exp;
  ex2_d.frac        = (fadd_s1.s==ex1_d.s) ? ex2_d.frac0 : (ex2_d.frac2 & 0x1000000000000LL) ? ex2_d.frac1 : ex2_d.frac2 & 0xffffffffffffLL; /* 49bit */
  ex2_d.zero        = ex2_d.frac==0;
  ex2_d.inf         = (!fadd_s1.s && fadd_s1.inf && !( ex1_d.s   && ex1_d.inf)   && !ex1_d.nan)
                   || ( fadd_s1.s && fadd_s1.inf && !(!ex1_d.s   && ex1_d.inf)   && !ex1_d.nan)
                   || (!ex1_d.s   && ex1_d.inf   && !( fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan)
                   || ( ex1_d.s   && ex1_d.inf   && !(!fadd_s1.s && fadd_s1.inf) && !fadd_s1.nan) ;
  ex2_d.nan         = fadd_s1.nan || ex1_d.nan;

  if (info) {
    printf("//ex2_d.frac =%08.8x_%08.8x(a+c+s)\n", (Uint)((Ull)ex2_d.frac>>32), (Uint)ex2_d.frac);
  }

#define FLOAT_PZERO 0x00000000
#define FLOAT_NZERO 0x80000000
#define FLOAT_PINF  0x7f800000
#define FLOAT_NINF  0xff800000
#define FLOAT_NAN   0xffc00000

  /**************************************************************************************************************/
  /***  normalize  **********************************************************************************************/
  /**************************************************************************************************************/
#if 1
  ex3_w.lzc          = (ex2_d.frac & 0x1000000000000LL)?62 :
                       (ex2_d.frac & 0x0800000000000LL)?63 :
                       (ex2_d.frac & 0x0400000000000LL)? 0 :
                       (ex2_d.frac & 0x0200000000000LL)? 1 :
                       (ex2_d.frac & 0x0100000000000LL)? 2 :
                       (ex2_d.frac & 0x0080000000000LL)? 3 :
                       (ex2_d.frac & 0x0040000000000LL)? 4 :
                       (ex2_d.frac & 0x0020000000000LL)? 5 :
                       (ex2_d.frac & 0x0010000000000LL)? 6 :
                       (ex2_d.frac & 0x0008000000000LL)? 7 :
                       (ex2_d.frac & 0x0004000000000LL)? 8 :
                       (ex2_d.frac & 0x0002000000000LL)? 9 :
                       (ex2_d.frac & 0x0001000000000LL)?10 :
                       (ex2_d.frac & 0x0000800000000LL)?11 :
                       (ex2_d.frac & 0x0000400000000LL)?12 :
                       (ex2_d.frac & 0x0000200000000LL)?13 :
                       (ex2_d.frac & 0x0000100000000LL)?14 :
                       (ex2_d.frac & 0x0000080000000LL)?15 :
                       (ex2_d.frac & 0x0000040000000LL)?16 :
                       (ex2_d.frac & 0x0000020000000LL)?17 :
                       (ex2_d.frac & 0x0000010000000LL)?18 :
                       (ex2_d.frac & 0x0000008000000LL)?19 :
                       (ex2_d.frac & 0x0000004000000LL)?20 :
                       (ex2_d.frac & 0x0000002000000LL)?21 :
                       (ex2_d.frac & 0x0000001000000LL)?22 :
                       (ex2_d.frac & 0x0000000800000LL)?23 :
                       (ex2_d.frac & 0x0000000400000LL)?24 :
                       (ex2_d.frac & 0x0000000200000LL)?25 :
                       (ex2_d.frac & 0x0000000100000LL)?26 :
                       (ex2_d.frac & 0x0000000080000LL)?27 :
                       (ex2_d.frac & 0x0000000040000LL)?28 :
                       (ex2_d.frac & 0x0000000020000LL)?29 :
                       (ex2_d.frac & 0x0000000010000LL)?30 :
                       (ex2_d.frac & 0x0000000008000LL)?31 :
                       (ex2_d.frac & 0x0000000004000LL)?32 :
                       (ex2_d.frac & 0x0000000002000LL)?33 :
                       (ex2_d.frac & 0x0000000001000LL)?34 :
                       (ex2_d.frac & 0x0000000000800LL)?35 :
                       (ex2_d.frac & 0x0000000000400LL)?36 :
                       (ex2_d.frac & 0x0000000000200LL)?37 :
                       (ex2_d.frac & 0x0000000000100LL)?38 :
                       (ex2_d.frac & 0x0000000000080LL)?39 :
                       (ex2_d.frac & 0x0000000000040LL)?40 :
                       (ex2_d.frac & 0x0000000000020LL)?41 :
                       (ex2_d.frac & 0x0000000000010LL)?42 :
                       (ex2_d.frac & 0x0000000000008LL)?43 :
                       (ex2_d.frac & 0x0000000000004LL)?44 :
                       (ex2_d.frac & 0x0000000000002LL)?45 :
                       (ex2_d.frac & 0x0000000000001LL)?46 :
                                                        47 ;
  if (info) {
    printf("//ex2:%x %x %08.8x_%08.8x ", ex2_d.s, ex2_d.exp, (Uint)((Ull)ex2_d.frac>>32), (Uint)ex2_d.frac);
  }

  if (ex2_d.nan) {
    ex3_d.s    = 1;
    ex3_d.frac = 0x400000;
    ex3_d.exp  = 0xff;

  }
  else if (ex2_d.inf) {
    ex3_d.s    = ex2_d.s;
    ex3_d.frac = 0x000000;
    ex3_d.exp  = 0xff;
  }
  else if (ex3_w.lzc == 62) {//★★★
    if (info) {
      printf("lzc==%d\n", ex3_w.lzc);
    }
    if (ex2_d.exp >= 253) {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = 0x000000;
      ex3_d.exp  = 0xff;
    }
    else {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = (ex2_d.frac>>2)>>23; //★★★ガード対応必要
      ex3_d.exp  = ex2_d.exp + 2;
    }
  }
  else if (ex3_w.lzc == 63) {//★★★
    if (info) {
      printf("lzc==%d\n", ex3_w.lzc);
    }
    if (ex2_d.exp >= 254) {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = 0x000000;
      ex3_d.exp  = 0xff;
    }
    else {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = (ex2_d.frac>>1)>>23; //★★★ガード対応必要
      ex3_d.exp  = ex2_d.exp + 1;
    }
  }
  else if (ex3_w.lzc <= 46) {//★★★
    if (info) {
      printf("lzc==%d\n", ex3_w.lzc);
    }
    if (ex2_d.exp >= ex3_w.lzc + 255) {
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = 0x000000;
      ex3_d.exp  = 0xff;
    }
    else if (ex2_d.exp <= ex3_w.lzc) { /* subnormal num */
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = (ex2_d.frac<<ex2_d.exp)>>23; //★★★ガード対応必要
      ex3_d.exp  = 0x00;
    }
    else { /* normalized num */
      ex3_d.s    = ex2_d.s;
      ex3_d.frac = (ex2_d.frac<<ex3_w.lzc)>>23; //★★★ガード対応必要
      ex3_d.exp  = ex2_d.exp - ex3_w.lzc;
    }
#define NO_GUARD_BITS
#ifndef NO_GUARD_BITS
    int f_ulp = (ex2_d.frac<<ex3_w.lzc)>>23&1;
    int f_g   = (ex2_d.frac<<ex3_w.lzc)>>22&1;
    int f_r   = (ex2_d.frac<<ex3_w.lzc)>>21&1;
    int f_s   =((ex2_d.frac<<ex3_w.lzc)&0x1fffff)!=0;
    switch (f_ulp<<3|f_g<<2|f_r<<1|f_s) {
    case 0: case 1: case 2: case 3: case 4: /* ulp|G|R|S */
    case 8: case 9: case 10: case 11:
      break;
    case 5: case 6: case 7: /* ulp++ */
    case 12: case 13: case 14: case 15: default:
      if (info)
	printf("//ex3:%x %x %x++ -> ", ex3_d.s, ex3_d.exp, ex3_d.frac);
      ex3_d.frac++;
      if (info)
	printf("%x\n", ex3_d.frac);
      break;
    }
#endif
  }
  else { /* zero */
    if (info) {
      printf("zero\n");
    }
    ex3_d.s    = 0;
    ex3_d.frac = 0x000000;
    ex3_d.exp  = 0x00;
  }
#endif

  if (info) {
    printf("//ex3:%x %x %x\n", ex3_d.s, ex3_d.exp, ex3_d.frac);
  }

  out.raw.w  = (ex3_d.s<<31)|(ex3_d.exp<<23)|(ex3_d.frac);
  org.flo.w  = i1+i2*i3;
  Uint diff = out.raw.w>org.raw.w ? out.raw.w-org.raw.w : org.raw.w-out.raw.w;

  if (!info)
    sprintf(hardbuf64, "%8.8e:%08.8x %8.8e:%08.8x %8.8e:%08.8x ->%8.8e:%08.8x (%8.8e:%08.8x) %08.8x %s%s%s",
           in1.flo.w, in1.raw.w, in2.flo.w, in2.raw.w, in3.flo.w, in3.raw.w, out.flo.w, out.raw.w, org.flo.w, org.raw.w, diff,
           diff>=TH1 ? "H":"",
           diff>=TH2 ? "H":"",
           diff>=TH3 ? "H":""
           );
  *o = out.flo.w;
  return(diff);
}