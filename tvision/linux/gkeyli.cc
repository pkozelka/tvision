/* Copyright (C) 1996-1998 Robert H�hne, see COPYING.RH for details */
/* This file is part of RHIDE. */
/*****************************************************************************

 Keyboard handler for Linux by Salvador E. Tropea (SET) (1998)

 That's the counterpart of the gkey.cc for DOS that I created some months ago
it uses curses input and tries to detect the shift/control status from the
keycode reported by curses. Additionally the routines detects ALT using the
ESC-keycode sequence (META+key) with a patch in the low level tv_GetRaw.

RH: I moved that patch to the TGKey::gkey() member

 To make it better in the Linux console I'm mixing the modifiers flags from
the ioctl call with the ones I figured out from the code.

 It fixes a lot of problems that appears when running from telnet, but not
all. Lamentably looks like telnet standard protocols can't simply report all
the keyboard combinations. We lose some important things like Ctrl+Function
key, Shift+(Inset,End,Home,PgUp,PgDn,Delete,Arrows,etc.) and more.

*****************************************************************************/
#ifdef __linux__

#define Uses_TEvent
#define Uses_TGKey
#define Uses_FullSingleKeySymbols
#include <tv.h>
#include <stdio.h>

#include <sys/time.h>

#include <unistd.h>
#include <ctype.h>
#ifdef __FreeBSD__
#include <ncurses.h>
#else
#include <curses.h>
#endif
#include <term.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/vt.h>
#include <signal.h>

#ifndef GKEY
#define dbprintf(a...)
#else
#define dbprintf(a...) printf(a)
#endif

/* Linux IOCTL values found experimentally */
const int kblNormal=0,kblShift=1,kblAltR=2,kblCtrl=4,kblAltL=8;

int use_real_keyboard_bios = 0;
int convert_num_pad = 0;

#if 0
static int timeout_esc = -1;
#endif
extern int timer_value;

/*
 * This is the time limit in ms within Esc-key sequences are detected as
 * Alt-letter sequences.  Useful when we can't generate Alt-letter sequences
 * directly.
 */
#define DELAY_ESCAPE		400
#define META_MASK 0x8000

/*
 * Gets information about modifier keys (Alt, Ctrl and Shift).  This can
 * be done only if the program runs on the system console.
 */

#define IN_FD fileno(stdin)

unsigned short getshiftstate(void)
{
 int arg = 6;	/* TIOCLINUX function #6 */
 int shift = 0;
 
 if (ioctl(IN_FD, TIOCLINUX, &arg) != -1)
   {
    dbprintf("Shift flags from IOCTL %X\r\n",arg);
    shift=arg;
   }
 return shift;
}

unsigned short gkey_shifts_flags;
unsigned char gkey_raw_value;
unsigned short gkey_raw_code;

#define _getch_ getch
#define _ungetch_ ungetch

// -  9 = Tab tiene conflicto con kbI+Control lo cual es natural, por otro
// -lado Ctrl+Tab no lo reporta en forma natural
// -  a = Enter tiene conflicto con ^J, como ^J no lo reporta nauralmente sino
// -forzado por el keymap lo mejor es definirlo directamente.
static
unsigned char kbToName1[128] =
{
 0,kbA,kbB,kbC,kbD,kbE,kbF,kbG,                                           // 00-07
 kbH,kbTab,kbEnter,kbK,kbL,kbM,kbN,kbO,                                   // 08-0F
 kbP,kbQ,kbR,kbS,kbT,kbU,kbV,kbW,                                         // 10-17
 kbX,kbY,kbZ,kbEsc,0,kbCloseBrace,kb6,kbMinus,                            // 18-1F
 kbSpace,kbAdmid,kbDobleQuote,kbNumeral,kbDolar,kbPercent,kbAmper,kbQuote,// 20-27
 kbOpenPar,kbClosePar,kbAsterisk,kbPlus,kbComma,kbMinus,kbStop,kbSlash,   // 28-2F
 kb0,kb1,kb2,kb3,kb4,kb5,kb6,kb7,                                         // 30-37
 kb8,kb9,kbDoubleDot,kbColon,kbLessThan,kbEqual,kbGreaterThan,kbQuestion, // 38-3F
 kbA_Roba,kbA,kbB,kbC,kbD,kbE,kbF,kbG,                                    // 40-47
 kbH,kbI,kbJ,kbK,kbL,kbM,kbN,kbO,                                         // 48-4F
 kbP,kbQ,kbR,kbS,kbT,kbU,kbV,kbW,                                         // 50-57
 kbX,kbY,kbZ,kbOpenBrace,kbBackSlash,kbCloseBrace,kbCaret,kbUnderLine,    // 58-5F
 kbGrave,kbA,kbB,kbC,kbD,kbE,kbF,kbG,                                     // 60-67
 kbH,kbI,kbJ,kbK,kbL,kbM,kbN,kbO,                                         // 68-6F
 kbP,kbQ,kbR,kbS,kbT,kbU,kbV,kbW,                                         // 70-77
 kbX,kbY,kbZ,kbOpenCurly,kbOr,kbCloseCurly,kbTilde,kbBackSpace            // 78-7F
};

// 15e = kb5 (del keypad)
static
unsigned char kbToName2[128] =
{
 0,0,kbDown,kbUp,kbLeft,kbRight,kbHome,kbBackSpace, // 00-07
 0,kbF1,kbF2,kbF3,kbF4,kbF5,kbF6,kbF7,              // 08-0F
 kbF8,kbF9,kbF10,kbF1,kbF2,kbF3,kbF4,kbF5,          // 10-17
 kbF6,kbF7,kbF8,kbF9,kbF10,0,0,0,                   // 18-1F
 0,0,0,0,0,0,0,0,                                   // 20-27
 0,0,0,0,0,0,0,0,                                   // 28-2F
 0,0,0,0,0,0,0,0,                                   // 30-37
 0,0,0,0,0,0,0,0,                                   // 38-3F
 0,0,0,0,0,0,0,0,                                   // 40-47
 0,0,kbDelete,kbInsert,0,0,0,0,                     // 48-4F
 0,0,kbPgDn,kbPgUp,0,0,0,0,                         // 50-57
 0,0,0,0,0,0,kb5,0,                                 // 58-5F
 0,0,0,0,0,0,0,0,                                   // 60-67
 kbEnd,0,0,0,0,0,0,0,                               // 68-6F
 0,0,0,0,0,0,0,0,                                   // 70-77
 0,0,0,0,0,0,0,kbBackSpace                          // 78-7F
};

const unsigned kbCtrl =kbCtrlCode>>4;
const unsigned kbAlt  =kbAltLCode>>4;
const unsigned char kbShift=kbShiftCode>>4;

static
unsigned char kbExtraFlags1[128] =
{
 0,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,              // 00-07
 kbCtrl,0,0,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,                   // 08-0F
 kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,         // 10-17
 kbCtrl,kbCtrl,kbCtrl,0,0,kbCtrl,kbCtrl,kbCtrl,                   // 18-1F
 0,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,0,             // 20-27
 kbShift,kbShift,kbShift,kbShift,0,0,0,0,                         // 28-2F
 0,0,0,0,0,0,0,0,                                                 // 30-37
 0,0,kbShift,0,kbShift,0,kbShift,kbShift,                         // 38-3F
 kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift, // 40-47
 kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift, // 48-4F
 kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift, // 50-57
 kbShift,kbShift,kbShift,0,0,0,kbShift,kbShift,                   // 58-5F
 0,0,0,0,0,0,0,0,                                                 // 60-67
 0,0,0,0,0,0,0,0,                                                 // 68-6F
 0,0,0,0,0,0,0,0,                                                 // 70-77
 0,0,0,kbShift,kbShift,kbShift,kbShift,0,                         // 78-7F
};

static
unsigned char kbExtraFlags2[128] =
{
 0,0,0,0,0,0,0,0,                               // 00-07
 0,0,0,0,0,0,0,0,                               // 08-0F
 0,0,0,kbShift,kbShift,kbShift,kbShift,kbShift, // 10-17
 kbShift,kbShift,kbShift,kbShift,kbShift,0,0,0, // 18-1F
 0,0,0,0,0,0,0,0,                               // 20-27
 0,0,0,0,0,0,0,0,                               // 28-2F
 0,0,0,0,0,0,0,0,                               // 30-37
 0,0,0,0,0,0,0,0,                               // 38-3F
 0,0,0,0,0,0,0,0,                               // 40-47
 0,0,0,0,0,0,0,0,                               // 48-4F
 0,0,0,0,0,0,0,0,                               // 50-57
 0,0,0,0,0,0,0,0,                               // 58-5F
 0,0,0,0,0,0,0,0,                               // 60-67
 0,0,0,0,0,0,0,0,                               // 68-6F
 0,0,0,0,0,0,0,0,                               // 70-77
 0,0,0,0,0,0,0,kbAlt                            // 78-7F
};

// xterm is a crappy terminal and does all in a way too different to the
// standard.
static int XtermMode=0;

static
void PatchTablesForOldKbdLayout(void)
{
 // That's the most common at the moment (Debian <= 2.0, RedHat <= 5.1, etc)
 unsigned char names[]=
 {kbF1,kbF2,kbF3,kbF4,kbF5,kbF6,kbF7,kbF8,kbF9,kbF10,
     0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
     0,   0,   0,   0,   0,   0};
 unsigned char modif[]=
 {kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,
     0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
     0,   0,   0,   0,   0,   0};
 int i;
 for (i=0; i<26; i++)
    {
     kbToName2[i+0x13]=names[i];
     kbExtraFlags2[i+0x13]=modif[i];
    }
}

static
void PatchTablesForNewKbdLayout(void)
{
 // Tom Aschenbrenner <tom@aschen.com> found problems on Red Hat 5.2, after
 // some tests we found that the new kbd package changed some stuff.
 unsigned char names[]=
 {kbF11,kbF12,
   kbF1,kbF2,kbF3,kbF4,kbF5,kbF6,kbF7,kbF8,kbF9,kbF10,kbF11,kbF12,
   kbF1,kbF2,kbF3,kbF4,kbF5,kbF6,kbF7,kbF8,kbF9,kbF10,kbF11,kbF12 };
 unsigned char modif[]=
 {      0,      0,
  kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,kbShift,
  kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl,kbCtrl };
 // This layout have much sense but is a pain in the ... for my tables
 int i;
 for (i=0; i<26; i++)
    {
     kbToName2[i+0x13]=names[i];
     kbExtraFlags2[i+0x13]=modif[i];
    }
}

void TGKey::SetKbdMapping(int version)
{
 switch (version)
   {
    case KBD_REDHAT52_STYLE:
         PatchTablesForNewKbdLayout();
         break;
    case KBD_XTERM_STYLE: // It can be combined with others
         XtermMode=1;
         break;
    case KBD_NO_XTERM_STYLE:
         XtermMode=0;
         break;
    default: // KBD_OLD_STYLE
         PatchTablesForOldKbdLayout();
   }
}

// The intelligence is here
unsigned short TGKey::gkey(void)
{
 Abstract=0;

 GetRaw();
 if (rawCode.full & META_MASK)
   {
    sFlags|=kblAltL;
    rawCode.full &= ~META_MASK;
    dbprintf("Adding left alt because the code contains META key\r\n");
   }
 else
   if (XtermMode && (rawCode.full & 0x80))
     {
      sFlags|=kblAltL;
      rawCode.full &= ~0x80;
      dbprintf("Adding left alt because the code contains 0x80 and xterm detected\r\n");
     }
 
 //---- The following code takes advantage of the flags reported by the ioctl
 //---- call. As this mechanism isn't available if we aren't loged in the
 //---- console I add more information later.
 
 // Compose the shift flags:
 if (sFlags & kblShift)
    Abstract|=kbShiftCode;
 if (sFlags & kblCtrl)
    Abstract|=kbCtrlCode;
 switch (AltSet)
   {
    case 0: // Normal thing, left is left, right is right
         if (sFlags & kblAltL)
            Abstract|=kbAltLCode;
         else
            if (sFlags & kblAltR)
               Abstract|=kbAltRCode;
         break;
    case 1: // Reverse thing
         if (sFlags & kblAltL)
            Abstract|=kbAltRCode;
         else
            if (sFlags & kblAltR)
               Abstract|=kbAltLCode;
         break;
    default: // Compatibility
         if (sFlags & (kblAltL | kblAltR))
            Abstract|=kbAltLCode;
   }

 //--- Translate the rest
 unsigned char *name,*flags;
 // This values could be 0 or 1 because curses reports upto 512 keys
 if (rawCode.b.scan)
   {
    name=kbToName2;
    flags=kbExtraFlags2;
   }
 else
   {
    name=kbToName1;
    flags=kbExtraFlags1;
   }
 unsigned key=rawCode.full;
 if (key & 0x80)
   { // Keys over 127 are interpreted as ASCII
    ascii=key;
    rawCode.b.scan=ascii;
    //Abstract|=kbUnknown;
   }
 else
   { // The rest are passed by the tables
    key&=0x7F;
    ascii=key;
    rawCode.b.scan=ascii;
    Abstract|=name[key] | (flags[key]<<4);
    sFlags|=(flags[key]<<4);
   }
 return rawCode.full;
}

// All the info. from BIOS in one call
void TGKey::GetRaw(void)
{
 int code;

 /* see if there is data available */
 if ((code = _getch_()) != ERR)
   {
    if (code == 27)
      {
       if (kbhit())
         {
          int code2 = _getch_();
          if (code2 != 27)
             code = code2 | META_MASK;
         }
      }
   }
 else
   {
    rawCode.full=0;
    sFlags=0;
    return;
   }
 rawCode.full=code;
 sFlags=getshiftstate();
}

int TGKey::kbhit(void)
{
  int c = _getch_();
  if (c != ERR) _ungetch_(c);
  return c != ERR;
}

// Who knows what's that?
#undef clear
void TGKey::clear(void)
{
  tcflush(IN_FD,TCIFLUSH);
}

typedef struct
{
  uchar change_table;
  uchar change_index;
  uchar old_table;
  uchar old_index;
  ushort old_val;
  ushort new_val;
} change_entry;

#define SCAN_F1 0x3b
#define SCAN_F2 0x3c
#define SCAN_F3 0x3d
#define SCAN_F4 0x3e
#define SCAN_F5 0x3f
#define SCAN_F6 0x40
#define SCAN_F7 0x41
#define SCAN_F8 0x42
#define SCAN_F9 0x43
#define SCAN_F10 0x44
#define SCAN_Q 0x10
#define SCAN_S 0x1f
#define SCAN_J 0x24
#define SCAN_M 0x32
#define SCAN_PGUP 104
#define SCAN_PGDN 109
#define SCAN_BKSP 14
#define SCAN_SPAC 57

change_entry changes[] = {
  { kblAltL,  SCAN_F1,   kblNormal, SCAN_F1,   0, 0},
  { kblAltR,  SCAN_F1,   kblNormal, SCAN_F1,   0, 0},
  { kblCtrl,  SCAN_F1,   kblNormal, SCAN_F1,   0, 0},
  { kblShift, SCAN_F1,   kblNormal, SCAN_F1,   0, 0},
  { kblAltL,  SCAN_F2,   kblNormal, SCAN_F2,   0, 0},
  { kblAltR,  SCAN_F2,   kblNormal, SCAN_F2,   0, 0},
  { kblCtrl,  SCAN_F2,   kblNormal, SCAN_F2,   0, 0},
  { kblShift, SCAN_F2,   kblNormal, SCAN_F2,   0, 0},
  { kblAltL,  SCAN_F3,   kblNormal, SCAN_F3,   0, 0},
  { kblAltR,  SCAN_F3,   kblNormal, SCAN_F3,   0, 0},
  { kblCtrl,  SCAN_F3,   kblNormal, SCAN_F3,   0, 0},
  { kblShift, SCAN_F3,   kblNormal, SCAN_F3,   0, 0},
  { kblAltL,  SCAN_F4,   kblNormal, SCAN_F4,   0, 0},
  { kblAltR,  SCAN_F4,   kblNormal, SCAN_F4,   0, 0},
  { kblCtrl,  SCAN_F4,   kblNormal, SCAN_F4,   0, 0},
  { kblShift, SCAN_F4,   kblNormal, SCAN_F4,   0, 0},
  { kblAltL,  SCAN_F5,   kblNormal, SCAN_F5,   0, 0},
  { kblAltR,  SCAN_F5,   kblNormal, SCAN_F5,   0, 0},
  { kblCtrl,  SCAN_F5,   kblNormal, SCAN_F5,   0, 0},
  { kblShift, SCAN_F5,   kblNormal, SCAN_F5,   0, 0},
  { kblAltL,  SCAN_F6,   kblNormal, SCAN_F6,   0, 0},
  { kblAltR,  SCAN_F6,   kblNormal, SCAN_F6,   0, 0},
  { kblCtrl,  SCAN_F6,   kblNormal, SCAN_F6,   0, 0},
  { kblShift, SCAN_F6,   kblNormal, SCAN_F6,   0, 0},
  { kblAltL,  SCAN_F7,   kblNormal, SCAN_F7,   0, 0},
  { kblAltR,  SCAN_F7,   kblNormal, SCAN_F7,   0, 0},
  { kblCtrl,  SCAN_F7,   kblNormal, SCAN_F7,   0, 0},
  { kblShift, SCAN_F7,   kblNormal, SCAN_F7,   0, 0},
  { kblAltL,  SCAN_F8,   kblNormal, SCAN_F8,   0, 0},
  { kblAltR,  SCAN_F8,   kblNormal, SCAN_F8,   0, 0},
  { kblCtrl,  SCAN_F8,   kblNormal, SCAN_F8,   0, 0},
  { kblShift, SCAN_F8,   kblNormal, SCAN_F8,   0, 0},
  { kblAltL,  SCAN_F9,   kblNormal, SCAN_F9,   0, 0},
  { kblAltR,  SCAN_F9,   kblNormal, SCAN_F9,   0, 0},
  { kblCtrl,  SCAN_F9,   kblNormal, SCAN_F9,   0, 0},
  { kblShift, SCAN_F9,   kblNormal, SCAN_F9,   0, 0},
  { kblAltL,  SCAN_F10,  kblNormal, SCAN_F10,  0, 0},
  { kblAltR,  SCAN_F10,  kblNormal, SCAN_F10,  0, 0},
  { kblCtrl,  SCAN_F10,  kblNormal, SCAN_F10,  0, 0},
  { kblShift, SCAN_F10,  kblNormal, SCAN_F10,  0, 0},
  { kblCtrl,  SCAN_Q,    kblNormal, SCAN_Q,    0, 0},
  { kblCtrl,  SCAN_S,    kblNormal, SCAN_S,    0, 0},
  { kblCtrl,  SCAN_J,    kblNormal, SCAN_J,    0, 0},
  { kblCtrl,  SCAN_M,    kblNormal, SCAN_M,    0, 0},
  { kblShift, SCAN_PGUP, kblNormal, SCAN_PGUP, 0, 0},
  { kblShift, SCAN_PGDN, kblNormal, SCAN_PGDN, 0, 0},
  { kblCtrl,  SCAN_BKSP, kblNormal, SCAN_BKSP, 0, 0},
  { kblCtrl,  SCAN_SPAC, kblNormal, SCAN_SPAC, 0, 0}
};

#define change_size (sizeof(changes)/sizeof(change_entry))

static struct termios saved_attributes;

static int keyboard_patch_set = 0;
static int keyboard_patch_available=0;
static struct kbentry entry;

static struct vt_mode oldvtmode;
static int our_vt;
static int console_sigs_set = 0;
static int atexit_done_console_sigs = 0;
int install_console_sigs = 1;

static void done_console_sigs()
{
  struct sigaction sig;

  if (!install_console_sigs)
    return;

  if (!console_sigs_set)
    return;
  sigemptyset(&sig.sa_mask);
  sigaddset(&sig.sa_mask,SIGUSR1);
  sigaddset(&sig.sa_mask,SIGUSR2);
  sig.sa_flags   = SA_RESTART;
  sigprocmask(SIG_BLOCK,&sig.sa_mask,NULL); // No switches now, we are not
                                            // initialized yet
  sig.sa_handler = SIG_DFL;
  sigaction(SIGUSR1,&sig,NULL);
  sig.sa_handler = SIG_DFL;
  sigaction(SIGUSR2,&sig,NULL);

  ioctl(STDIN_FILENO,VT_SETMODE,&oldvtmode);

  sigprocmask(SIG_UNBLOCK,&sig.sa_mask,NULL);

  console_sigs_set = 0;

}

static void _patch_keyboard();
static void _unpatch_keyboard();

static void releasevt_handler(int)
{
  _unpatch_keyboard();
  TMouse::suspend();
  ioctl(STDIN_FILENO,VT_RELDISP,1);
}

static void acquirevt_handler(int)
{
  ioctl(STDIN_FILENO,VT_RELDISP,VT_ACKACQ);
  ioctl(STDIN_FILENO,VT_WAITACTIVE,our_vt);
  _patch_keyboard();
  TMouse::resume();
}

static void init_console_sigs()
{
  if (!install_console_sigs)
    return;

  // -------- Get our console number
  if (sscanf(ttyname(STDOUT_FILENO),"/dev/tty%2d",&our_vt) != 1)
  {
    return;
  }

  // -------- Tell our console to inform us about switches
  struct vt_mode newvtmode;
  if (ioctl(STDIN_FILENO,VT_GETMODE,&newvtmode))
  {
    return;
  }

  // -------- set up signal handlers to know about console switches
  struct sigaction sig;
  sigemptyset(&sig.sa_mask);
  sigaddset(&sig.sa_mask,SIGUSR1);
  sigaddset(&sig.sa_mask,SIGUSR2);
  sig.sa_flags   = SA_RESTART;
  sigprocmask(SIG_BLOCK,&sig.sa_mask,NULL); // No switches now, we are not
                                            // initialized yet
  sig.sa_handler = releasevt_handler;
  sigaction(SIGUSR1,&sig,NULL);
  sig.sa_handler = acquirevt_handler;
  sigaction(SIGUSR2,&sig,NULL);

  oldvtmode = newvtmode;
  newvtmode.mode   = VT_PROCESS;
  newvtmode.relsig = SIGUSR1;
  newvtmode.acqsig = SIGUSR2;
  if (ioctl(STDIN_FILENO,VT_SETMODE,&newvtmode))
  {
    return;
  }

  if (atexit_done_console_sigs)
    atexit(done_console_sigs);
  atexit_done_console_sigs = 1;

  sigprocmask(SIG_UNBLOCK,&sig.sa_mask,NULL);

  console_sigs_set = 1;
}

static int atexit_unpatch_keyboard = 0;

static void patch_keyboard_init()
{
  unsigned i;
  if (use_real_keyboard_bios)
  {
    keyboard_patch_available = 0;
    return;
  }
  if (keyboard_patch_available)
    return;
  for (i=0;i<change_size;i++)
  {
    change_entry *e = &changes[i];
    entry.kb_table = e->change_table;
    entry.kb_index = e->change_index;
    if (ioctl(STDIN_FILENO,KDGKBENT,&entry) != 0)
    {
      keyboard_patch_available = 0;
      return;
    }
    e->old_val = entry.kb_value;
    entry.kb_table = e->old_table;
    entry.kb_index = e->old_index;
    ioctl(STDIN_FILENO,KDGKBENT,&entry);
    e->new_val = entry.kb_value;
  }
  keyboard_patch_available = 1;
  if (!atexit_unpatch_keyboard)
    atexit(_unpatch_keyboard);
  atexit_unpatch_keyboard = 1;
}

static void _patch_keyboard()
{
  unsigned i;
  if (keyboard_patch_set)
    return;
  patch_keyboard_init();
  if (!keyboard_patch_available)
    return;
  for (i=0;i<change_size;i++)
  {
    change_entry *e = &changes[i];
    entry.kb_table = e->change_table;
    entry.kb_index = e->change_index;
    entry.kb_value = e->new_val;
    if (ioctl(STDIN_FILENO,KDSKBENT,&entry) != 0)
    {
      keyboard_patch_available = 0;
      return;
    }
  }
  keyboard_patch_set = 1;
}

void patch_keyboard()
{
  _patch_keyboard();
  if (keyboard_patch_set)
    init_console_sigs();
  cbreak();
  noecho();
}

static void _unpatch_keyboard()
{
  unsigned i;
  if (!keyboard_patch_available)
    return;
  if (!keyboard_patch_set)
    return;
  for (i=0;i<change_size;i++)
  {
    change_entry *e = &changes[i];
    entry.kb_table = e->change_table;
    entry.kb_index = e->change_index;
    entry.kb_value = e->old_val;
    ioctl(STDIN_FILENO,KDSKBENT,&entry);
  }
  keyboard_patch_set = 0;
}

static void unpatch_keyboard()
{
  _unpatch_keyboard();
  if (keyboard_patch_available)
    done_console_sigs();
  echo();
  nocbreak();
}

void resume_keyboard()
{
  tcgetattr (STDIN_FILENO, &saved_attributes);
  patch_keyboard();
}

void suspend_keyboard()
{
  tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
  unpatch_keyboard();
}


#endif // __linux__

