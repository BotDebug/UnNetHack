/*     SCCS Id: @(#)termcap.c  3.4     2000/07/10      */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include <limits.h>

#if defined (TTY_GRAPHICS) && !defined(NO_TERMS)

#include "wintty.h"

#include "tcap.h"

#ifdef MICROPORT_286_BUG
#define Tgetstr(key) (tgetstr(key, tbuf))
#else
#define Tgetstr(key) (tgetstr(key, &tbufptr))
#endif /* MICROPORT_286_BUG **/

static char * s_atr2str(int);
static char * e_atr2str(int);

void cmov(int, int);
void nocmov(int, int);
#if defined(TEXTCOLOR) && defined(TERMLIB)
# if !defined(UNIX) || !defined(TERMINFO)
#  ifndef TOS
static void analyze_seq(char *, int *, int *);
#  endif
# endif
static void init_hilite(void);
static void kill_hilite(void);
#endif

/* (see tcap.h) -- nh_CM, nh_ND, nh_CD, nh_HI,nh_HE, nh_US,nh_UE,
            ul_hack */
struct tc_lcl_data tc_lcl_data = { 0, 0, 0, 0, 0, 0, 0, FALSE };

static char *HO, *CL, *CE, *UP, *XD, *BC, *SO, *SE, *TI, *TE;
static char *VS, *VE;
static char *ME, *MR, *MB, *MH, *MD;
static char *ZH, *ZR;

#ifdef TERMLIB
# ifdef TEXTCOLOR
static char *MD;
# endif
static int SG;
static char PC = '\0';
static char tbuf[512];
#endif

#ifdef TEXTCOLOR
# ifdef TOS
const char *hilites[CLR_MAX];   /* terminal escapes for the various colors */
# else
char NEARDATA *hilites[CLR_MAX]; /* terminal escapes for the various colors */
# endif
#endif

static char *KS = (char *)0, *KE = (char *)0;   /* keypad sequences */
static char nullstr[] = "";

#if defined(ASCIIGRAPH) && !defined(NO_TERMS)
extern boolean HE_resets_AS;
#endif

#ifndef TERMLIB
static char tgotobuf[20];
# ifdef TOS
#define tgoto(fmt, x, y)    (Sprintf(tgotobuf, fmt, y+' ', x+' '), tgotobuf)
# else
#define tgoto(fmt, x, y)    (Sprintf(tgotobuf, fmt, y+1, x+1), tgotobuf)
# endif
#endif /* TERMLIB */

#ifndef MSDOS

static void init_ttycolor(void);

boolean colorflag = FALSE;          /* colors are initialized */
int ttycolors[CLR_MAX];

void
init_ttycolor(void)
{
    if (!colorflag) {
        ttycolors[CLR_RED]      = CLR_RED;
        ttycolors[CLR_GREEN]    = CLR_GREEN;
        ttycolors[CLR_BROWN]    = CLR_BROWN;
        ttycolors[CLR_BLUE]     = CLR_BLUE;
        ttycolors[CLR_MAGENTA]  = CLR_MAGENTA;
        ttycolors[CLR_CYAN]     = CLR_CYAN;
        ttycolors[CLR_GRAY]     = CLR_GRAY;
        if (iflags.wc2_newcolors) {
            ttycolors[CLR_BLACK]    = CLR_BLACK;
        } else {
            ttycolors[CLR_BLACK]     = CLR_BLUE;
            defsyms[S_corr].color    = CLR_GRAY;
            defsyms[S_dnstair].color = CLR_GRAY;
            defsyms[S_upstair].color = CLR_GRAY;
        }
        ttycolors[CLR_ORANGE]         = CLR_ORANGE;
        ttycolors[CLR_BRIGHT_GREEN]   = CLR_BRIGHT_GREEN;
        ttycolors[CLR_YELLOW]         = CLR_YELLOW;
        ttycolors[CLR_BRIGHT_BLUE]    = CLR_BRIGHT_BLUE;
        ttycolors[CLR_BRIGHT_MAGENTA] = CLR_BRIGHT_MAGENTA;
        ttycolors[CLR_BRIGHT_CYAN]    = CLR_BRIGHT_CYAN;
        ttycolors[CLR_WHITE]          = CLR_WHITE;
    }
}

static int convert_uchars(char *, uchar *, int);

#ifdef VIDEOSHADES
/*
 * OPTIONS=videocolors:1-2-3-4-5-6-7-8-9-10-11-12-13-14-15
 * Left to right assignments for:
 *  red green    brown  blue    magenta cyan    gray    black
 *  orange  br.green yellow br.blue br.mag  br.cyan white
 */
int assign_videocolors(char *colorvals)
{
    int i, icolor;
    uchar *tmpcolor;

    init_ttycolor();

    i = strlen(colorvals);
    tmpcolor = (uchar *)alloc(i);
    if (convert_uchars(colorvals, tmpcolor, i) < 0) return FALSE;

    icolor = CLR_RED;
    for( i = 0; tmpcolor[i] != 0; ++i) {
        if (icolor <= CLR_WHITE)
            ttycolors[icolor++] = tmpcolor[i];
    }

    colorflag = TRUE;
    free((genericptr_t)tmpcolor);
    return 1;
}
#endif

static int
convert_uchars(
    char *bufp,  /**< current pointer */
    uchar *list, /**< return list */
    int size)
{
    unsigned int num = 0;
    int count = 0;

    list[count] = 0;

    while (1) {
        switch(*bufp) {
        case ' ':  case '\0':
        case '\t': case '-':
        case '\n':
            if (num) {
                list[count++] = num;
                list[count] = 0;
                num = 0;
            }
            if ((count==size) || !*bufp) return count;
            bufp++;
            break;
        case '#':
            if (num) {
                list[count++] = num;
                list[count] = 0;
            }
            return count;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
        case '8': case '9':
            num = num*10 + (*bufp-'0');
            if (num > 15) return -1;
            bufp++;
            break;
        default: return -1;
        }
    }
    /*NOTREACHED*/
}
#endif /* !MSDOS */

void
tty_startup(int *wid, int *hgt)
{
    int i;
#ifdef TERMLIB
    const char *term;
    char *tptr;
    char *tbufptr, *pc;
#endif

#ifdef TEXTCOLOR
# ifndef MSDOS
    init_ttycolor();
# endif
#endif

#ifdef TERMLIB

# ifdef VMS
    term = verify_termcap();
    if (!term)
# endif
    term = getenv("TERM");

# if defined(TOS) && defined(__GNUC__)
    if (!term)
        term = "builtin";       /* library has a default */
# endif
    if (!term)
#endif
#ifndef ANSI_DEFAULT
    error("Can't get TERM.");
#else
# ifdef TOS
    {
        CO = 80; LI = 25;
        TI = VS = VE = TE = nullstr;
        HO = "\033H";
        CE = "\033K";       /* the VT52 termcap */
        UP = "\033A";
        nh_CM = "\033Y%c%c";    /* used with function tgoto() */
        nh_ND = "\033C";
        XD = "\033B";
        BC = "\033D";
        SO = "\033p";
        SE = "\033q";
        /* HI and HE will be updated in init_hilite if we're using color */
        nh_HI = "\033p";
        nh_HE = "\033q";
        *wid = CO;
        *hgt = LI;
        CL = "\033E";       /* last thing set */
        return;
    }
# else /* TOS */
    {
#  ifdef MICRO
        get_scr_size();
#   ifdef CLIPPING
        if(CO < COLNO || LI < ROWNO+3)
            setclipped();
#   endif
#  endif
        HO = "\033[H";
/*      nh_CD = "\033[J"; */
        CE = "\033[K";      /* the ANSI termcap */
#  ifndef TERMLIB
        nh_CM = "\033[%d;%dH";
#  else
        nh_CM = "\033[%i%d;%dH";
#  endif
        UP = "\033[A";
        nh_ND = "\033[C";
        XD = "\033[B";
#  ifdef MICRO  /* backspaces are non-destructive */
        BC = "\b";
#  else
        BC = "\033[D";
#  endif
        nh_HI = SO = "\033[1m";
        nh_US = "\033[4m";
        MR = "\033[7m";
        TI = nh_HE = ME = SE = nh_UE = "\033[0m";
        /* strictly, SE should be 2, and nh_UE should be 24,
           but we can't trust all ANSI emulators to be
           that complete.  -3. */
#  ifndef MICRO
        AS = "\016";
        AE = "\017";
#  endif
        TE = VS = VE = nullstr;
#  ifdef TEXTCOLOR
        for (i = 0; i < CLR_MAX / 2; i++)
            if (i != CLR_BLACK) {
                hilites[i|BRIGHT] = (char *) alloc(sizeof("\033[1;3%dm"));
                Sprintf(hilites[i|BRIGHT], "\033[1;3%dm", i);
                if (iflags.wc2_newcolors || (i != CLR_GRAY))
#   ifdef MICRO
                    if (i == CLR_BLUE) hilites[CLR_BLUE] = hilites[CLR_BLUE|BRIGHT];
                    else
#   endif
                {
                    hilites[i] = (char *) alloc(sizeof("\033[0;3%dm"));
                    Sprintf(hilites[i], "\033[0;3%dm", i);
                }
            }
#  endif
        *wid = CO;
        *hgt = LI;
        CL = "\033[2J";     /* last thing set */
        return;
    }
# endif /* TOS */
#endif /* ANSI_DEFAULT */

#ifdef TERMLIB
    tptr = (char *) alloc(1024);

    tbufptr = tbuf;
    if(!strncmp(term, "5620", 4))
        flags.null = FALSE; /* this should be a termcap flag */
    if(tgetent(tptr, term) < 1) {
        char buf[BUFSZ];
        (void) strncpy(buf, term,
                       (BUFSZ - 1) - (sizeof("Unknown terminal type: .  ")));
        buf[BUFSZ-1] = '\0';
        error("Unknown terminal type: %s.", term);
    }
    if ((pc = Tgetstr("pc")) != 0)
        PC = *pc;

    if(!(BC = Tgetstr("le")))   /* both termcap and terminfo use le */
# ifdef TERMINFO
        error("Terminal must backspace.");
# else
        if(!(BC = Tgetstr("bc"))) { /* termcap also uses bc/bs */
#  ifndef MINIMAL_TERM
            if(!tgetflag("bs"))
                error("Terminal must backspace.");
#  endif
            BC = tbufptr;
            tbufptr += 2;
            *BC = '\b';
        }
# endif

# ifdef MINIMAL_TERM
    HO = (char *)0;
# else
    HO = Tgetstr("ho");
# endif
    /*
     * LI and CO are set in ioctl.c via a TIOCGWINSZ if available.  If
     * the kernel has values for either we should use them rather than
     * the values from TERMCAP ...
     */
# ifndef MICRO
    if (!CO) CO = tgetnum("co");
    if (!LI) LI = tgetnum("li");
# else
#  if defined(TOS) && defined(__GNUC__)
    if (!strcmp(term, "builtin"))
        get_scr_size();
    else {
#  endif
    CO = tgetnum("co");
    LI = tgetnum("li");
    if (!LI || !CO)             /* if we don't override it */
        get_scr_size();
#  if defined(TOS) && defined(__GNUC__)
}
#  endif
# endif
# ifdef CLIPPING
    if(CO < COLNO || LI < ROWNO+3)
        setclipped();
# endif
    nh_ND = Tgetstr("nd");
    if(tgetflag("os"))
        error("UnNetHack can't have OS.");
    if(tgetflag("ul"))
        ul_hack = TRUE;
    CE = Tgetstr("ce");
    UP = Tgetstr("up");
    /* It seems that xd is no longer supported, and we should use
       a linefeed instead; unfortunately this requires resetting
       CRMOD, and many output routines will have to be modified
       slightly. Let's leave that till the next release. */
    XD = Tgetstr("xd");
/* not:     XD = Tgetstr("do"); */
    if(!(nh_CM = Tgetstr("cm"))) {
        if(!UP && !HO)
            error("UnNetHack needs CM or UP or HO.");
        tty_raw_print("Playing UnNetHack on terminals without CM is suspect.");
        tty_wait_synch();
    }
    SO = Tgetstr("so");
    SE = Tgetstr("se");
    nh_US = Tgetstr("us");
    nh_UE = Tgetstr("ue");
    ZH = Tgetstr("ZH"); /* italic start */
    ZR = Tgetstr("ZR"); /* italic end */
    SG = tgetnum("sg"); /* -1: not fnd; else # of spaces left by so */
    if(!SO || !SE || (SG > 0)) SO = SE = nh_US = nh_UE = nullstr;
    TI = Tgetstr("ti");
    TE = Tgetstr("te");
    VS = VE = nullstr;
# ifdef TERMINFO
    VS = Tgetstr("eA"); /* enable graphics */
# endif
    KS = Tgetstr("ks"); /* keypad start (special mode) */
    KE = Tgetstr("ke"); /* keypad end (ordinary mode [ie, digits]) */
    MR = Tgetstr("mr"); /* reverse */
    MB = Tgetstr("mb"); /* blink */
    MD = Tgetstr("md"); /* boldface */
    MH = Tgetstr("mh"); /* dim */
    ME = Tgetstr("me"); /* turn off all attributes */
    if (!ME) {
        ME = SE ? SE : nullstr; /* default to SE value */
    }

    /* Get rid of padding numbers for nh_HI and nh_HE.  Hope they
     * aren't really needed!!!  nh_HI and nh_HE are outputted to the
     * pager as a string - so how can you send it NULs???
     *  -jsb
     */
    nh_HI = (char *) alloc((unsigned)(strlen(SO)+1));
    nh_HE = (char *) alloc((unsigned)(strlen(ME)+1));
    i = 0;
    while (digit(SO[i])) i++;
    Strcpy(nh_HI, &SO[i]);
    i = 0;
    while (digit(ME[i])) i++;
    Strcpy(nh_HE, &ME[i]);
    AS = Tgetstr("as");
    AE = Tgetstr("ae");
    nh_CD = Tgetstr("cd");
# ifdef TEXTCOLOR
    MD = Tgetstr("md");
# endif
# ifdef TEXTCOLOR
#  if defined(TOS) && defined(__GNUC__)
    if (!strcmp(term, "builtin") || !strcmp(term, "tw52") ||
        !strcmp(term, "st52")) {
        init_hilite();
    }
#  else
    init_hilite();
#  endif
# endif
    *wid = CO;
    *hgt = LI;
    if (!(CL = Tgetstr("cl")))  /* last thing set */
        error("UnNetHack needs CL.");
    if ((int)(tbufptr - tbuf) > (int)(sizeof tbuf))
        error("TERMCAP entry too big...\n");
    free((genericptr_t)tptr);
#endif /* TERMLIB */
}

/* note: at present, this routine is not part of the formal window interface */
/* deallocate resources prior to final termination */
void
tty_shutdown(void)
{
#if defined(TEXTCOLOR) && defined(TERMLIB)
    kill_hilite();
#endif
    /* we don't attempt to clean up individual termcap variables [yet?] */
    return;
}

void
tty_number_pad(int state)
{
    switch (state) {
    case -1:        /* activate keypad mode (escape sequences) */
        if (KS && *KS) xputs(KS);
        break;
    case  1:        /* activate numeric mode for keypad (digits) */
        if (KE && *KE) xputs(KE);
        break;
    case  0:        /* don't need to do anything--leave terminal as-is */
    default:
        break;
    }
}

#ifdef TERMLIB
extern void (*decgraphics_mode_callback)(void);    /* defined in drawing.c */
static void tty_decgraphics_termcap_fixup(void);

/*
   We call this routine whenever DECgraphics mode is enabled, even if it
   has been previously set, in case the user manages to reset the fonts.
   The actual termcap fixup only needs to be done once, but we can't
   call xputs() from the option setting or graphics assigning routines,
   so this is a convenient hook.
 */
static void
tty_decgraphics_termcap_fixup(void)
{
    static char ctrlN[]   = "\016";
    static char ctrlO[]   = "\017";
    static char appMode[] = "\033=";
    static char numMode[] = "\033>";

    /* these values are missing from some termcaps */
    if (!AS) AS = ctrlN;    /* ^N (shift-out [graphics font]) */
    if (!AE) AE = ctrlO;    /* ^O (shift-in  [regular font])  */
    if (!KS) KS = appMode;  /* ESC= (application keypad mode) */
    if (!KE) KE = numMode;  /* ESC> (numeric keypad mode)     */
    /*
     * Select the line-drawing character set as the alternate font.
     * Do not select NA ASCII as the primary font since people may
     * reasonably be using the UK character set.
     */
    if (iflags.DECgraphics) xputs("\033)0");
#ifdef PC9800
    init_hilite();
#endif

#if defined(ASCIIGRAPH) && !defined(NO_TERMS)
    /* some termcaps suffer from the bizarre notion that resetting
       video attributes should also reset the chosen character set */
    {
        const char *nh_he = nh_HE, *ae = AE;
        int he_limit, ae_length;

        if (digit(*ae)) { /* skip over delay prefix, if any */
            do ++ae; while (digit(*ae));
            if (*ae == '.') { ++ae; if (digit(*ae)) ++ae; }
            if (*ae == '*') ++ae;
        }
        /* can't use nethack's case-insensitive strstri() here, and some old
           systems don't have strstr(), so use brute force substring search */
        ae_length = strlen(ae), he_limit = strlen(nh_he);
        while (he_limit >= ae_length) {
            if (strncmp(nh_he, ae, ae_length) == 0) {
                HE_resets_AS = TRUE;
                break;
            }
            ++nh_he, --he_limit;
        }
    }
#endif
}
#endif  /* TERMLIB */

#if defined(ASCIIGRAPH) && defined(PC9800)
extern void (*ibmgraphics_mode_callback());    /* defined in drawing.c */
#endif

#ifdef PC9800
extern void (*ascgraphics_mode_callback());    /* defined in drawing.c */
static void tty_ascgraphics_hilite_fixup();

static void
tty_ascgraphics_hilite_fixup(void)
{
    int c;

    for (c = 0; c < CLR_MAX / 2; c++)
        if (c != CLR_BLACK) {
            hilites[c|BRIGHT] = (char *) alloc(sizeof("\033[1;3%dm"));
            Sprintf(hilites[c|BRIGHT], "\033[1;3%dm", c);
            if (iflags.wc2_newcolors || (c != CLR_GRAY)) {
                hilites[c] = (char *) alloc(sizeof("\033[0;3%dm"));
                Sprintf(hilites[c], "\033[0;3%dm", c);
            }
        }
}
#endif /* PC9800 */

void
tty_start_screen(void)
{
    xputs(TI);
    xputs(VS);
#ifdef PC9800
    if (!iflags.IBMgraphics && !iflags.DECgraphics)
        tty_ascgraphics_hilite_fixup();
    /* set up callback in case option is not set yet but toggled later */
    ascgraphics_mode_callback = tty_ascgraphics_hilite_fixup;
# ifdef ASCIIGRAPH
    if (iflags.IBMgraphics) init_hilite();
    /* set up callback in case option is not set yet but toggled later */
    ibmgraphics_mode_callback = init_hilite;
# endif
#endif /* PC9800 */

#ifdef TERMLIB
    if (iflags.DECgraphics) tty_decgraphics_termcap_fixup();
    /* set up callback in case option is not set yet but toggled later */
    decgraphics_mode_callback = tty_decgraphics_termcap_fixup;
#endif
    if (iflags.num_pad) tty_number_pad(1);  /* make keypad send digits */
}

void
tty_end_screen(void)
{
    clear_screen();
    xputs(VE);
    xputs(TE);
}

/* Cursor movements */

void
nocmov(int x, int y)
{
    if ((int) ttyDisplay->cury > y) {
        if(UP) {
            while ((int) ttyDisplay->cury > y) {    /* Go up. */
                xputs(UP);
                ttyDisplay->cury--;
            }
        } else if(nh_CM) {
            cmov(x, y);
        } else if(HO) {
            home();
            tty_curs(BASE_WINDOW, x+1, y);
        } /* else impossible("..."); */
    } else if ((int) ttyDisplay->cury < y) {
        if(XD) {
            while((int) ttyDisplay->cury < y) {
                xputs(XD);
                ttyDisplay->cury++;
            }
        } else if(nh_CM) {
            cmov(x, y);
        } else {
            while((int) ttyDisplay->cury < y) {
                xputc('\n');
                ttyDisplay->curx = 0;
                ttyDisplay->cury++;
            }
        }
    }
    if ((int) ttyDisplay->curx < x) {       /* Go to the right. */
        if(!nh_ND) cmov(x, y); else /* bah */
            /* should instead print what is there already */
            while ((int) ttyDisplay->curx < x) {
                xputs(nh_ND);
                ttyDisplay->curx++;
            }
    } else if ((int) ttyDisplay->curx > x) {
        while ((int) ttyDisplay->curx > x) {    /* Go to the left. */
            xputs(BC);
            ttyDisplay->curx--;
        }
    }
}

void
cmov(int x, int y)
{
    xputs(tgoto(nh_CM, x, y));
    ttyDisplay->cury = y;
    ttyDisplay->curx = x;
}

int
xputc(int c) /* actually char, but explicitly specify its widened type */
{
    /*
     * Note:  xputc() as a direct all to putchar() doesn't make any
     * sense _if_ putchar() is a function.  But if it is a macro, an
     * overlay configuration would want to avoid hidden code bloat
     * from multiple putchar() expansions.  And it gets passed as an
     * argument to tputs() so we have to guarantee an actual function
     * (while possibly lacking ANSI's (func) syntax to override macro).
     *
     * xputc() used to be declared as 'void xputc(c) char c; {}' but
     * avoiding the proper type 'int' just to avoid (void) casts when
     * ignoring the result can't have been sufficent reason to add it.
     * It also had '#if apollo' conditional to have the arg be int.
     * Matching putchar()'s declaration and using explicit casts where
     * warranted is more robust, so we're just a jacket around that.
     */
    return putchar(c);
}

void
xputs(const char *s)
{
#ifndef TERMLIB
    (void) fputs(s, stdout);
#else
    tputs(s, 1, xputc);
#endif
}

void
cl_end(void)
{
    if(CE)
        xputs(CE);
    else {  /* no-CE fix - free after Harold Rynes */
        /* this looks terrible, especially on a slow terminal
           but is better than nothing */
        int cx = ttyDisplay->curx+1;

        while(cx < CO) {
            xputc(' ');
            cx++;
        }
        tty_curs(BASE_WINDOW, (int)ttyDisplay->curx+1,
                 (int)ttyDisplay->cury);
    }
}

void
clear_screen(void)
{
    /* note: if CL is null, then termcap initialization failed,
        so don't attempt screen-oriented I/O during final cleanup.
     */
    if (CL) {
        xputs(CL);
        home();
    }
}

void
home(void)
{
    if(HO)
        xputs(HO);
    else if(nh_CM)
        xputs(tgoto(nh_CM, 0, 0));
    else
        tty_curs(BASE_WINDOW, 1, 0);    /* using UP ... */
    ttyDisplay->curx = ttyDisplay->cury = 0;
}

void
standoutbeg(void)
{
    if(SO) xputs(SO);
}

void
standoutend(void)
{
    if(SE) xputs(SE);
}

#if 0   /* if you need one of these, uncomment it (here and in extern.h) */
void
revbeg(void)
{
    if(MR) xputs(MR);
}

void
boldbeg(void)
{
    if(MD) xputs(MD);
}

void
blinkbeg(void)
{
    if(MB) xputs(MB);
}

void
dimbeg(void)
/* not in most termcap entries */
{
    if(MH) xputs(MH);
}

void
m_end(void)
{
    if(ME) xputs(ME);
}
#endif

void
backsp(void)
{
    xputs(BC);
}

void
tty_nhbell(void)
{
    if (flags.silent) return;
    (void) putchar('\007');     /* curx does not change */
    (void) fflush(stdout);
}

#ifdef ASCIIGRAPH
void
graph_on(void)
{
    if (AS) xputs(AS);
}

void
graph_off(void)
{
    if (AE) xputs(AE);
}
#endif

#if !defined(MICRO)
# ifdef VMS
static const short tmspc10[] = {        /* from termcap */
    0, 2000, 1333, 909, 743, 666, 333, 166, 83, 55, 50, 41, 27, 20, 13, 10,
    5
};
# else
static const short tmspc10[] = {        /* from termcap */
    0, 2000, 1333, 909, 743, 666, 500, 333, 166, 83, 55, 41, 20, 10, 5
};
# endif
#endif

/* delay 50 ms */
void
tty_delay_output(void)
{
#if defined(MICRO)
    int i;
#endif
    if (iflags.debug_fuzzer) {
        return;
    }
#ifdef TIMED_DELAY
    if (flags.nap) {
        (void) fflush(stdout);
        msleep(50);     /* sleep for 50 milliseconds */
        return;
    }
#endif
#if defined(MICRO)
    /* simulate the delay with "cursor here" */
    for (i = 0; i < 3; i++) {
        cmov(ttyDisplay->curx, ttyDisplay->cury);
        (void) fflush(stdout);
    }
#else /* MICRO */
    /* BUG: if the padding character is visible, as it is on the 5620
       then this looks terrible. */
    if(flags.null)
# ifdef TERMINFO
        tputs("$<50>", 1, xputc);
# else
        tputs("50", 1, xputc);
# endif

    else if(ospeed > 0 && ospeed < SIZE(tmspc10) && nh_CM) {
        /* delay by sending cm(here) an appropriate number of times */
        int cmlen = strlen(tgoto(nh_CM, ttyDisplay->curx,
                                          ttyDisplay->cury));
        int i = 500 + tmspc10[ospeed]/2;

        while(i > 0) {
            cmov((int)ttyDisplay->curx, (int)ttyDisplay->cury);
            i -= cmlen*tmspc10[ospeed];
        }
    }
#endif /* MICRO */
}

void
cl_eos(void) /* free after Robert Viduya */
{            /* must only be called with curx = 1 */

    if(nh_CD)
        xputs(nh_CD);
    else {
        int cy = ttyDisplay->cury+1;
        while(cy <= LI-2) {
            cl_end();
            xputc('\n');
            cy++;
        }
        cl_end();
        tty_curs(BASE_WINDOW, (int)ttyDisplay->curx+1,
                 (int)ttyDisplay->cury);
    }
}

#if defined(TEXTCOLOR) && defined(TERMLIB)
# if defined(UNIX) && defined(TERMINFO)
/*
 * Sets up color highlighting, using terminfo(4) escape sequences.
 *
 * Having never seen a terminfo system without curses, we assume this
 * inclusion is safe.  On systems with color terminfo, it should define
 * the 8 COLOR_FOOs, and avoid us having to guess whether this particular
 * terminfo uses BGR or RGB for its indexes.
 *
 * If we don't get the definitions, then guess.  Original color terminfos
 * used BGR for the original Sf (setf, Standard foreground) codes, but
 * there was a near-total lack of user documentation, so some subsequent
 * terminfos, such as early Linux ncurses and SCO UNIX, used RGB.  Possibly
 * as a result of the confusion, AF (setaf, ANSI Foreground) codes were
 * introduced, but this caused yet more confusion.  Later Linux ncurses
 * have BGR Sf, RGB AF, and RGB COLOR_FOO, which appears to be the SVR4
 * standard.  We could switch the colors around when using Sf with ncurses,
 * which would help things on later ncurses and hurt things on early ncurses.
 * We'll try just preferring AF and hoping it always agrees with COLOR_FOO,
 * and falling back to Sf if AF isn't defined.
 *
 * In any case, treat black specially so we don't try to display black
 * characters on the assumed black background.
 */

/* `curses' is aptly named; various versions don't like these
    macros used elsewhere within nethack; fortunately they're
    not needed beyond this point, so we don't need to worry
    about reconstructing them after the header file inclusion. */
#undef delay_output
#undef TRUE
#undef FALSE
#define m_move curses_m_move    /* Some curses.h decl m_move(), not used here */

#include <curses.h>

#ifndef HAVE_TPARM
extern char *tparm();
#endif

#  ifdef COLOR_BLACK    /* trust include file */
#ifndef VIDEOSHADES
#undef COLOR_BLACK
#endif
#  else
#   ifndef _M_UNIX  /* guess BGR */
#ifdef VIDEOSHADES
#define COLOR_BLACK   0
#endif
#define COLOR_BLUE    1
#define COLOR_GREEN   2
#define COLOR_CYAN    3
#define COLOR_RED     4
#define COLOR_MAGENTA 5
#define COLOR_YELLOW  6
#define COLOR_WHITE   7
#   else        /* guess RGB */
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7
#   endif
#  endif
#ifndef VIDEOSHADES
#define COLOR_BLACK COLOR_BLUE
#endif

#ifdef TTY_GRAPHICS
static struct
{
    int index;
    uint64_t value;
} color_definitions_256[] = {
    {  16, 0x000000 },
    {  17, 0x00005f },
    {  18, 0x000087 },
    {  19, 0x0000af },
    {  20, 0x0000d7 },
    {  21, 0x0000ff },
    {  22, 0x005f00 },
    {  23, 0x005f5f },
    {  24, 0x005f87 },
    {  25, 0x005faf },
    {  26, 0x005fd7 },
    {  27, 0x005fff },
    {  28, 0x008700 },
    {  29, 0x00875f },
    {  30, 0x008787 },
    {  31, 0x0087af },
    {  32, 0x0087d7 },
    {  33, 0x0087ff },
    {  34, 0x00af00 },
    {  35, 0x00af5f },
    {  36, 0x00af87 },
    {  37, 0x00afaf },
    {  38, 0x00afd7 },
    {  39, 0x00afff },
    {  40, 0x00d700 },
    {  41, 0x00d75f },
    {  42, 0x00d787 },
    {  43, 0x00d7af },
    {  44, 0x00d7d7 },
    {  45, 0x00d7ff },
    {  46, 0x00ff00 },
    {  47, 0x00ff5f },
    {  48, 0x00ff87 },
    {  49, 0x00ffaf },
    {  50, 0x00ffd7 },
    {  51, 0x00ffff },
    {  52, 0x5f0000 },
    {  53, 0x5f005f },
    {  54, 0x5f0087 },
    {  55, 0x5f00af },
    {  56, 0x5f00d7 },
    {  57, 0x5f00ff },
    {  58, 0x5f5f00 },
    {  59, 0x5f5f5f },
    {  60, 0x5f5f87 },
    {  61, 0x5f5faf },
    {  62, 0x5f5fd7 },
    {  63, 0x5f5fff },
    {  64, 0x5f8700 },
    {  65, 0x5f875f },
    {  66, 0x5f8787 },
    {  67, 0x5f87af },
    {  68, 0x5f87d7 },
    {  69, 0x5f87ff },
    {  70, 0x5faf00 },
    {  71, 0x5faf5f },
    {  72, 0x5faf87 },
    {  73, 0x5fafaf },
    {  74, 0x5fafd7 },
    {  75, 0x5fafff },
    {  76, 0x5fd700 },
    {  77, 0x5fd75f },
    {  78, 0x5fd787 },
    {  79, 0x5fd7af },
    {  80, 0x5fd7d7 },
    {  81, 0x5fd7ff },
    {  82, 0x5fff00 },
    {  83, 0x5fff5f },
    {  84, 0x5fff87 },
    {  85, 0x5fffaf },
    {  86, 0x5fffd7 },
    {  87, 0x5fffff },
    {  88, 0x870000 },
    {  89, 0x87005f },
    {  90, 0x870087 },
    {  91, 0x8700af },
    {  92, 0x8700d7 },
    {  93, 0x8700ff },
    {  94, 0x875f00 },
    {  95, 0x875f5f },
    {  96, 0x875f87 },
    {  97, 0x875faf },
    {  98, 0x875fd7 },
    {  99, 0x875fff },
    { 100, 0x878700 },
    { 101, 0x87875f },
    { 102, 0x878787 },
    { 103, 0x8787af },
    { 104, 0x8787d7 },
    { 105, 0x8787ff },
    { 106, 0x87af00 },
    { 107, 0x87af5f },
    { 108, 0x87af87 },
    { 109, 0x87afaf },
    { 110, 0x87afd7 },
    { 111, 0x87afff },
    { 112, 0x87d700 },
    { 113, 0x87d75f },
    { 114, 0x87d787 },
    { 115, 0x87d7af },
    { 116, 0x87d7d7 },
    { 117, 0x87d7ff },
    { 118, 0x87ff00 },
    { 119, 0x87ff5f },
    { 120, 0x87ff87 },
    { 121, 0x87ffaf },
    { 122, 0x87ffd7 },
    { 123, 0x87ffff },
    { 124, 0xaf0000 },
    { 125, 0xaf005f },
    { 126, 0xaf0087 },
    { 127, 0xaf00af },
    { 128, 0xaf00d7 },
    { 129, 0xaf00ff },
    { 130, 0xaf5f00 },
    { 131, 0xaf5f5f },
    { 132, 0xaf5f87 },
    { 133, 0xaf5faf },
    { 134, 0xaf5fd7 },
    { 135, 0xaf5fff },
    { 136, 0xaf8700 },
    { 137, 0xaf875f },
    { 138, 0xaf8787 },
    { 139, 0xaf87af },
    { 140, 0xaf87d7 },
    { 141, 0xaf87ff },
    { 142, 0xafaf00 },
    { 143, 0xafaf5f },
    { 144, 0xafaf87 },
    { 145, 0xafafaf },
    { 146, 0xafafd7 },
    { 147, 0xafafff },
    { 148, 0xafd700 },
    { 149, 0xafd75f },
    { 150, 0xafd787 },
    { 151, 0xafd7af },
    { 152, 0xafd7d7 },
    { 153, 0xafd7ff },
    { 154, 0xafff00 },
    { 155, 0xafff5f },
    { 156, 0xafff87 },
    { 157, 0xafffaf },
    { 158, 0xafffd7 },
    { 159, 0xafffff },
    { 160, 0xd70000 },
    { 161, 0xd7005f },
    { 162, 0xd70087 },
    { 163, 0xd700af },
    { 164, 0xd700d7 },
    { 165, 0xd700ff },
    { 166, 0xd75f00 },
    { 167, 0xd75f5f },
    { 168, 0xd75f87 },
    { 169, 0xd75faf },
    { 170, 0xd75fd7 },
    { 171, 0xd75fff },
    { 172, 0xd78700 },
    { 173, 0xd7875f },
    { 174, 0xd78787 },
    { 175, 0xd787af },
    { 176, 0xd787d7 },
    { 177, 0xd787ff },
    { 178, 0xd7af00 },
    { 179, 0xd7af5f },
    { 180, 0xd7af87 },
    { 181, 0xd7afaf },
    { 182, 0xd7afd7 },
    { 183, 0xd7afff },
    { 184, 0xd7d700 },
    { 185, 0xd7d75f },
    { 186, 0xd7d787 },
    { 187, 0xd7d7af },
    { 188, 0xd7d7d7 },
    { 189, 0xd7d7ff },
    { 190, 0xd7ff00 },
    { 191, 0xd7ff5f },
    { 192, 0xd7ff87 },
    { 193, 0xd7ffaf },
    { 194, 0xd7ffd7 },
    { 195, 0xd7ffff },
    { 196, 0xff0000 },
    { 197, 0xff005f },
    { 198, 0xff0087 },
    { 199, 0xff00af },
    { 200, 0xff00d7 },
    { 201, 0xff00ff },
    { 202, 0xff5f00 },
    { 203, 0xff5f5f },
    { 204, 0xff5f87 },
    { 205, 0xff5faf },
    { 206, 0xff5fd7 },
    { 207, 0xff5fff },
    { 208, 0xff8700 },
    { 209, 0xff875f },
    { 210, 0xff8787 },
    { 211, 0xff87af },
    { 212, 0xff87d7 },
    { 213, 0xff87ff },
    { 214, 0xffaf00 },
    { 215, 0xffaf5f },
    { 216, 0xffaf87 },
    { 217, 0xffafaf },
    { 218, 0xffafd7 },
    { 219, 0xffafff },
    { 220, 0xffd700 },
    { 221, 0xffd75f },
    { 222, 0xffd787 },
    { 223, 0xffd7af },
    { 224, 0xffd7d7 },
    { 225, 0xffd7ff },
    { 226, 0xffff00 },
    { 227, 0xffff5f },
    { 228, 0xffff87 },
    { 229, 0xffffaf },
    { 230, 0xffffd7 },
    { 231, 0xffffff },
    { 232, 0x080808 },
    { 233, 0x121212 },
    { 234, 0x1c1c1c },
    { 235, 0x262626 },
    { 236, 0x303030 },
    { 237, 0x3a3a3a },
    { 238, 0x444444 },
    { 239, 0x4e4e4e },
    { 240, 0x585858 },
    { 241, 0x626262 },
    { 242, 0x6c6c6c },
    { 243, 0x767676 },
    { 244, 0x808080 },
    { 245, 0x8a8a8a },
    { 246, 0x949494 },
    { 247, 0x9e9e9e },
    { 248, 0xa8a8a8 },
    { 249, 0xb2b2b2 },
    { 250, 0xbcbcbc },
    { 251, 0xc6c6c6 },
    { 252, 0xd0d0d0 },
    { 253, 0xdadada },
    { 254, 0xe4e4e4 },
    { 255, 0xeeeeee }
};
#endif

const int ti_map[8] = {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
};

typedef struct {
    unsigned char r, g, b;
} RGB;

/** Calculate the color distance between two colors.
 *
 * Algorithm taken from https://www.compuphase.com/cmetric.htm
 **/
int
color_distance(uint64_t rgb1, uint64_t rgb2)
{
    int r1 = (rgb1 >> 16) & 0xFF;
    int g1 = (rgb1 >>  8) & 0xFF;
    int b1 = (rgb1      ) & 0xFF;
    int r2 = (rgb2 >> 16) & 0xFF;
    int g2 = (rgb2 >>  8) & 0xFF;
    int b2 = (rgb2      ) & 0xFF;

    int rmean = ( r1 + r2 ) / 2;
    int r = r1 - r2;
    int g = g1 - g2;
    int b = b1 - b2;
    return ((((512+rmean)*r*r)>>8) + 4*g*g + (((767-rmean)*b*b)>>8));
}

static void
init_color_rgb(int color, uint64_t rgb)
{
    char *tc = "\e[38.2.255.255.255m";
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >>  8) & 0xFF;
    int b = (rgb      ) & 0xFF;

    free((genericptr_t) hilites[color]);
    hilites[color] = (char *) alloc(strlen(tc) + 1);
    char sep = iflags.truecolor_separator;
    Sprintf(hilites[color], "\e[38%c2%c%d%c%d%c%dm", sep, sep, r, sep, g, sep, b);
}

static void
init_hilite(void)
{
    int c;
    char *setf, *scratch;

    for (c = 0; c < SIZE(hilites); c++) {
        hilites[c] = nh_HI;
    }
    hilites[CLR_GRAY] = hilites[NO_COLOR] = (char *)0;

    int colors = tgetnum("Co");
    iflags.color_mode = colors;
    if (colors < 8
        || ((setf = tgetstr("AF", (char **)0)) == (char *)0
            && (setf = tgetstr("Sf", (char **)0)) == (char *)0))
        return;

    for (c = 0; c < CLR_MAX / 2; c++) {
        scratch = tparm(setf, ti_map[c]);
        if (iflags.wc2_newcolors || (c != CLR_GRAY)) {
            hilites[c] = (char *) alloc(strlen(scratch) + 1);
            Strcpy(hilites[c], scratch);
        }
        if (colors >= 16) {
            /* Use proper bright colors if terminal supports them. */
            scratch = tparm(setf, ti_map[c]|BRIGHT);
            hilites[c|BRIGHT] = (char *) alloc(strlen(scratch) + 1);
            Strcpy(hilites[c|BRIGHT], scratch);
        } else {
            /* For terminals supporting only 8 colors, use bold + color for
             * bright colors. */
            if (c != CLR_BLACK) {
                hilites[c|BRIGHT] = (char*) alloc(strlen(scratch)+strlen(MD)+1);
                Strcpy(hilites[c|BRIGHT], MD);
                Strcat(hilites[c|BRIGHT], scratch);
            }
        }
    }

    if (!iflags.wc2_newcolors) {
        hilites[CLR_BLACK] = hilites[CLR_BLUE];
    }

    char *colorterm = getenv("COLORTERM");
    if (colorterm && !strcmpi(colorterm, "truecolor")) {
        if (iflags.wc2_newcolors && colors == 8) {
            /* Peculiar special case.
             * Terminal reports both 8 color support but also truecolor support.
             * For terminals supporting more colors, proper gray will have
             * been set already. */
            init_color_rgb(CLR_BLACK, TERMINAL_COLOR_GRAY_RGB);
        }
        colors = iflags.color_mode = 16777216;
    }

    if (colors == 256) {
        for (c = 0; c < CLR_MAX; c++) {
            if (iflags.color_definitions[c]) {
                int i;
                int color_index = -1;
                int similar = INT_MAX;
                int current;

                for (i = 0; i < SIZE(color_definitions_256); i++) {
                    /* look for an exact match */
                    if (iflags.color_definitions[c] == color_definitions_256[i].value) {
                        color_index = i;
                        break;
                    }
                    /* find a close color match */
                    current = color_distance(iflags.color_definitions[c], color_definitions_256[i].value);
                    if (current < similar) {
                        color_index = i;
                        similar = current;
                    }
                }

                if (color_index >= 0) {
                    iflags.color_definitions[c] = color_definitions_256[color_index].value;
                    int color = color_definitions_256[color_index].index;
                    scratch = tparm(setf, color);
                    free((genericptr_t) hilites[c]);
                    hilites[c] = (char *) alloc(strlen(scratch) + 1);
                    Strcpy(hilites[c], scratch);
                }
            }
        }
    } else if (colors == 16777216) {
        /* There is currently no reliable way to determine what kind of escape
         * sequence is understood by the terminal.
         *
         * Most terminals seem to support the variant with semicolons, so for
         * the time being this is taken as default unless configured
         * differently.
         *
         * https://gist.github.com/XVilka/8346728
         * */
        for (c = 0; c < CLR_MAX; c++) {
            if (iflags.color_definitions[c]) {
                init_color_rgb(c, iflags.color_definitions[c]);
            }
        }
    } else {
        /* no customized color support, clear color definitions */
        for (c = 0; c < CLR_MAX; c++) {
            iflags.color_definitions[c] = 0;
        }
    }
}

# else /* UNIX && TERMINFO */

#  ifndef TOS
/* find the foreground and background colors set by nh_HI or nh_HE */
static void
analyze_seq (str, fg, bg)
char *str;
int *fg, *bg;
{
    int c, code;
    int len;

#   ifdef MICRO
    *fg = CLR_GRAY; *bg = CLR_BLACK;
#   else
    *fg = *bg = NO_COLOR;
#   endif

    c = (str[0] == '\233') ? 1 : 2;  /* index of char beyond esc prefix */
    len = strlen(str) - 1;       /* length excluding attrib suffix */
    if ((c != 1 && (str[0] != '\033' || str[1] != '[')) ||
        (len - c) < 1 || str[len] != 'm')
        return;

    while (c < len) {
        if ((code = atoi(&str[c])) == 0) { /* reset */
            /* this also catches errors */
#   ifdef MICRO
            *fg = CLR_GRAY; *bg = CLR_BLACK;
#   else
            *fg = *bg = NO_COLOR;
#   endif
        } else if (code == 1) { /* bold */
            *fg |= BRIGHT;
#   if 0
            /* I doubt we'll ever resort to using blinking characters,
               unless we want a pulsing glow for something.  But, in case
               we do... - 3. */
        } else if (code == 5) { /* blinking */
            *fg |= BLINK;
        } else if (code == 25) { /* stop blinking */
            *fg &= ~BLINK;
#   endif
        } else if (code == 7 || code == 27) { /* reverse */
            code = *fg & ~BRIGHT;
            *fg = *bg | (*fg & BRIGHT);
            *bg = code;
        } else if (code >= 30 && code <= 37) { /* hi_foreground RGB */
            *fg = code - 30;
        } else if (code >= 40 && code <= 47) { /* hi_background RGB */
            *bg = code - 40;
        }
        while (digit(str[++c]));
        c++;
    }
}
#  endif

/*
 * Sets up highlighting sequences, using ANSI escape sequences (highlight code
 * found in print.c).  The nh_HI and nh_HE sequences (usually from SO) are
 * scanned to find foreground and background colors.
 */

static void
init_hilite(void)
{
    int c;
#  ifdef TOS
    extern unsigned long tos_numcolors; /* in tos.c */
    static char NOCOL[] = "\033b0", COLHE[] = "\033q\033b0";

    if (tos_numcolors <= 2) {
        return;
    }
/* Under TOS, the "bright" and "dim" colors are reversed. Moreover,
 * on the Falcon the dim colors are *really* dim; so we make most
 * of the colors the bright versions, with a few exceptions where
 * the dim ones look OK.
 */
    hilites[0] = NOCOL;
    for (c = 1; c < SIZE(hilites); c++) {
        char *foo;
        foo = (char *) alloc(sizeof("\033b0"));
        if (tos_numcolors > 4)
            Sprintf(foo, "\033b%c", (c&~BRIGHT)+'0');
        else
            Strcpy(foo, "\033b0");
        hilites[c] = foo;
    }

    if (tos_numcolors == 4) {
        TI = "\033b0\033c3\033E\033e";
        TE = "\033b3\033c0\033J";
        nh_HE = COLHE;
        hilites[CLR_GREEN] = hilites[CLR_GREEN|BRIGHT] = "\033b2";
        hilites[CLR_RED] = hilites[CLR_RED|BRIGHT] = "\033b1";
    } else {
        sprintf(hilites[CLR_BROWN], "\033b%c", (CLR_BROWN^BRIGHT)+'0');
        sprintf(hilites[CLR_GREEN], "\033b%c", (CLR_GREEN^BRIGHT)+'0');

        TI = "\033b0\033c\017\033E\033e";
        TE = "\033b\017\033c0\033J";
        nh_HE = COLHE;
        hilites[CLR_WHITE] = hilites[CLR_BLACK] = NOCOL;
        hilites[NO_COLOR] = hilites[CLR_GRAY];
    }

#  else /* TOS */

    int backg, foreg, hi_backg, hi_foreg;

    for (c = 0; c < SIZE(hilites); c++)
        hilites[c] = nh_HI;
    hilites[CLR_GRAY] = hilites[NO_COLOR] = (char *)0;

    analyze_seq(nh_HI, &hi_foreg, &hi_backg);
    analyze_seq(nh_HE, &foreg, &backg);

    for (c = 0; c < SIZE(hilites); c++)
        /* avoid invisibility */
        if ((backg & ~BRIGHT) != c) {
#   ifdef MICRO
            if (c == CLR_BLUE) continue;
#   endif
            if (c == foreg)
                hilites[c] = (char *)0;
            else if (c != hi_foreg || backg != hi_backg) {
                hilites[c] = (char *) alloc(sizeof("\033[%d;3%d;4%dm"));
                Sprintf(hilites[c], "\033[%d", !!(c & BRIGHT));
                if ((c | BRIGHT) != (foreg | BRIGHT))
                    Sprintf(eos(hilites[c]), ";3%d", c & ~BRIGHT);
                if (backg != CLR_BLACK)
                    Sprintf(eos(hilites[c]), ";4%d", backg & ~BRIGHT);
                Strcat(hilites[c], "m");
            }
        }

#   ifdef MICRO
    /* brighten low-visibility colors */
    hilites[CLR_BLUE] = hilites[CLR_BLUE|BRIGHT];
#   endif
#  endif /* TOS */
}
# endif /* UNIX */

static void
kill_hilite(void)
{
# ifndef TOS
    int c;

    for (c = 0; c < CLR_MAX / 2; c++) {
        if ((!iflags.wc2_newcolors) &&
            (c == CLR_BLUE || c == CLR_GRAY)) continue;

        if (hilites[c|BRIGHT] == hilites[c]) hilites[c|BRIGHT] = 0;
        if (hilites[c] && (hilites[c] != nh_HI))
            free((genericptr_t) hilites[c]),  hilites[c] = 0;
        if (hilites[c|BRIGHT] && (hilites[c|BRIGHT] != nh_HI))
            free((genericptr_t) hilites[c|BRIGHT]),  hilites[c|BRIGHT] = 0;
    }
# endif
    return;
}
#endif /* TEXTCOLOR */


static char nulstr[] = "";

static char *
s_atr2str(int n)
{
    switch (n) {
    case ATR_BLINK:
    case ATR_ULINE:
        if (n == ATR_BLINK) {
            if (MB && *MB) {
                return MB;
            }
        } else {
            /* Underline */
            if (nh_US && *nh_US) {
                return nh_US;
            }
        }
        /* fall through */

    case ATR_BOLD:
        if (MD && *MD) {
            return MD;
        }
        if (nh_HI && *nh_HI) {
            return nh_HI;
        }
        break;

    case ATR_INVERSE:
        if (MR && *MR) {
            return MR;
        }
        break;

    case ATR_DIM:
        if (MH && *MH) {
            return MH;
        }
        break;

    case ATR_ITALIC:
        if (ZH && *ZH) {
            return ZH;
        }
        break;
    }
    return nulstr;
}

static char *
e_atr2str(int n)
{
    switch (n) {
    case ATR_ULINE:
        if (nh_UE && *nh_UE) {
            return nh_UE;
        }
        /* fall through */

    case ATR_BOLD:
    case ATR_BLINK:
        if (nh_HE && *nh_HE) {
            return nh_HE;
        }
        /* fall through */

    case ATR_DIM:
    case ATR_INVERSE:
        if (ME && *ME) {
            return ME;
        }
        break;

    case ATR_ITALIC:
        if (ZR && *ZR) {
            return ZR;
        }
        break;
    }
    return nulstr;
}


void
term_start_attr(int attr)
{
    if (attr) {
        xputs(s_atr2str(attr));
    }
}


void
term_end_attr(int attr)
{
    if(attr) {
        xputs(e_atr2str(attr));
    }
}


void
term_start_raw_bold(void)
{
    xputs(nh_HI);
}


void
term_end_raw_bold(void)
{
    xputs(nh_HE);
}


#ifdef TEXTCOLOR

void
term_end_color(void)
{
    xputs(nh_HE);
}


void
term_start_color(int color)
{
    if (iflags.wc2_newcolors)
        xputs(hilites[ttycolors[color]]);
    else
        xputs(hilites[color]);
}


int
has_color(int color)
{
#ifdef X11_GRAPHICS
    /* XXX has_color() should be added to windowprocs */
    if (windowprocs.name != NULL &&
        !strcmpi(windowprocs.name, "X11")) return TRUE;
#endif
#ifdef GEM_GRAPHICS
    /* XXX has_color() should be added to windowprocs */
    if (windowprocs.name != NULL &&
        !strcmpi(windowprocs.name, "Gem")) return TRUE;
#endif
#ifdef LISP_GRAPHICS
    /* XXX has_color() should be added to windowprocs */
    if (windowprocs.name != NULL &&
        !strcmpi(windowprocs.name, "lisp")) return TRUE;
#endif
#ifdef QT_GRAPHICS
    /* XXX has_color() should be added to windowprocs */
    if (windowprocs.name != NULL &&
        !strcmpi(windowprocs.name, "Qt")) return TRUE;
#endif
#ifdef AMII_GRAPHICS
    /* hilites[] not used */
    return iflags.use_color;
#endif
#ifdef CURSES_GRAPHICS
    /* XXX has_color() should be added to windowprocs */
    /* iflags.wc_color is set to false and the option disabled if the
       terminal cannot display color */
    if (windowprocs.name != NULL &&
        !strcmpi(windowprocs.name, "curses")) return iflags.wc_color;
#endif
    return hilites[color] != (char *)0;
}

#endif /* TEXTCOLOR */

#endif /* TTY_GRAPHICS */

/*termcap.c*/
