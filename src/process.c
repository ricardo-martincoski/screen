/* Copyright (c) 1991
 *      Juergen Weigert (jnweiger@immd4.informatik.uni-erlangen.de)
 *      Michael Schroeder (mlschroe@immd4.informatik.uni-erlangen.de)
 * Copyright (c) 1987 Oliver Laumann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 1, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Noteworthy contributors to screen's design and implementation:
 *	Wayne Davison (davison@borland.com)
 *	Patrick Wolfe (pat@kai.com, kailand!pat)
 *	Bart Schaefer (schaefer@cse.ogi.edu)
 *	Nathan Glasser (nathan@brokaw.lcs.mit.edu)
 *	Larry W. Virden (lvirden@cas.org)
 *	Howard Chu (hyc@hanauma.jpl.nasa.gov)
 *	Tim MacKenzie (tym@dibbler.cs.monash.edu.au)
 *	Markku Jarvinen (mta@{cc,cs,ee}.tut.fi)
 *	Marc Boucher (marc@CAM.ORG)
 *
 ****************************************************************
 */

#include "rcs.h"
RCS_ID("$Id$ FAU")

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#if !defined(sun) && !defined(B43) && !defined(ISC) && !defined(pyr)
# include <time.h>
#endif
#include <sys/time.h>
#ifndef sun
#include <sys/ioctl.h>
#endif


#include "config.h"
#include "screen.h"
#include "extern.h"

extern struct comm comms[];
extern struct win *fore;
extern char *rc_name;
extern char *RcFileName, *home, *extra_incap, *extra_outcap;
extern char *BellString, *ActivityString, *ShellProg, *ShellArgs[];
extern char *hardcopydir, *screenlogdir;
extern char *VisualBellString;
extern int VBellWait, MsgWait, MsgMinWait;
extern char Esc, MetaEsc;
extern char SockPath[], *SockNamePtr;
extern int TtyMode, auto_detach;
extern int iflag;
extern int default_wrap;
extern use_hardstatus, visual_bell, default_monitor;
extern int default_startup;
extern int slowpaste;
extern struct display *display;
extern struct win *fore, *console_window, *windows;
extern char screenterm[];
extern int intrc, origintrc; /* display? */
extern struct NewWindow nwin_default, nwin_undef;
#ifdef COPY_PASTE
extern int join_with_cr;
extern char mark_key_tab[];
extern char *BufferFile;
#endif
#ifdef POW_DETACH
extern char *BufferFile, *PowDetachString;
#endif

/*
 * XXX: system
 */
extern time_t time __P((time_t *));
#if !defined(BSDI) && !defined(SVR4) && !defined(linux) && !defined(__386BSD__)
extern char *getpass __P((char *));
#endif /* !BSDI && !SVR4 && !linux && !__386BSD__*/


static int  CheckArgNum __P((int, char **));
static void FreeKey __P((int));
static int  NextWindow __P(());
static int  PreviousWindow __P(());
static int  MoreWindows __P(());
static void LogToggle __P((int));
static void ShowWindows __P(());
static void ShowTime __P(());
static void ShowInfo __P(());
static void SwitchWindow __P((int));
static char **SaveArgs __P((char **));
static int  ParseSwitch __P((struct action *, int *));
static int  ParseOnOff __P((struct action *, int *));
static int  ParseSaveStr __P((struct action *act, char **));
static int  ParseNum __P((struct action *act, int *));
static int  ParseOct __P((struct action *act, int *));
static char *ParseChar __P((char *, char *));
static int  IsNum __P((char *, int));
static int  IsNumColon __P((char *, int, char *, int));
static void InputColon __P(());
static void Colonfin __P((char *, int));
static void InputAKA __P(());
static void AKAfin __P((char *, int));
#ifdef COPY_PASTE
static void copy_reg_fn __P((char *, int));
static void ins_reg_fn __P((char *, int));
#endif
static void process_fn __P((char *, int));
#ifdef PASSWORD
static void pass1 __P((char *, int));
static void pass2 __P((char *, int));
#endif
#ifdef POW_DETACH
static void pow_detach_fn __P((char *, int));
#endif



extern struct display *display, *displays;
extern struct win *fore, *windows;

extern char Termcap[], screenterm[], HostName[], version[];
extern char **NewEnv;
extern struct NewWindow nwin_undef, nwin_default;
extern struct LayFuncs WinLf;
extern struct layer BlankLayer;

extern int Z0width, Z1width;
extern int real_uid, real_gid;
extern int visual_bell, default_monitor;

#if defined(TIOCSWINSZ) || defined(TIOCGWINSZ)
extern struct winsize glwz;
#endif

#ifdef NETHACK
extern int nethackflag;
#endif


static struct win *wtab[MAXWIN];	/* window table */
struct action ktab[256];	/* command key translation table */


#ifdef MULTIUSER
static struct acl *acls;	/* access control list anchor */
extern char *multi;
#endif
#ifdef PASSWORD
int CheckPassword;
char Password[20];
#endif

#define MAX_PLOP_DEFS 256

static struct plop
{
  char *buf;
  int len;
} plop_tab[MAX_PLOP_DEFS];


char Esc = Ctrl('a');
char MetaEsc = 'a';
#ifdef PTYMODE
int TtyMode = PTYMODE;
#else
int TtyMode = 0622;
#endif
int hardcopy_append = 0;
int all_norefresh = 0;


char *noargs[1];

void
InitKeytab()
{
  register unsigned int i;

  for (i = 0; i < sizeof(ktab)/sizeof(*ktab); i++)
    {
      ktab[i].nr = RC_ILLEGAL;
      ktab[i].args = noargs;
    }

  ktab['h'].nr = ktab[Ctrl('h')].nr = RC_HARDCOPY;
#ifdef BSDJOBS
  ktab['z'].nr = ktab[Ctrl('z')].nr = RC_SUSPEND;
#endif
  ktab['c'].nr = ktab[Ctrl('c')].nr = RC_SCREEN;
  ktab[' '].nr = ktab[Ctrl(' ')].nr =
    ktab['n'].nr = ktab[Ctrl('n')].nr = RC_NEXT;
  ktab['-'].nr = ktab['p'].nr = ktab[Ctrl('p')].nr = RC_PREV;
  ktab['k'].nr = ktab[Ctrl('k')].nr = RC_KILL;
  ktab['l'].nr = ktab[Ctrl('l')].nr = RC_REDISPLAY;
  ktab['w'].nr = ktab[Ctrl('w')].nr = RC_WINDOWS;
  ktab['v'].nr = ktab[Ctrl('v')].nr = RC_VERSION;
  ktab['q'].nr = ktab[Ctrl('q')].nr = RC_XON;
  ktab['s'].nr = ktab[Ctrl('s')].nr = RC_XOFF;
  ktab['t'].nr = ktab[Ctrl('t')].nr = RC_TIME;
  ktab['i'].nr = ktab[Ctrl('i')].nr = RC_INFO;
  ktab['m'].nr = ktab[Ctrl('m')].nr = RC_LASTMSG;
  ktab['A'].nr = RC_AKA;
#ifdef UTMPOK
  ktab['L'].nr = RC_LOGIN;
#endif
  ktab[','].nr = RC_LICENSE;
  ktab['W'].nr = RC_WIDTH;
  ktab['.'].nr = RC_TERMCAP;
  ktab[Ctrl('\\')].nr = RC_QUIT;
  ktab['d'].nr = ktab[Ctrl('d')].nr = RC_DETACH;
  ktab['r'].nr = ktab[Ctrl('r')].nr = RC_WRAP;
  ktab['f'].nr = ktab[Ctrl('f')].nr = RC_FLOW;
  ktab['C'].nr = RC_CLEAR;
  ktab['Z'].nr = RC_RESET;
  ktab['H'].nr = RC_LOG;
  ktab[(unsigned int)Esc].nr = RC_OTHER;
  ktab[(unsigned int)MetaEsc].nr = RC_META;
  ktab['M'].nr = RC_MONITOR;
  ktab['?'].nr = RC_HELP;
  for (i = 0; i <= 9; i++)
    {
      char *args[2], arg1[10];
      args[0] = arg1;
      args[1] = 0;
      sprintf(arg1, "%d", i);
      ktab['0' + i].nr = RC_SELECT;
      ktab['0' + i].args = SaveArgs(args);
    }
  ktab[Ctrl('G')].nr = RC_VBELL;
  ktab[':'].nr = RC_COLON;
#ifdef COPY_PASTE
  ktab['['].nr = ktab[Ctrl('[')].nr = RC_COPY;
  ktab[']'].nr = ktab[Ctrl(']')].nr = RC_PASTE;
  ktab['{'].nr = RC_HISTORY;
  ktab['}'].nr = RC_HISTNEXT;
  ktab['>'].nr = RC_WRITEBUF;
  ktab['<'].nr = RC_READBUF;
  ktab['='].nr = RC_REMOVEBUF;
#endif
#ifdef POW_DETACH
  ktab['D'].nr = RC_POW_DETACH;
#endif
#ifdef LOCK
  ktab['x'].nr = ktab[Ctrl('x')].nr = RC_LOCKSCREEN;
#endif
  ktab['b'].nr = ktab[Ctrl('b')].nr = RC_BREAK;
  ktab['B'].nr = RC_POW_BREAK;
}

static void
FreeKey(key)
int key;
{
  char **p;

  struct action *act = &ktab[key];
  if (act->nr == RC_ILLEGAL)
    return;
  act->nr = RC_ILLEGAL;
  if (act->args == noargs)
    return;
  for (p = act->args; *p; p++)
    free(*p);
  free(act->args);
  act->args = noargs;
}

void
ProcessInput(ibuf, ilen)
char *ibuf;
int ilen;
{
  char *s;
  int slen;

  while (display)
    {
      fore = d_fore;
      slen = ilen;
      s = ibuf;
      while (ilen > 0)
	{
	  if (*s++ == Esc)
	    break;
	  ilen--;
	}
      slen -= ilen;
      while (slen)
	Process(&ibuf, &slen);
      if (--ilen == 0)
	d_ESCseen = 1;
      if (ilen <= 0)
	return;
      DoAction(&ktab[(int)(unsigned char)*s], (int)(unsigned char)*s);
      ibuf = s + 1;
      ilen--;
    }
}

int
FindCommnr(str)
char *str;
{
  int x, m, l = 0, r = RC_LAST;
  while (l <= r)
    {
      m = (l + r) / 2;
      x = strcmp(str, comms[m].name);
      if (x > 0)
	l = m + 1;
      else if (x < 0)
	r = m - 1;
      else
	return m;
    }
  return RC_ILLEGAL;
}

static int
CheckArgNum(nr, args)
int nr;
char **args;
{
  int i, n;
  static char *argss[] = {"no", "one", "two", "three"};

  n = comms[nr].flags & ARGS_MASK;
  for (i = 0; args[i]; i++)
    ;
  if (comms[nr].flags & ARGS_ORMORE)
    {
      if (i < n)
	{
	  Msg(0, "%s: %s: at least %s argument%s required", rc_name, comms[nr].name, argss[n], n != 1 ? "s" : "");
	  return -1;
	}
    }
  else if (comms[nr].flags & ARGS_PLUSONE)
    {
      if (i != n && i != n + 1)
	{
	  Msg(0, "%s: %s: %s or %s argument%s required", rc_name, comms[nr].name, argss[n], argss[n + 1], n != 0 ? "s" : "");
          return -1;
	}
    }
  else if (i != n)
    {
      Msg(0, "%s: %s: %s argument%s required", rc_name, comms[nr].name, argss[n], n != 1 ? "s" : "");
      return -1;
    }
  return 0;
}

void
DoAction(act, key)
struct action *act;
int key;
{
  int nr = act->nr;
  char **args = act->args;
  int n, msgok;
  char *p;
  char ch;

  n = comms[nr].flags;
  if ((n & NEED_DISPLAY) && display == 0)
    {
      Msg(0, "%s: %s: display required", rc_name, comms[nr].name);
      return;
    }
  if ((n & NEED_FORE) && fore == 0)
    {
      Msg(0, "%s: %s: window required", rc_name, comms[nr].name);
      return;
    }
  if (CheckArgNum(nr, args))
    return;
  msgok = display && !*rc_name;
  switch(nr)
    {
    case RC_SELECT:
      if (ParseNum(act, &n) == 0)
        SwitchWindow(n);
      break;
#ifdef AUTO_NUKE
    case RC_AUTONUKE:
      if (ParseOnOff(act, &d_auto_nuke) == 0 && msgok)
	Msg(0, "Autonuke turned %s", d_auto_nuke ? "on" : "off");
      break;
#endif
    case RC_OBUFLIMIT:
      if (*args == 0)
	Msg(0, "Limit is %d", d_obufmax);
      else if (ParseNum(act, &d_obufmax) == 0 && msgok)
	Msg(0, "Limit set to %d", d_obufmax);
      break;
    case RC_DUMPTERMCAP:
      WriteFile(DUMP_TERMCAP);
      break;
    case RC_HARDCOPY:
      WriteFile(DUMP_HARDCOPY);
      break;
    case RC_LOG:
      n = fore->w_logfp ? 1 : 0;
      ParseSwitch(act, &n);
      LogToggle(n);
      break;
#ifdef BSDJOBS
    case RC_SUSPEND:
      Detach(D_STOP);
      break;
#endif
    case RC_NEXT:
      if (MoreWindows())
	SwitchWindow(NextWindow());
      break;
    case RC_PREV:
      if (MoreWindows())
	SwitchWindow(PreviousWindow());
      break;
    case RC_KILL:
      n = fore->w_number;
      KillWindow(fore);
#ifdef NETHACK
      if (nethackflag)
	Msg(0, "You destroy poor window %d.", n);
#endif
      break;
    case RC_QUIT:
      Finit(0);
      /* NOTREACHED */
    case RC_DETACH:
      Detach(D_DETACH);
      break;
#ifdef POW_DETACH
    case RC_POW_DETACH:
      if (key >= 0)
	{
	  static char buf[2];

	  buf[0] = key;
	  Input(buf, 1, pow_detach_fn, INP_RAW);
	}
      else
        Detach(D_POWER); /* detach and kill Attacher's parent */
      break;
#endif
#ifdef COPY_PASTE
    case RC_COPY_REG:
      if ((p = *args) == NULL)
	{
	  Input("Copy to register:", 1, copy_reg_fn, INP_RAW);
	  break;
	}
      if ((p = ParseChar(p, &ch)) == NULL || *p)
	{
	  Msg(0, "%s: copy_reg: character, ^x, or (octal) \\032 expected.",
	      rc_name);
	  break;
	}
      copy_reg_fn(&ch, 0);
      break;
    case RC_INS_REG:
      if ((p = *args) == NULL)
	{
	  Input("Insert from register:", 1, ins_reg_fn, INP_RAW);
	  break;
	}
      if ((p = ParseChar(p, &ch)) == NULL || *p)
	{
	  Msg(0, "%s: ins_reg: character, ^x, or (octal) \\032 expected.",
	      rc_name);
	  break;
	}
      ins_reg_fn(&ch, 0);
      break;
#endif
    case RC_REGISTER:
      if ((p = ParseChar(*args, &ch)) == NULL || *p)
	Msg(0, "%s: register: character, ^x, or (octal) \\032 expected.",
	    rc_name);
      else
	{
	  struct plop *plp = plop_tab + (int)(unsigned char)ch;

	  if (plp->buf)
	    free(plp->buf);
	  plp->buf = SaveStr(args[1]);
	  plp->len = strlen(args[1]);
	}
      break;
    case RC_PROCESS:
      if ((p = *args) == NULL)
	{
	  Input("Process register:", 1, process_fn, INP_RAW);
	  break;
	}
      if ((p = ParseChar(p, &ch)) == NULL || *p)
	{
	  Msg(0, "%s: process: character, ^x, or (octal) \\032 expected.",
	      rc_name);
	  break;
	}
      process_fn(&ch, 0);
      break;
    case RC_REDISPLAY:
      Activate(-1);
      break;
    case RC_WINDOWS:
      ShowWindows();
      break;
    case RC_VERSION:
      Msg(0, "screen %s", version);
      break;
    case RC_TIME:
      ShowTime();
      break;
    case RC_INFO:
      ShowInfo();
      break;
    case RC_OTHER:
      if (MoreWindows())
	SwitchWindow(d_other ? d_other->w_number : NextWindow());
      break;
    case RC_META:
      ch = Esc;
      p = &ch;
      n = 1;
      Process(&p, &n);
      break;
    case RC_XON:
      ch = Ctrl('q');
      p = &ch;
      n = 1;
      Process(&p, &n);
      break;
    case RC_XOFF:
      ch = Ctrl('s');
      p = &ch;
      n = 1;
      Process(&p, &n);
      break;
    case RC_POW_BREAK:
    case RC_BREAK:
      n = 0;
      if (*args && ParseNum(act, &n))
	break;
      SendBreak(fore, n, nr == RC_POW_BREAK);
      break;
#ifdef LOCK
    case RC_LOCKSCREEN:
      Detach(D_LOCK);
      break;
#endif
    case RC_WIDTH:
      if (*args)
	{
	  if (ParseNum(act, &n))
	    break;
	}
      else
	{
	  if (d_width == Z0width)
	    n = Z1width;
	  else if (d_width == Z1width)
	    n = Z0width;
	  else if (d_width > (Z0width + Z1width) / 2)
	    n = Z0width;
	  else
	    n = Z1width;
	}
      if (n <= 0)
        {
	  Msg(0, "Illegal width");
	  break;
	}
      if (n == d_width)
	break;
      if (ResizeDisplay(n, d_height) == 0)
	{
	  DoResize(d_width, d_height);
	  Activate(d_fore ? d_fore->w_norefresh : 0);
	}
      else
	Msg(0, "Your termcap does not specify how to change the terminal's width to %d.", n);
      break;
    case RC_HEIGHT:
      if (*args)
	{
	  if (ParseNum(act, &n))
	    break;
	}
      else
	{
#define H0height 42
#define H1height 24
	  if (d_height == H0height)
	    n = H1height;
	  else if (d_height == H1height)
	    n = H0height;
	  else if (d_height > (H0height + H1height) / 2)
	    n = H0height;
	  else
	    n = H1height;
	}
      if (n <= 0)
        {
	  Msg(0, "Illegal height");
	  break;
	}
      if (n == d_height)
	break;
      if (ResizeDisplay(d_width, n) == 0)
	{
	  DoResize(d_width, d_height);
	  Activate(d_fore ? d_fore->w_norefresh : 0);
	}
      else
	Msg(0, "Your termcap does not specify how to change the terminal's height to %d.", n);
      break;
    case RC_AKA:
      if (*args == 0)
	InputAKA();
      else
	strncpy(fore->w_cmd + fore->w_akapos, *args, 20);
      break;
    case RC_COLON:
      InputColon();
      break;
    case RC_LASTMSG:
      if (d_status_lastmsg)
	Msg(0, "%s", d_status_lastmsg);
      break;
    case RC_SCREEN:
      DoScreen("key", args);
      break;
    case RC_WRAP:
      if (ParseSwitch(act, &fore->w_wrap) == 0 && msgok)
        Msg(0, "%cwrap", fore->w_wrap ? '+' : '-');
      break;
    case RC_FLOW:
      if (*args)
	{
	  if (args[0][0] == 'a')
	    {
	      fore->w_flow = (fore->w_flow & FLOW_AUTO) ? FLOW_AUTOFLAG |FLOW_AUTO|FLOW_NOW : FLOW_AUTOFLAG;
	    }
	  else
	    {
	      if (ParseOnOff(act, &n))
		break;
	      fore->w_flow = (fore->w_flow & FLOW_AUTO) | n;
	    }
	}
      else
	{
	  if (fore->w_flow & FLOW_AUTOFLAG)
	    fore->w_flow = (fore->w_flow & FLOW_AUTO) | FLOW_NOW;
	  else if (fore->w_flow & FLOW_NOW)
	    fore->w_flow &= ~FLOW_NOW;
	  else
	    fore->w_flow = fore->w_flow ? FLOW_AUTOFLAG|FLOW_AUTO|FLOW_NOW : FLOW_AUTOFLAG;
	}
      SetFlow(fore->w_flow & FLOW_NOW);
      if (msgok)
	Msg(0, "%cflow%s", (fore->w_flow & FLOW_NOW) ? '+' : '-',
	    (fore->w_flow & FLOW_AUTOFLAG) ? "(auto)" : "");
      break;
    case RC_CLEAR:
      if (fore->w_state == LIT)
	WriteString(fore, "\033[H\033[J", 6);
      break;
    case RC_RESET:
      if (fore->w_state == LIT)
	WriteString(fore, "\033c", 2);
      break;
    case RC_MONITOR:
      n = fore->w_monitor == MON_ON;
      if (ParseSwitch(act, &n))
	break;
      if (n)
	{
	  fore->w_monitor = MON_ON;
#ifdef NETHACK
	  if (nethackflag)
	    Msg(0, "You feel like someone is watching you...");
	  else
#endif
	    Msg(0, "Window %d is now being monitored for all activity.", fore->w_number);
	}
      else
	{
	  fore->w_monitor = MON_OFF;
#ifdef NETHACK
	  if (nethackflag)
	    Msg(0, "You no longer sense the watcher's presence.");
	  else
#endif
	    Msg(0, "Window %d is no longer being monitored for activity.", fore->w_number);
	}
      break;
    case RC_HELP:
      display_help();
      break;
    case RC_LICENSE:
      display_copyright();
      break;
#ifdef COPY_PASTE
    case RC_COPY:
      if (d_layfn != &WinLf)
	{
	  Msg(0, "Must be on a window layer");
	  break;
	}
      MarkRoutine();
      break;
    case RC_HISTNEXT:
      if (d_layfn != &WinLf)
	{
	  Msg(0, "Must be on a window layer");
	  break;
	}
      if (GetHistory())
	if (d_copybuffer != NULL)
	  {
	    d_pastelen = d_copylen;
	    d_pastebuffer = d_copybuffer;
	    debug("histnext\n");
	  }
      break;
    case RC_HISTORY:
      if (d_layfn != &WinLf)
	{
	  Msg(0, "Must be on a window layer");
	  break;
	}
      if (GetHistory())
	if (d_copybuffer != NULL)
	  {
	    d_pastelen = d_copylen;
	    d_pastebuffer = d_copybuffer;
	    debug1("history new d_copylen: %d\n", d_pastelen);
	  }
      break;
    case RC_PASTE:
      if (d_copybuffer == NULL)
	{
#ifdef NETHACK
	  if (nethackflag)
	    Msg(0, "Nothing happens.");
	  else
#endif
	  Msg(0, "empty buffer");
	  break;
	}
      d_pastelen = d_copylen;
      d_pastebuffer = d_copybuffer;
      break;
    case RC_WRITEBUF:
      if (d_copybuffer == NULL)
	{
#ifdef NETHACK
	  if (nethackflag)
	    Msg(0, "Nothing happens.");
	  else
#endif
	  Msg(0, "empty buffer");
	  break;
	}
      WriteFile(DUMP_EXCHANGE);
      break;
    case RC_READBUF:
      ReadFile();
      break;
    case RC_REMOVEBUF:
      KillBuffers();
      break;
#endif				/* COPY_PASTE */
    case RC_ESCAPE:
      FreeKey((int)(unsigned char)Esc);
      FreeKey((int)(unsigned char)MetaEsc);
      if (ParseEscape(*args))
	{
	  Msg(0, "%s: two characters required after escape.", rc_name);
	  break;
	}
      FreeKey((int)(unsigned char)Esc);
      FreeKey((int)(unsigned char)MetaEsc);
      ktab[(int)(unsigned char)Esc].nr = RC_OTHER;
      ktab[(int)(unsigned char)MetaEsc].nr = RC_META;
      break;
    case RC_CHDIR:
      p = *args ? *args : home;
      if (chdir(p) == -1)
	Msg(errno, "%s", p);
      break;
    case RC_SHELL:
      if (ParseSaveStr(act, &ShellProg) == 0)
        ShellArgs[0] = ShellProg;
      break;
    case RC_HARDCOPYDIR:
      (void)ParseSaveStr(act, &hardcopydir);
      break;
    case RC_LOGDIR:
      (void)ParseSaveStr(act, &screenlogdir);
      break;
    case RC_SHELLAKA:
      (void)ParseSaveStr(act, &nwin_default.aka);
      break;
    case RC_SLEEP:
    case RC_TERMCAP:
    case RC_TERMINFO:
      break;			/* Already handled */
    case RC_TERM:
      p = NULL;
      if (ParseSaveStr(act, &p))
	break;
      if (strlen(p) >= 20)
	{
	  Msg(0,"%s: term: argument too long ( < 20)", rc_name);
	  free(p);
	  break;
	}
      strcpy(screenterm, p);
      free(p);
      debug1("screenterm set to %s\n", screenterm);
      MakeTermcap(display == 0);
      debug("new termcap made\n");
      break;
    case RC_ECHO:
      if (msgok)
	{
	  /*
	   * d_user typed ^A:echo... well, echo isn't FinishRc's job,
	   * but as he wanted to test us, we show good will
	   */
	  if (*args && (args[1] == 0 || (strcmp(args[1], "-n") == 0 && args[2] == 0)))
	    Msg(0, "%s", args[1] ? args[1] : *args);
	  else
 	    Msg(0, "%s: 'echo [-n] \"string\"' expected.", rc_name);
	}
      break;
    case RC_BELL:
      (void)ParseSaveStr(act, &BellString);
      break;
#ifdef COPY_PASTE
    case RC_BUFFERFILE:
      if (*args == 0)
	BufferFile = SaveStr(DEFAULT_BUFFERFILE);
      else if (ParseSaveStr(act, &BufferFile))
        break;
      if (msgok)
        Msg(0, "Bufferfile is now '%s'\n", BufferFile);
      break;
#endif
    case RC_ACTIVITY:
      (void)ParseSaveStr(act, &ActivityString);
      break;
#ifdef POW_DETACH
    case RC_POW_DETACH_MSG:
      (void)ParseSaveStr(act, &PowDetachString);
      break;
#endif
#ifdef UTMPOK
    case RC_DEFLOGIN:
      (void)ParseOnOff(act, &nwin_default.lflag);
      break;
    case RC_LOGIN:
      n = fore->w_slot != (slot_t)-1;
      if (ParseSwitch(act, &n) == 0)
        SlotToggle(n);
      break;
#endif
    case RC_DEFFLOW:
      if (args[0] && args[1] && args[1][0] == 'i')
	{
	  iflag = 1;
	  if ((intrc == VDISABLE) && (origintrc != VDISABLE))
	    {
#if defined(TERMIO) || defined(POSIX)
	      intrc = d_NewMode.tio.c_cc[VINTR] = origintrc;
#else /* TERMIO || POSIX */
	      intrc = d_NewMode.m_tchars.t_intrc = origintrc;
#endif /* TERMIO || POSIX */

	      if (display)
		SetTTY(d_userfd, &d_NewMode);
	    }
	}
      if (args[0] && args[0][0] == 'a')
	nwin_default.flowflag = FLOW_AUTOFLAG;
      else
	(void)ParseOnOff(act, &nwin_default.flowflag);
      break;
    case RC_DEFWRAP:
      (void)ParseOnOff(act, &default_wrap);
      break;
    case RC_HARDSTATUS:
      RemoveStatus();
      (void)ParseOnOff(act, &use_hardstatus);
      break;
    case RC_DEFMONITOR:
      if (ParseOnOff(act, &n) == 0)
        default_monitor = (n == 0) ? MON_OFF : MON_ON;
      break;
    case RC_CONSOLE:
      n = (console_window != 0);
      if (ParseSwitch(act, &n))
        break;
      if (TtyGrabConsole(fore->w_ptyfd, n, rc_name))
	break;
      if (n == 0)
	  Msg(0, "%s: releasing console %s", rc_name, HostName);
      else if (console_window)
	  Msg(0, "%s: stealing console %s from window %d", rc_name, HostName, console_window->w_number);
      else
	  Msg(0, "%s: grabbing console %s", rc_name, HostName);
      console_window = n ? fore : 0;
      break;
    case RC_ALLPARTIAL:
      if (ParseOnOff(act, &all_norefresh))
	break;
      if (all_norefresh)
	Msg(0, "No refresh on window change!\n");
      else
	{
	  if (fore)
	    Activate(-1);
	  Msg(0, "Window specific refresh\n");
	}
      break;
    case RC_PARTIAL:
      (void)ParseSwitch(act, &fore->w_norefresh);
      break;
    case RC_VBELL:
      if (ParseSwitch(act, &visual_bell) || !msgok)
        break;
      if (visual_bell == 0)
	{
#ifdef NETHACK
	  if (nethackflag)
	    Msg(0, "Suddenly you can't see your bell!");
	  else
#endif
	  Msg(0, "switched to audible bell.");
	}
      else
	{
#ifdef NETHACK
	  if (nethackflag)
	    Msg(0, "Your bell is no longer invisible.");
	  else
#endif
	  Msg(0, "switched to visual bell.");
	}
      break;
    case RC_VBELLWAIT:
      if (ParseNum(act, &VBellWait) == 0 && msgok)
        Msg(0, "vbellwait set to %d seconds", VBellWait);
      break;
    case RC_MSGWAIT:
      if (ParseNum(act, &MsgWait) == 0 && msgok)
        Msg(0, "msgwait set to %d seconds", MsgWait);
      break;
    case RC_MSGMINWAIT:
      if (ParseNum(act, &MsgMinWait) && msgok)
        Msg(0, "msgminwait set to %d seconds", MsgMinWait);
      break;
#ifdef COPY_PASTE
    case RC_DEFSCROLLBACK:
      (void)ParseNum(act, &nwin_default.histheight);
      break;
    case RC_SCROLLBACK:
      (void)ParseNum(act, &n);
      ChangeScrollback(fore, n, d_width);
      if (msgok)
	Msg(0, "scrollback set to %d", fore->w_histheight);
      break;
#endif
    case RC_SESSIONNAME:
      if (*args == 0)
	Msg(0, "This session is named '%s'\n", SockNamePtr);
      else
	{
	  char buf[MAXPATH];
	  char *s = NULL;

	  if (ParseSaveStr(act, &s))
	    break;
	  if (!*s)
	    {
	      Msg(0, "%s: bad session name '%s'\n", rc_name, s);
	      free(s);
	      break;
	    }
	  sprintf(buf, "%s", SockPath);
	  sprintf(buf + (SockNamePtr - SockPath), "%d.%s", getpid(), s); 
	  free(s);
	  if ((access(buf, F_OK) == 0) || (errno != ENOENT))
	    {
	      Msg(0, "%s: inappropriate path: '%s'.", rc_name, buf);
	      break;
	    }
	  if (rename(SockPath, buf))
	    {
	      Msg(errno, "%s: failed to rename(%s, %s)", rc_name, SockPath, buf);
	      break;
	    }
	  debug2("rename(%s, %s) done\n", SockPath, buf);
	  sprintf(SockPath, "%s", buf);
	  MakeNewEnv();
	}
      break;
    case RC_SETENV:
#ifndef USESETENV
	{
	  char *buf;
	  int l;

	  if ((buf = (char *)malloc((l = strlen(args[0])) + 
				     strlen(args[1]) + 2)) == NULL)
	    {
	      Msg(0, strnomem);
	      break;
	    }
	  strcpy(buf, args[0]);
	  buf[l] = '=';
	  strcpy(buf + l + 1, args[1]);
	  putenv(buf);
# ifdef NEEDPUTENV
	  /*
	   * we use our own putenv(), knowing that it does a malloc()
	   * the string space, we can free our buf now. 
	   */
	  free(buf);
# else /* NEEDSETENV */
	  /*
	   * For all sysv-ish systems that link a standard putenv() 
	   * the string-space buf is added to the environment and must not
	   * be freed, or modified.
	   * We are sorry to say that memory is lost here, when setting 
	   * the same variable again and again.
	   */
# endif /* NEEDSETENV */
	}
#else /* USESETENV */
# if defined(linux) || defined(__386BSD__) || defined(BSDI)
      setenv(args[0], args[1], 0);
# else
      setenv(args[0], args[1]);
# endif /* linux || __386BSD__ || BSDI */
#endif /* USESETENV */
      break;
    case RC_UNSETENV:
      unsetenv(*args);
      break;
    case RC_SLOWPASTE:
      if (ParseNum(act, &slowpaste) == 0 && msgok)
	Msg(0, "slowpaste set to %d milliseconds", slowpaste);
      break;
#ifdef COPY_PASTE
    case RC_MARKKEYS:
      p = NULL;
      if (ParseSaveStr(act, &p))
        break;
      if (CompileKeys(p, mark_key_tab))
	{
	  Msg(0, "%s: markkeys: syntax error.", rc_name);
	  free(p);
	  break;
	}
      debug1("markkeys %s\n", *args);
      free(p);
      break;
#endif
#ifdef NETHACK
    case RC_NETHACK:
      (void)ParseOnOff(act, &nethackflag);
      break;
#endif
    case RC_HARDCOPY_APPEND:
      (void)ParseOnOff(act, &hardcopy_append);
      break;
    case RC_VBELL_MSG:
      (void)ParseSaveStr(act, &VisualBellString);
      debug1(" new vbellstr '%s'\n", VisualBellString);
      break;
    case RC_DEFMODE:
      if (ParseOct(act, &n))
        break;
      if (n < 0 || n > 0777)
	{
	  Msg(0, "%s: mode: Invalid tty mode %o", rc_name, n);
          break;
	}
      TtyMode = n;
      if (msgok)
	Msg(0, "Ttymode set to %03o", TtyMode);
      break;
#ifdef COPY_PASTE
    case RC_CRLF:
      (void)ParseOnOff(act, &join_with_cr);
      break;
#endif
    case RC_AUTODETACH:
      (void)ParseOnOff(act, &auto_detach);
      break;
    case RC_STARTUP_MESSAGE:
      (void)ParseOnOff(act, &default_startup);
      break;
#ifdef PASSWORD
    case RC_PASSWORD:
      CheckPassword = 1;
      if (*args)
	{
	  strncpy(Password, *args, sizeof(Password) - 1);
	  if (!strcmp(Password, "none"))
	    CheckPassword = 0;
	}
      else
	{
	  if (display == 0)
	    {
	      debug("prompting for password on no display???\n");
	      break;
	    }
	  Input("New screen password:", sizeof(Password) - 1, pass1, 
		INP_NOECHO);
	}
      break;
#endif				/* PASSWORD */
    case RC_BIND:
      if ((p = ParseChar(*args, &ch)) == NULL || *p)
	{
	  Msg(0, "%s: bind: character, ^x, or (octal) \\032 expected.",
	      rc_name);
	  break;
	}
      n = (unsigned char)ch;
      FreeKey(n);
      if (args[1])
	{
          int i;

	  if ((i = FindCommnr(args[1])) == RC_ILLEGAL)
	    {
	      Msg(0, "%s: bind: unknown command '%s'", rc_name, args[1]);
	      break;
	    }
	  if (CheckArgNum(i, args + 2))
	    break;
	  ktab[n].nr = i;
	  if (args[2])
	    ktab[n].args = SaveArgs(args + 2);
	}
      break;
#ifdef MULTIUSER
    case RC_ACLADD:
	{
	  struct acl *acl;

	  p = NULL;
	  if (ParseSaveStr(act, &p))
	    break;
	  if (findacl(p))
	    {
	      Msg(0, "%s already in acl database", p);
	      free(p);
	      return;
	    }
	  if ((acl = (struct acl *)malloc(sizeof *acl)) == 0)
	    {
	      Msg(0, strnomem);
	      free(p);
	      break;
	    }
	  acl->name = p;
	  acl->next = acls;
	  acls = acl;
	  if (msgok)
	    Msg(0, "%s added to acl database", p);
	  break;
	}
    case RC_ACLDEL:
        {
	  struct acl **aclp, *acl;

	  p = NULL;
	  if (ParseSaveStr(act, &p))
	    break;
	  if ((aclp = findacl(p)) == 0)
	    {
	      Msg(0, "%s not in acl database", p);
	      free(p);
	      return;
	    }
	  acl = *aclp;
	  *aclp = acl->next;
	  free(acl->name);
	  free(acl);
	  if (msgok)
	    Msg(0, "%s removed from acl database", p);
	  free(p);
	  break;
        }
    case RC_MULTIUSER:
      if (ParseOnOff(act, &n))
	break;
      multi = n ? "" : 0;
      chsock();
      if (msgok)
	Msg(0, "Multiuser mode %s", multi ? "enabled" : "disabled");
      break;
#endif
    case RC_EXEC:
      winexec(args);
      break;
    default:
      break;
    }
}

static char **
SaveArgs(args)
char **args;
{
  register char **ap, **pp;
  register int argc = 0;

  debug("SaveArgs: ");
  while (args[argc])
    argc++;
  if ((pp = ap = (char **) malloc((unsigned) (argc + 1) * sizeof(char **))) == 0)
    Panic(0, strnomem);
  while (argc--)
    {
      debug1(" '%s'", *args);
      *pp++ = SaveStr(*args++);
    }
  debug("\n");
  *pp = 0;
  return ap;
}

int 
Parse(buf, args)
char *buf, **args;
{
  static char bang_special[] = "exec";
  register char *p = buf, **ap = args;
  register int delim, argc;

  argc = 0;
  for (;;)
    {
      while (*p && (*p == ' ' || *p == '\t'))
	++p;
      if (argc == 0 && *p == '!')
	{
	  *ap++ = bang_special;
	  p++;
	  while (*p == ' ')
	    p++;
	  argc++;
	}
      if (*p == '\0' || *p == '#')
	{
	  *p = '\0';
	  args[argc] = 0;
	  return argc;
	}
      if (++argc >= MAXARGS)
	{
	  Msg(0, "%s: too many tokens.", rc_name);
	  return 0;
	}
      delim = 0;
      if (*p == '"' || *p == '\'')
	delim = *p++;
      *ap++ = p;
      while (*p && !(delim ? *p == delim : (*p == ' ' || *p == '\t')))
	++p;
      if (*p == '\0')
	{
	  if (delim)
	    {
	      Msg(0, "%s: Missing quote.", rc_name);
	      return 0;
	    }
	}
      else
        *p++ = '\0';
    }
}

int 
ParseEscape(p)
char *p;
{
  if ((p = ParseChar(p, &Esc)) == NULL ||
      (p = ParseChar(p, &MetaEsc)) == NULL || *p)
    return -1;
  return 0;
}

static int
ParseSwitch(act, var)
struct action *act;
int *var;
{
  if (*act->args == 0)
    {
      *var ^= 1;
      return 0;
    }
  return ParseOnOff(act, var);
}

static int
ParseOnOff(act, var)
struct action *act;
int *var;
{
  register int num = -1;
  char **args = act->args;

  if (args[1] == 0)
    {
      if (strcmp(args[0], "on") == 0)
	num = 1;
      else if (strcmp(args[0], "off") == 0)
	num = 0;
    }
  if (num < 0)
    {
      Msg(0, "%s: %s: invalid argument. Give 'on' or 'off'", rc_name, comms[act->nr].name);
      return -1;
    }
  *var = num;
  return 0;
}

static int
ParseSaveStr(act, var)
struct action *act;
char **var;
{
  char **args = act->args;
  if (*args == 0 || args[1])
    {
      Msg(0, "%s: %s: one argument required.", rc_name, comms[act->nr].name);
      return -1;
    }
  if (*var)
    free(*var);
  *var = SaveStr(*args);
  return 0;
}

static int
ParseNum(act, var)
struct action *act;
int *var;
{
  int i;
  char *p, **args = act->args;

  p = *args;
  if (p == 0 || *p == 0 || args[1])
    {
      Msg(0, "%s: %s: invalid argument. Give one argument.",
          rc_name, comms[act->nr].name);
      return -1;
    }
  i = 0; 
  while (*p)
    {
      if (*p >= '0' && *p <= '9')
	i = 10 * i + (*p - '0');
      else
	{
	  Msg(0, "%s: %s: invalid argument. Give numeric argument.",
	      rc_name, comms[act->nr].name);
	  return -1;
	}    
      p++;
    }
  debug1("ParseNum got %d\n", i);
  *var = i;
  return 0;
}

static int
ParseOct(act, var)
struct action *act;
int *var;
{
  char *p, **args = act->args;
  int i = 0; 

  p = *args;
  if (p == 0 || *p == 0 || args[1])
    {
      Msg(0, "%s: %s: invalid argument. Give one octal argument.",
          rc_name, comms[act->nr].name);
      return -1;
    }
  while (*p)
    {
      if (*p >= '0' && *p <= '7')
	i = 8 * i + (*p - '0');
      else
	{
	  Msg(0, "%s: %s: invalid argument. Give octal argument.",
	      rc_name, comms[act->nr].name);
	  return -1;
	}    
      p++;
    }
  debug1("ParseOct got %d\n", i);
  *var = i;
  return 0;
}

static char *
ParseChar(p, cp)
char *p, *cp;
{
  if (*p == 0)
    return 0;
  if (*p == '^')
    {
      if (*++p == '?')
        *cp = '\177';
      else if (*p >= '@')
        *cp = Ctrl(*p);
      else
        return 0;
      ++p;
    }
  else if (*p == '\\' && *++p <= '7' && *p >= '0')
    {
      *cp = 0;
      do
        *cp = *cp * 8 + *p - '0';
      while (*++p <= '7' && *p >= '0');
    }
  else
    *cp = *p++;
  return p;
}


static
int IsNum(s, base)
register char *s;
register int base;
{
  for (base += '0'; *s; ++s)
    if (*s < '0' || *s > base)
      return 0;
  return 1;
}

static int
IsNumColon(s, base, p, psize)
int base, psize;
char *s, *p;
{
  char *q;
  if ((q = rindex(s, ':')) != NULL)
    {
      strncpy(p, q + 1, psize - 1);
      p[psize - 1] = '\0';
      *q = '\0';
    }
  else
    *p = '\0';
  return IsNum(s, base);
}

static void
SwitchWindow(n)
int n;
{
  struct win *p;

  debug1("SwitchWindow %d\n", n);
  if (display == 0)
    return;
  if ((p = wtab[n]) == 0)
    {
      ShowWindows();
      return;
    }
  if (p == d_fore)
    {
      Msg(0, "This IS window %d.", n);
      return;
    }
  if (p->w_display)
    {
      Msg(0, "Window %d is on another display.", n);
      return;
    }
  SetForeWindow(p);
  Activate(fore->w_norefresh);
}

void
SetForeWindow(wi)
struct win *wi;
{
  struct win *p, **pp;
  struct layer *l;
  /*
   * If we come from another window, make it inactive.
   */
  if (display)
    {
      fore = d_fore;
      if (fore)
	{
	  d_other = fore;
	  fore->w_active = 0;
	  fore->w_display = 0;
	}
      else
	{
	  for (l = d_lay; l; l = l->l_next)
	    if (l->l_next == &BlankLayer)
	      {
		l->l_next = wi->w_lay;
		wi->w_lay = d_lay;
		for (l = d_lay; l != wi->w_lay; l = l->l_next)
		  l->l_block |= wi->w_lay->l_block;
		break;
	      }
	}
      d_fore = wi;
      if (d_other == wi)
	d_other = 0;
      d_lay = wi->w_lay;
      d_layfn = d_lay->l_layfn;
    }
  fore = wi;
  fore->w_display = display;
  if (!fore->w_lay)
    fore->w_active = 1;
  /*
   * Place the window at the head of the most-recently-used list.
   */
  for (pp = &windows; (p = *pp); pp = &p->w_next)
    if (p == wi)
      break;
  ASSERT(p);
  *pp = p->w_next;
  p->w_next = windows;
  windows = p;
}

static int
NextWindow()
{
  register struct win **pp;
  int n = fore ? fore->w_number : 0;

  for (pp = wtab + n + 1; pp != wtab + n; pp++)
    {
      if (pp == wtab + MAXWIN)
	pp = wtab;
      if (*pp)
	break;
    }
  return pp - wtab;
}

static int
PreviousWindow()
{
  register struct win **pp;
  int n = fore ? fore->w_number : MAXWIN - 1;

  for (pp = wtab + n - 1; pp != wtab + n; pp--)
    {
      if (pp < wtab)
	pp = wtab + MAXWIN - 1;
      if (*pp)
	break;
    }
  return pp - wtab;
}

static int
MoreWindows()
{
  if (windows && windows->w_next)
    return 1;
  if (fore == 0)
    {
      Msg(0, "No window available");
      return 0;
    }
#ifdef NETHACK
  if (nethackflag)
    Msg(0, "You cannot escape from window %d!", fore->w_number);
  else
#endif
  Msg(0, "No d_other window.");
  return 0;
}

void
FreeWindow(wp)
struct win *wp;
{
  struct display *d;

#ifdef UTMPOK
  RemoveUtmp(wp);
#endif
  (void) chmod(wp->w_tty, 0666);
  (void) chown(wp->w_tty, 0, 0);
  close(wp->w_ptyfd);
  if (wp == console_window)
    console_window = 0;
  if (wp->w_logfp != NULL)
    fclose(wp->w_logfp);
  ChangeWindowSize(wp, 0, 0);
  for (d = displays; d; d = d->_d_next)
    if (d->_d_other == wp)
      d->_d_other = 0;
  free(wp);
}

void
FreePseudowin(w)
struct win *w;
{
  struct pseudowin *pwin = w->w_pwin;

  ASSERT(pwin);
  if (!W_RW(w) && fcntl(w->w_ptyfd, F_SETFL, FNDELAY))
    Msg(errno, "Warning: FreePseudowin: NDELAY fcntl failed");
  (void) chmod(pwin->p_tty, 0666);
  (void) chown(pwin->p_tty, 0, 0);
  close(pwin->p_ptyfd);
  free(pwin);
  w->w_pwin = NULL;
}

int
OpenDevice(arg, lflag, fp, namep)
char *arg;
int *fp, lflag;
char **namep;
{
  struct stat st;
  int r, f;

  if ((stat(arg, &st)) == 0 && (st.st_mode & S_IFCHR))
    {
      if (access(arg, R_OK | W_OK) == -1)
	{
	  Msg(errno, "Cannot access line '%s' for R/W", arg); 
	  return -1;
	}
      else
	{
	  r = TTY_FLAG_PLAIN;
	  if ((f = OpenTTY(arg)) < 0)
	    return -1;
	  *namep = arg;
	}
    }
  else
    {
      r = 0;    /* for now we hope it is a program */
      f = OpenPTY(namep);
      if (f == -1)
	{
	  Msg(0, "No more PTYs.");
	  return -1;
	}
      (void) fcntl(f, F_SETFL, FNDELAY);
#ifdef TIOCPKT
      {
	int flag = 1;

	if (ioctl(f, TIOCPKT, &flag))
	  {
	    Msg(errno, "TIOCPKT ioctl");
	    close(f);
	    return -1;
	  }
      }
#endif /* TIOCPKT */
    }
#ifdef PTYGROUP
  (void) chown(*namep, real_uid, PTYGROUP);
#else
  (void) chown(*namep, real_uid, real_gid);
#endif
#ifdef UTMPOK
  (void) chmod(*namep, lflag ? TtyMode : (TtyMode & ~022));
#else
  (void) chmod(*namep, TtyMode);
#endif
  *fp = f;
  return r;
}

/*
 * Fields w_width, w_height, aflag, number (and w_tty)
 * are read from struct win *win. No fields written.
 * If pwin is nonzero, filedescriptors are distributed 
 * between win->w_tty and open(ttyname)
 *
 *  210    n    B           0    F    210   newfd  0   0   1
 * -----------------------------------------------------------
 *  BBB   c0   d0   c2c1   d1          BB    -1           d2
 *  BBF   c1   d1   c0c2        o0     BF     0           d2
 *  BFB   c0   d0   c2c1        o1     FB     1       d2
 *  BFF   c2   d2   c0c1        o0    B F     0       d1
 *  FBB   c0   d0   c2c1   d1   o2    FBB     2
 *  FBF   c1   d1   c0c2        o0     BF     0       d2
 *  FFB   c0   d0   c2c1        o1     FB     1           d2
 *  FFF   c2        c0c1        o0      F     0   d1      d2
 */
int 
ForkWindow(args, dir, term, ttyname, win)
char **args, *dir, *term, *ttyname;
struct win *win;
{
  struct pseudowin *pwin = win->w_pwin;
  int pid;
  char tebuf[25];
  char ebuf[10];
#ifndef TIOCSWINSZ
  char libuf[20], cobuf[20];
#endif
  int newfd = -1, n, pat;
  int w = win->w_width;
  int h = win->w_height;

  switch (pid = fork())
    {
    case -1:
      Msg(errno, "fork");
      return -1;
    case 0:
      signal(SIGHUP, SIG_DFL);
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);
      signal(SIGTERM, SIG_DFL);
#ifdef BSDJOBS
      signal(SIGTTIN, SIG_DFL);
      signal(SIGTTOU, SIG_DFL);
#endif
      if (setuid(real_uid) || setgid(real_gid))
	{
	  SendErrorMsg("Setuid/gid: %s", sys_errlist[errno]);
	  eexit(1);
	}
      if (dir && *dir && chdir(dir) == -1)
	{
	  SendErrorMsg("Cannot chdir to %s: %s", dir, sys_errlist[errno]);
	  eexit(1);
	}

      if (display)
	{
	  brktty(d_userfd);
	  freetty();
	}
      else
	brktty(-1);
#ifdef DEBUG
      if (dfp != stderr)
	fclose(dfp);
#endif
      closeallfiles(win->w_ptyfd);
#ifdef DEBUG
      if ((dfp = fopen("/tmp/debug/screen.child", "a")) == 0)
	dfp = stderr;
      else
	(void) chmod("/tmp/debug/screen.child", 0666);
#endif
      /* 
       * distribute filedescriptors between the ttys
       */
      pat = pwin ? pwin->fdpat : 
		   ((F_PFRONT<<(F_PSHIFT*2)) | (F_PFRONT<<F_PSHIFT) | F_PFRONT);
      n = ((pat & F_PFRONT) == F_PFRONT) + ((pat & ((F_PFRONT << F_PSHIFT) | 
           F_PFRONT)) == ((F_PFRONT << F_PSHIFT) | F_PFRONT));
      close(n);
      debug2("ForkWindow pat==%d, close(%d)\n", pat, n);
      if ((pat & ((F_PFRONT<<(F_PSHIFT*2)) | (F_PFRONT<<F_PSHIFT) | F_PFRONT))!=
                 ((F_PFRONT<<(F_PSHIFT*2)) | (F_PFRONT<<F_PSHIFT) | F_PFRONT))
	{
	  newfd = dup(win->w_ptyfd); /* BACK connection */
	  debug2("ForkWindow: %d = dup(%d)\n", newfd, win->w_ptyfd);
	}
      close(n == 0 ? 2 : 0);
      close(n == 1 ? 2 : 1);
      debug2("ForkWindow close %d and %d\n", n == 0 ? 2 : 0, n == 1 ? 2 : 1);
      close(win->w_ptyfd);
      debug2("ForkWindow close(win->w_ptyfd==%d \"%s\")\n", 
	     win->w_ptyfd, win->w_tty);
      if ((pat & ((F_PFRONT<<F_PSHIFT) | F_PFRONT)) == 0)
        {
          newfd = dup(0);
	  debug1("ForkWindow %d = dup(0)\n", newfd);
        }
      if (pat & ((F_PFRONT<<(F_PSHIFT*2)) | (F_PFRONT<<F_PSHIFT) | F_PFRONT))
        {
	  if ((newfd = open(ttyname, O_RDWR)) < 0)
	    {
	      SendErrorMsg("Cannot open %s: %s", 
	      		   ttyname, sys_errlist[errno]);
	      eexit(1);
	    }
	  debug2("ForkWindow %d = open(%s, O_RDWR)\n", newfd, ttyname);
	}
      else
        newfd = -1;
      debug1("ForkWindow newfd == %d\n", newfd);
      if (newfd != 2)
        {
          if ((pat & ((F_PFRONT<<(F_PSHIFT*2))|(F_PFRONT<<F_PSHIFT)|F_PFRONT))==
                     ((F_PFRONT<<(F_PSHIFT*2))|(F_PFRONT<<F_PSHIFT)|F_PFRONT))
	    {
	      n = dup(0);
	      debug1("ForkWindow all FRONT: extra %d = dup(0)\n", n);
	    }
	  n = dup(((pat & (F_PFRONT<<(F_PSHIFT*2))) == 0) == 
		  ((pat & (F_PFRONT<<F_PSHIFT)) == 0));
	  debug2("%d = dup(%d)\n", n, ((pat & (F_PFRONT<<(F_PSHIFT*2))) == 0) ==
				      ((pat & (F_PFRONT<<F_PSHIFT)) == 0));
	}
#ifdef SVR4
      if (newfd >= 0 && ioctl(newfd, I_PUSH, "ptem"))
	{
	  SendErrorMsg("Cannot I_PUSH ptem %s %s", ttyname, sys_errlist[errno]);
	  eexit(1);
	}
      if (newfd >= 0 && ioctl(newfd, I_PUSH, "ldterm"))
	{
	  SendErrorMsg("Cannot I_PUSH ldterm %s %s", ttyname, sys_errlist[errno]);
	  eexit(1);
	}
      if (newfd >= 0 && ioctl(newfd, I_PUSH, "ttcompat"))
	{
	  SendErrorMsg("Cannot I_PUSH ttcompat %s %s", ttyname, sys_errlist[errno]);
	  eexit(1);
	}
#endif
      if (newfd >= 0 && fgtty(newfd))
	SendErrorMsg("fgtty: %s (%d)", sys_errlist[errno], errno);
#ifdef TIOCSWINSZ
      glwz.ws_col = w;
      glwz.ws_row = h;
      (void) ioctl(newfd, TIOCSWINSZ, &glwz);
#else
      sprintf(libuf, "LINES=%d", h);
      sprintf(cobuf, "COLUMNS=%d", w);
      NewEnv[4] = libuf;
      NewEnv[5] = cobuf;
#endif
      if (newfd >= 0)
	{
	  if (display)
	    SetTTY(newfd, &d_OldMode);
	  else
	    {
	      struct mode Mode;
	      debug("No display - creating tty setting\n");
	      InitTTY(&Mode);
#ifdef DEBUG
	      DebugTTY(&Mode);
#endif
	      SetTTY(newfd, &Mode);
	    }
	}
      /* 
       * the pseudo window process should not be surprised with a 
       * nonblocking filedescriptor
       */
      if (!W_RW(win) && fcntl(0, F_SETFL, 0))
	SendErrorMsg("ForkWindow clear NDELAY fcntl failed, %d", errno);
      if (win->w_aflag)
	NewEnv[2] = MakeTermcap(1);
      else
	NewEnv[2] = Termcap;
      if (term && *term && strcmp(screenterm, term) &&
	  (strlen(term) < 20))
	{
	  char *s1, *s2, tl;

	  sprintf(tebuf, "TERM=%s", term);
	  debug2("Makewindow %d with %s\n", win->w_number, tebuf);
	  tl = strlen(term);
	  NewEnv[1] = tebuf;
	  if ((s1 = index(Termcap, '|')))
	    {
	      if ((s2 = index(++s1, '|')))
		{
		  if (strlen(Termcap) - (s2 - s1) + tl < 1024)
		    {
		      bcopy(s2, s1 + tl, strlen(s2) + 1);
		      bcopy(term, s1, tl);
		    }
		}
	    }
	}
      sprintf(ebuf, "WINDOW=%d", win->w_number);
      NewEnv[3] = ebuf;

      debug1("calling execvpe %s\n", *args);
      execvpe(*args, args, NewEnv);
      debug1("exec error: %d\n", errno);
      SendErrorMsg("Cannot exec %s: %s", *args, sys_errlist[errno]);
      exit(1);
    default:
      return pid;
    } /* end fork switch */
  /* NOTREACHED */
}

int
MakeWindow(newwin)
struct NewWindow *newwin;
{
  register struct win **pp, *p;
  register int n;
  int f = -1;
  struct NewWindow nwin;
  int ttyflag;
  char *TtyName;

  debug1("NewWindow: StartAt %d\n", newwin->StartAt);
  debug1("NewWindow: aka     %s\n", newwin->aka?newwin->aka:"NULL");
  debug1("NewWindow: dir     %s\n", newwin->dir?newwin->dir:"NULL");
  debug1("NewWindow: term    %s\n", newwin->term?newwin->term:"NULL");
  nwin_compose(&nwin_default, newwin, &nwin);
  pp = wtab + nwin.StartAt;

  do
    {
      if (*pp == 0)
	break;
      if (++pp == wtab + MAXWIN)
	pp = wtab;
    }
  while (pp != wtab + nwin.StartAt);
  if (*pp)
    {
      Msg(0, "No more windows.");
      return -1;
    }

#ifdef USRLIMIT
  /*
   * Count current number of users, if logging windows in.
   */
  if (nwin.lflag && CountUsers() >= USRLIMIT)
    {
      Msg(0, "User limit reached.  Window will not be logged in.");
      nwin.lflag = 0;
    }
#endif
  n = pp - wtab;
  debug1("Makewin creating %d\n", n);

  if ((ttyflag = OpenDevice(nwin.args[0], nwin.lflag, &f, &TtyName)) < 0)
    return ttyflag;

  if ((p = (struct win *) malloc(sizeof(struct win))) == 0)
    {
      close(f);
      Msg(0, strnomem);
      return -1;
    }
  bzero((char *) p, (int) sizeof(struct win));
  p->w_winlay.l_next = 0;
  p->w_winlay.l_layfn = &WinLf;
  p->w_winlay.l_data = (char *)p;
  p->w_lay = &p->w_winlay;
  p->w_display = display;
  p->w_number = n;
  p->w_ptyfd = f;
  p->w_aflag = nwin.aflag;
  p->w_flow = nwin.flowflag | ((nwin.flowflag & FLOW_AUTOFLAG) ? (FLOW_AUTO|FLOW_NOW) : FLOW_AUTO);
  if (!nwin.aka || ttyflag)
    nwin.aka = Filename(nwin.args[0]);
  strncpy(p->w_cmd, nwin.aka, MAXSTR - 1);
  if ((nwin.aka = rindex(p->w_cmd, '|')) != NULL)
    {
      *nwin.aka++ = '\0';
      nwin.aka += strlen(nwin.aka);
      p->w_akapos = nwin.aka - p->w_cmd;
      p->w_autoaka = 0;
    }
  else
    p->w_akapos = 0;
  p->w_monitor = default_monitor;
  p->w_norefresh = 0;
  strncpy(p->w_tty, TtyName, MAXSTR - 1);

  if (ChangeWindowSize(p, display ? d_defwidth : 80, display ? d_defheight : 24))
    {
      FreeWindow(p);
      return -1;
    }
#ifdef COPY_PASTE
  ChangeScrollback(p, nwin.histheight, p->w_width);
#endif
  ResetWindow(p);	/* sets p->w_wrap */

  if (ttyflag == TTY_FLAG_PLAIN)
    {
      p->w_t.flags |= TTY_FLAG_PLAIN;
      p->w_pid = 0;
    }
  else
    {
      debug("forking...\n");
      p->w_pwin = NULL;
      p->w_pid = ForkWindow(nwin.args, nwin.dir, nwin.term, TtyName, p);
      if (p->w_pid < 0)
	{
	  FreeWindow(p);
	  return -1;
	}
    }
  /*
   * Place the newly created window at the head of the most-recently-used list.
   */
  if (display && d_fore)
    d_other = d_fore;
  *pp = p;
  p->w_next = windows;
  windows = p;
#ifdef UTMPOK
  debug1("MakeWindow will %slog in.\n", nwin.lflag?"":"not ");
  if (nwin.lflag == 1)
    {
      if (display)
        SetUtmp(p);
      else
	p->w_slot = (slot_t) 0;
    }
  else
    p->w_slot = (slot_t) -1;
#endif
  SetForeWindow(p);
  Activate(p->w_norefresh);
  return n;
}

void
KillWindow(wi)
struct win *wi;
{
  struct win **pp, *p;

  display = wi->w_display;
  if (display)
    {
      if (wi == d_fore)
	{
	  RemoveStatus();
	  if (d_lay != &wi->w_winlay)
	    ExitOverlayPage();
	  d_fore = 0;
	  d_lay = &BlankLayer;
	  d_layfn = BlankLayer.l_layfn;
	}
    }

  for (pp = &windows; (p = *pp); pp = &p->w_next)
    if (p == wi)
      break;
  ASSERT(p);
  *pp = p->w_next;
  /*
   * Remove window from linked list.
   */
  wi->w_inlen = 0;
  wtab[wi->w_number] = 0;
  FreeWindow(wi);
  /*
   * If the foreground window disappeared check the head of the linked list
   * of windows for the most recently used window. If no window is alive at
   * all, exit.
   */
  if (display && d_fore)
    return;
  if (windows == 0)
    Finit(0);
  SwitchWindow(windows->w_number);
}

static void
LogToggle(on)
int on;
{
  char buf[1024];

  if ((fore->w_logfp != 0) == on)
    {
      if (display && !*rc_name)
	Msg(0, "You are %s logging.", on ? "already" : "not");
      return;
    }
  if (screenlogdir)
    sprintf(buf, "%s/screenlog.%d", screenlogdir, fore->w_number);
  else
    sprintf(buf, "screenlog.%d", fore->w_number);
  if (fore->w_logfp != NULL)
    {
#ifdef NETHACK
      if (nethackflag)
	Msg(0, "You put away your scroll of logging named \"%s\".", buf);
      else
#endif
      Msg(0, "Logfile \"%s\" closed.", buf);
      fclose(fore->w_logfp);
      fore->w_logfp = NULL;
      return;
    }
  if ((fore->w_logfp = secfopen(buf, "a")) == NULL)
    {
#ifdef NETHACK
      if (nethackflag)
	Msg(0, "You don't seem to have a scroll of logging named \"%s\".", buf);
      else
#endif
      Msg(errno, "Error opening logfile \"%s\"", buf);
      return;
    }
#ifdef NETHACK
  if (nethackflag)
    Msg(0, "You %s your scroll of logging named \"%s\".",
	ftell(fore->w_logfp) ? "add to" : "start writing on", buf);
  else
#endif
  Msg(0, "%s logfile \"%s\"", ftell(fore->w_logfp) ? "Appending to" : "Creating", buf);
}

static void
ShowWindows()
{
  char buf[1024];
  register char *s;
  register struct win **pp, *p;
  register char *cmd;

  ASSERT(display);
  s = buf;
  for (pp = wtab; pp < wtab + MAXWIN; pp++)
    {
      if ((p = *pp) == 0)
	continue;

      if (p->w_akapos)
	{
	  if (*(p->w_cmd + p->w_akapos) && *(p->w_cmd + p->w_akapos - 1) != ':')
	    cmd = p->w_cmd + p->w_akapos;
	  else
	    cmd = p->w_cmd + strlen(p->w_cmd) + 1;
	}
      else
	cmd = p->w_cmd;
      if (s - buf + strlen(cmd) > sizeof(buf) - 6)
	break;
      if (s > buf)
	{
	  *s++ = ' ';
	  *s++ = ' ';
	}
      *s++ = p->w_number + '0';
      if (p == fore)
	*s++ = '*';
      else if (p == d_other)
	*s++ = '-';
      if (p->w_monitor == MON_DONE || p->w_monitor == MON_MSG)
	*s++ = '@';
      if (p->w_bell == BELL_DONE || p->w_bell == BELL_MSG)
	*s++ = '!';
#ifdef UTMPOK
      if (p->w_slot != (slot_t) 0 && p->w_slot != (slot_t) -1)
	*s++ = '$';
#endif
      if (p->w_logfp != NULL)
	{
	  strcpy(s, "(L)");
	  s += 3;
	}
      *s++ = ' ';
      strcpy(s, cmd);
      s += strlen(s);
      if (p == fore)
	{
	  /* 
	   * this is usually done by Activate(), but when looking
	   * on your current window, you may get annoyed, as there is still
	   * that temporal '!' and '@' displayed.
	   * So we remove that after displaying it once.
	   */
	  p->w_bell = BELL_OFF;
	  if (p->w_monitor != MON_OFF)
	    p->w_monitor = MON_ON;
	}
    }
  *s++ = ' ';
  *s = '\0';
  Msg(0, "%s", buf);
}


static void
ShowTime()
{
  char buf[512];
  struct tm *tp;
  time_t now;

  (void) time(&now);
  tp = localtime(&now);
  sprintf(buf, "%2d:%02d:%02d %s", tp->tm_hour, tp->tm_min, tp->tm_sec,
	  HostName);
#ifdef LOADAV
  AddLoadav(buf + strlen(buf));
#endif /* LOADAV */
  Msg(0, "%s", buf);
}

static void
ShowInfo()
{
  char buf[512], *p;
  register struct win *wp = fore;
  register int i;

  if (wp == 0)
    {
      Msg(0, "(%d,%d)/(%d,%d) no window", d_x + 1, d_y + 1, d_width, d_height);
      return;
    }
#ifdef COPY_PASTE
  sprintf(buf, "(%d,%d)/(%d,%d)+%d %c%sflow %cins %corg %cwrap %capp %clog %cmon %cr",
#else
  sprintf(buf, "(%d,%d)/(%d,%d) %c%sflow %cins %corg %cwrap %capp %clog %cmon %cr",
#endif
	  wp->w_x + 1, wp->w_y + 1, wp->w_width, wp->w_height,
#ifdef COPY_PASTE
	  wp->w_histheight,
#endif
	  (wp->w_flow & FLOW_NOW) ? '+' : '-',
	  (wp->w_flow & FLOW_AUTOFLAG) ? "" : ((wp->w_flow & FLOW_AUTO) ? "(+)" : "(-)"),
	  wp->w_insert ? '+' : '-', wp->w_origin ? '+' : '-',
	  wp->w_wrap ? '+' : '-', wp->w_keypad ? '+' : '-',
	  (wp->w_logfp != NULL) ? '+' : '-',
	  (wp->w_monitor != MON_OFF) ? '+' : '-',
	  wp->w_norefresh ? '-' : '+');
  if (CG0)
    {
      p = buf + strlen(buf);
      sprintf(p, " G%1d [", wp->w_Charset);
      for (i = 0; i < 4; i++)
	p[i + 5] = wp->w_charsets[i] ? wp->w_charsets[i] : 'B';
      p[9] = ']';
      p[10] = '\0';
    }
  Msg(0, "%s", buf);
}


static void
AKAfin(buf, len)
char *buf;
int len;
{
  ASSERT(display);
  if (len && fore)
    {
      strcpy(fore->w_cmd + fore->w_akapos, buf);
    }
}

static void
InputAKA()
{
  Input("Set window's a.k.a. to: ", 20, AKAfin, INP_COOKED);
}

static void
Colonfin(buf, len)
char *buf;
int len;
{
  if (len)
    RcLine(buf);
}

static void
InputColon()
{
  Input(":", 100, Colonfin, INP_COOKED);
}

void
DoScreen(fn, av)
char *fn, **av;
{
  struct NewWindow nwin;
  register int num;
  char buf[20];
  char termbuf[25];

  nwin = nwin_undef;
  termbuf[0] = '\0';
  while (av && *av && av[0][0] == '-')
    {
      switch (av[0][1])
	{
	case 'f':
	  switch (av[0][2])
	    {
	    case 'n':
	    case '0':
	      nwin.flowflag = FLOW_NOW * 0;
	      break;
	    case 'y':
	    case '1':
	    case '\0':
	      nwin.flowflag = FLOW_NOW * 1;
	      break;
	    case 'a':
	      nwin.flowflag = FLOW_AUTOFLAG;
	      break;
	    default:
	      break;
	    }
	  break;
	case 'k':
	case 't':
	  if (av[0][2])
	    nwin.aka = &av[0][2];
	  else if (*++av)
	    nwin.aka = *av;
	  else
	    --av;
	  break;
	case 'T':
	  if (av[0][2])
	    nwin.term = &av[0][2];
	  else if (*++av)
	    nwin.term = *av;
	  else
	    --av;
	  break;
	case 'h':
	  if (av[0][2])
	    nwin.histheight = atoi(av[0] + 2);
	  else if (*++av)
	    nwin.histheight = atoi(*av);
	  else 
	    --av;
	  break;
	case 'l':
	  switch (av[0][2])
	    {
	    case 'n':
	    case '0':
	      nwin.lflag = 0;
	      break;
	    case 'y':
	    case '1':
	    case '\0':
	      nwin.lflag = 1;
	      break;
	    default:
	      break;
	    }
	  break;
	case 'a':
	  nwin.aflag = 1;
	  break;
	default:
	  Msg(0, "%s: screen: invalid option -%c.", fn, av[0][1]);
	  break;
	}
      ++av;
    }
  num = 0;
  if (av && *av && IsNumColon(*av, 10, buf, sizeof(buf)))
    {
      if (*buf != '\0')
	nwin.aka = buf;
      num = atoi(*av);
      if (num < 0 || num > MAXWIN - 1)
	{
	  Msg(0, "%s: illegal screen number %d.", fn, num);
	  num = 0;
	}
      nwin.StartAt = num;
      ++av;
    }
  if (av && *av)
    nwin.args = av;
  MakeWindow(&nwin);
}

/*
 * CompileKeys must be called before Markroutine is first used.
 * to initialise the keys with defaults, call CompileKeys(NULL, mark_key_tab);
 *
 * s is an ascii string in a termcap-like syntax. It looks like
 *   "j=u:k=d:l=r:h=l: =.:" and so on...
 * this example rebinds the cursormovement to the keys u (up), d (down),
 * l (left), r (right). placing a mark will now be done with ".".
 */
int
CompileKeys(s, array)
char *s, *array;
{
  int i;
  unsigned char key, value;

  if (!s || !*s)
    {
      for (i = 0; i < 256; i++)
        array[i] = i;
      return 0;
    }
  debug1("CompileKeys: '%s'\n", s);
  while (*s)
    {
      s = ParseChar(s, (char *) &key);
      if (*s != '=')
	return -1;
      do 
	{
          s = ParseChar(++s, (char *) &value);
	  array[value] = key;
	}
      while (*s == '=');
      if (!*s) 
	break;
      if (*s++ != ':')
	return -1;
    }
  return 0;
}

#ifdef MULTIUSER
struct acl **
findacl(name)
char *name;
{
  register struct acl *acl, **aclp;
  for (aclp = &acls; (acl = *aclp); aclp = &acl->next)
    if (strcmp(acl->name, name) == 0)
      break;
  return acl ? aclp : (struct acl **)0;
}
#endif


/*
 *  Asynchronous input functions
 */

#ifdef POW_DETACH
static void
pow_detach_fn(buf, len)
char *buf;
int len;
{
  if (len)
    {
      *buf = 0;
      return;
    }
  if (ktab[(int)(unsigned char)*buf].nr != RC_POW_DETACH)
    {
      if (display)
        write(d_userfd, "\007", 1);
#ifdef NETHACK
      if (nethackflag)
	 Msg(0, "The blast of disintegration whizzes by you!");
#endif
    }
  else
    Detach(D_POWER);
}
#endif /* POW_DETACH */

#ifdef COPY_PASTE
static void
copy_reg_fn(buf, len)
char *buf;
int len;
{
  struct plop *pp = plop_tab + (int)(unsigned char)*buf;

  if (len)
    {
      *buf = 0;
      return;
    }
  if (pp->buf)
    free(pp->buf);
  if ((pp->buf = (char *)malloc(d_copylen)) == NULL)
    {
      Msg(0, strnomem);
      return;
    }
  bcopy(d_copybuffer, pp->buf, d_copylen);
  pp->len = d_copylen;
  Msg(0, "Copied %d characters into register %c", d_copylen, *buf);
}

static void
ins_reg_fn(buf, len)
char *buf;
int len;
{
  struct plop *pp = plop_tab + (int)(unsigned char)*buf;

  if (len)
    {
      *buf = 0;
      return;
    }
  if (pp->buf)
    {
      d_pastebuffer  = pp->buf;
      d_pastelen = pp->len;
      return;
    }
#ifdef NETHACK
  if (nethackflag)
    Msg(0, "Nothing happens.");
  else
#endif
  Msg(0, "Empty register.");
}
#endif /* COPY_PASTE */

static void
process_fn(buf, len)
char *buf;
int len;
{
  struct plop *pp = plop_tab + (int)(unsigned char)*buf;

  if (len)
    {
      *buf = 0;
      return;
    }
  if (pp->buf)
    {
      ProcessInput(pp->buf, pp->len);
      return;
    }
#ifdef NETHACK
  if (nethackflag)
    Msg(0, "Nothing happens.");
  else
#endif
  Msg(0, "Empty register.");
}


#ifdef PASSWORD

/* ARGSUSED */
static void
pass1(buf, len)
char *buf;
int len;
{
  strncpy(Password, buf, sizeof(Password) - 1);
  Input("Retype new password:", sizeof(Password) - 1, pass2, 1);
}

/* ARGSUSED */
static void
pass2(buf, len)
char *buf;
int len;
{
  int st;
  char salt[2];

  if (buf == 0 || strcmp(Password, buf))
    {
#ifdef NETHACK
      if (nethackflag)
	Msg(0, "[ Passwords don't match - your armor crumbles away ]");
      else
#endif /* NETHACK */
        Msg(0, "[ Passwords don't match - checking turned off ]");
      CheckPassword = 0;
    }
  if (Password[0] == '\0')
    {
      CheckPassword = 0;
      Msg(0, "[ No password - no secure ]");
    }
  for (st = 0; st < 2; st++)
    salt[st] = 'A' + (int)((time(0) >> 6 * st) % 26);
  strncpy(Password, crypt(Password, salt), sizeof(Password));
  if (CheckPassword)
    {
#ifdef COPY_PASTE
      if (d_copybuffer)

	free(d_copybuffer);
      d_copylen = strlen(Password);
      if ((d_copybuffer = (char *) malloc(d_copylen+1)) == NULL)
	{
	  Msg(0, strnomem);
	  return;
	}
      strcpy(d_copybuffer, Password);
      Msg(0, "[ Password moved into d_copybuffer ]");
#else				/* COPY_PASTE */
      Msg(0, "[ Crypted password is \"%s\" ]", Password);
#endif				/* COPY_PASTE */
    }
}
#endif /* PASSWORD */

