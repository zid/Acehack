/*	SCCS Id: @(#)wintty.c	3.4	2002/09/27	*/
/* Copyright (c) David Cohrs, 1991				  */
/* Modified 21 Apr 2011 by Alex Smith */
/* NetHack may be freely redistributed.	 See license for details. */

/*
 * Neither a standard out nor character-based control codes should be
 * part of the "tty look" windowing implementation.
 * h+ 930227
 */

#include "hack.h"
#include "dlb.h"
#ifdef SHORT_FILENAMES
#include "patchlev.h"
#else
#include "patchlevel.h"
#endif

#ifdef TTY_GRAPHICS

#ifdef MAC
# define MICRO /* The Mac is a MICRO only for this file, not in general! */
# ifdef THINK_C
extern void msmsg(const char *,...);
# endif
#endif


#ifndef NO_TERMS
#include "tcap.h"
#endif

#include "wintty.h"

#ifdef CLIPPING		/* might want SIGWINCH */
# if defined(BSD) || defined(ULTRIX) || defined(AIX_31) || defined(_BULL_SOURCE)
#include <signal.h>
# endif
#endif

extern char mapped_menu_cmds[]; /* from options.c */

/* Interface definition, for windows.c */
struct window_procs tty_procs = {
    "tty",
#ifdef MSDOS
    WC_TILED_MAP|WC_ASCII_MAP|
#endif
#if defined(WIN32CON)
    WC_MOUSE_SUPPORT|
#endif
    WC_COLOR|WC_HILITE_PET|WC_INVERSE|WC_EIGHT_BIT_IN,
    0L,
    tty_init_nhwindows,
    tty_player_selection,
    tty_game_mode_selection,
    tty_askname,
    tty_get_nh_event,
    tty_exit_nhwindows,
    tty_suspend_nhwindows,
    tty_resume_nhwindows,
    tty_create_nhwindow,
    tty_clear_nhwindow,
    tty_display_nhwindow,
    tty_destroy_nhwindow,
    tty_curs,
    tty_suppress_more,
    tty_putstr,
    tty_putstr_colored,
    tty_display_file,
    tty_start_menu,
    tty_add_menu,
    tty_add_menu_colored,
    tty_end_menu,
    tty_select_menu,
    tty_message_menu,
    tty_update_inventory,
    tty_mark_synch,
    tty_wait_synch,
#ifdef CLIPPING
    tty_cliparound,
#endif
#ifdef POSITIONBAR
    tty_update_positionbar,
#endif
    tty_print_glyph,
    tty_raw_print,
    tty_raw_print_bold,
    tty_nhgetch,
    tty_nh_poskey,
    tty_nhbell,
    tty_doprev_message,
    tty_yn_function,
    tty_getlin,
    tty_get_ext_cmd,
    tty_number_pad,
    tty_delay_output,
#ifdef CHANGE_COLOR	/* the Mac uses a palette device */
    tty_change_color,
#ifdef MAC
    tty_change_background,
    set_tty_font_name,
#endif
    tty_get_color_string,
#endif

    /* other defs that really should go away (they're tty specific) */
    tty_start_screen,
    tty_end_screen,
    genl_outrip,
#if defined(WIN32CON)
    nttty_preference_update,
#else
    genl_preference_update,
#endif
};

static int maxwin = 0;			/* number of windows in use */
winid BASE_WINDOW;
struct WinDesc *wins[MAXWIN];
struct DisplayDesc *ttyDisplay;	/* the tty display descriptor */

extern void FDECL(cmov, (int,int)); /* from termcap.c */
extern void FDECL(nocmov, (int,int)); /* from termcap.c */
#if defined(UNIX) || defined(VMS)
static char obuf[BUFSIZ];	/* BUFSIZ is defined in stdio.h */
#endif

static char winpanicstr[] = "Bad window id %d";
char defmorestr[] = "--More--";

#ifdef CLIPPING
# if defined(USE_TILES) && defined(MSDOS)
boolean clipping = FALSE;	/* clipping on? */
int clipx = 0, clipxmax = 0;
# else
static boolean clipping = FALSE;	/* clipping on? */
static int clipx = 0, clipxmax = 0;
# endif
static int clipy = 0, clipymax = 0;
#endif /* CLIPPING */

#if defined(USE_TILES) && defined(MSDOS)
extern void FDECL(adjust_cursor_flags, (struct WinDesc *));
#endif

#if defined(ASCIIGRAPH) && !defined(NO_TERMS)
boolean GFlag = FALSE;
boolean HE_resets_AS;	/* see termcap.c */
#endif

#if defined(MICRO) || defined(WIN32CON)
static const char to_continue[] = "to continue";
#define getret() getreturn(to_continue)
#else
STATIC_DCL void NDECL(getret);
#endif
STATIC_DCL void FDECL(tty_start_color, (int));
STATIC_DCL void NDECL(tty_end_color);
STATIC_DCL void FDECL(tty_start_attr, (int));
STATIC_DCL void FDECL(tty_end_attr, (int));
STATIC_DCL void FDECL(erase_menu_or_text, (winid, struct WinDesc *, BOOLEAN_P));
STATIC_DCL void FDECL(free_window_info, (struct WinDesc *, BOOLEAN_P));
STATIC_DCL void FDECL(dmore,(struct WinDesc *, const char *));
STATIC_DCL void FDECL(set_item_state, (winid, int, tty_menu_item *));
STATIC_DCL void FDECL(set_all_on_page, (winid,tty_menu_item *,tty_menu_item *));
STATIC_DCL void FDECL(unset_all_on_page, (winid,tty_menu_item *,tty_menu_item *));
STATIC_DCL void FDECL(invert_all_on_page, (winid,tty_menu_item *,tty_menu_item *, CHAR_P));
STATIC_DCL void FDECL(invert_all, (winid,tty_menu_item *,tty_menu_item *, CHAR_P));
STATIC_DCL void FDECL(process_menu_window, (winid,struct WinDesc *));
STATIC_DCL void FDECL(process_text_window, (winid,struct WinDesc *));
STATIC_DCL tty_menu_item *FDECL(reverse, (tty_menu_item *));
STATIC_DCL const char * FDECL(compress_str, (const char *));
STATIC_DCL void FDECL(tty_putsym, (winid, int, int, CHAR_P));
static char *FDECL(copy_of, (const char *));
STATIC_DCL void FDECL(bail, (const char *));	/* __attribute__((noreturn)) */

/*
 * A string containing all the default commands -- to add to a list
 * of acceptable inputs.
 */
static const char default_menu_cmds[] = {
	MENU_FIRST_PAGE,
	MENU_LAST_PAGE,
	MENU_NEXT_PAGE,
	MENU_PREVIOUS_PAGE,
	MENU_SELECT_ALL,
	MENU_UNSELECT_ALL,
	MENU_INVERT_ALL,
	MENU_SELECT_PAGE,
	MENU_UNSELECT_PAGE,
	MENU_INVERT_PAGE,
	0	/* null terminator */
};


/* clean up and quit */
STATIC_OVL void
bail(mesg)
const char *mesg;
{
    clearlocks();
    tty_exit_nhwindows(mesg);
    terminate(EXIT_SUCCESS);
    /*NOTREACHED*/
}

#if defined(SIGWINCH) && defined(CLIPPING)
STATIC_OVL void
winch()
{
    int oldLI = LI, oldCO = CO, i;
    register struct WinDesc *cw;

    getwindowsz();
    if((oldLI != LI || oldCO != CO) && ttyDisplay) {
	ttyDisplay->rows = LI;
	ttyDisplay->cols = CO;

	cw = wins[BASE_WINDOW];
	cw->rows = ttyDisplay->rows;
	cw->cols = ttyDisplay->cols;

	if(iflags.window_inited) {
	    cw = wins[WIN_MESSAGE];
	    cw->curx = cw->cury = 0;

	    tty_destroy_nhwindow(WIN_STATUS);
	    WIN_STATUS = tty_create_nhwindow(NHW_STATUS);

	    if(u.ux) {
#ifdef CLIPPING
		if(CO < COLNO || LI < ROWNO+3) {
		    setclipped();
		    tty_cliparound(u.ux, u.uy);
		} else {
		    clipping = FALSE;
		    clipx = clipy = 0;
		}
#endif
		i = ttyDisplay->toplin;
		ttyDisplay->toplin = 0;
		docrt();
		bot();
		ttyDisplay->toplin = i;
		flush_screen(1);
		if(i) {
		    addtopl(toplines);
		} else
		    for(i=WIN_INVEN; i < MAXWIN; i++)
			if(wins[i] && wins[i]->active) {
			    /* cop-out */
			    addtopl("Press Return to continue: ");
			    break;
			}
		(void) fflush(stdout);
		if(i < 2) flush_screen(1);
	    }
	}
    }
}
#endif

/*ARGSUSED*/
void
tty_init_nhwindows(argcp,argv)
int* argcp;
char** argv;
{
    int wid, hgt;

    /*
     *	Remember tty modes, to be restored on exit.
     *
     *	gettty() must be called before tty_startup()
     *	  due to ordering of LI/CO settings
     *	tty_startup() must be called before initoptions()
     *	  due to ordering of graphics settings
     */
#if defined(UNIX) || defined(VMS)
    setbuf(stdout,obuf);
#endif
    gettty();

    /* to port dependant tty setup */
    tty_startup(&wid, &hgt);
    setftty();			/* calls start_screen */

    /* set up tty descriptor */
    ttyDisplay = (struct DisplayDesc*) alloc(sizeof(struct DisplayDesc));
    ttyDisplay->toplin = 0;
    ttyDisplay->rows = hgt;
    ttyDisplay->cols = wid;
    ttyDisplay->curx = ttyDisplay->cury = 0;
    ttyDisplay->inmore = ttyDisplay->inread = ttyDisplay->intr = 0;
    ttyDisplay->dismiss_more = 0;
    ttyDisplay->attrs = 0;

    /* set up the default windows */
    BASE_WINDOW = tty_create_nhwindow(NHW_BASE);
    wins[BASE_WINDOW]->active = 1;

    ttyDisplay->lastwin = WIN_ERR;

#if defined(SIGWINCH) && defined(CLIPPING)
    (void) signal(SIGWINCH, winch);
#endif

    /* add one a space forward menu command alias */
    add_menu_cmd_alias(' ', MENU_NEXT_PAGE);

    /* Clear the screen with the background color. */
    onechar(0);
    tty_clear_nhwindow(BASE_WINDOW);

    tty_putstr(BASE_WINDOW, 0, "");
    tty_putstr(BASE_WINDOW, 0, COPYRIGHT_BANNER_A);
    tty_putstr(BASE_WINDOW, 0, COPYRIGHT_BANNER_B);
    tty_putstr(BASE_WINDOW, 0, COPYRIGHT_BANNER_C);
    tty_putstr(BASE_WINDOW, 0, "");
    tty_display_nhwindow(BASE_WINDOW, FALSE);
}

/**
   Runs before player selection, and is used to determine which game
   mode to play in. If the choice is to continue or start a new game, it
   sets the character name to determine which (currently unused name for
   the player = new game, currently used name = continue that game);
   otherwise, it implements the choice itself then either loops or
   exits.
*/
void
tty_game_mode_selection()
{
  dlb *fp;
  int c;
  char** saved;
  char** sp;
  int plname_in_saved = 0;
  int plname_was_explicit = !!*plname;

  saved = get_saved_games();

  if (*plname && saved && *saved) {
    for (sp = saved; *sp; sp++) {
      /* Note that this means that public servers need to prevent two
         users with the same name but different capitalisation. (If
         they aren't, it's likely to cause problems anyway...) */
      if (!strcmpi(plname, *sp)) {
        plname_in_saved = 1;
        /* Fix capitalisation of the name to match the saved game. */
        strncpy(plname, *sp, sizeof(plname)-1);
      }
    }
  } else if (*plname) {
    set_savefile_name();
    /* Let's just try to open the savefile directly to see if it exists */
    plname_in_saved = verify_savefile();
  }

retry_mode_selection:
  tty_clear_nhwindow(BASE_WINDOW);
  curs(BASE_WINDOW,1,0);

  fp = dlb_fopen("logo.vt100", RDBMODE);
  if (!fp) {
    panic("Cannot find data file 'logo.vt100'");
  }
  while ((c = dlb_fgetc(fp)) != EOF) {
    /* TODO: Strip color if the options specify we can't show it on
       this terminal. (Part of the reason the options setup is shown
       on first load if there's no RC is so that color can be turned
       off before it makes the game unplayable on such terminals.)
       Converting the vt100 codes via termcap may also make sense. */
    if (c == 1) { /* gray out if there's no game to continue */
      if (!*plname && (!saved || !*saved)) xputs("\033[31m");
      else if (*plname && !plname_in_saved) xputs("\033[31m");
      /* and bold if there is */
      else xputs("\033[1m");
    }
    else if (c == 2) { /* gray out if a game must be continued */
      if (*plname && plname_in_saved) xputs("\033[31m");
    }
    else xputc(c); /* low-level as codes are in the file itself */
  }
  dlb_fclose(fp);

reread_char:
  c = lowc(readchar());
  switch (c) {
  case 'c': /* continue game */
    if (*plname && plname_in_saved) break; /* just load by name */
    if (!saved || !*saved) goto reread_char;
    if (*plname && !plname_in_saved) goto reread_char;
    if (!saved[1]) {
      /* only one game to continue */
      (void) strncpy(plname, *saved, sizeof(plname)-1);
    } else {
      /* we'll have to ask which game to continue */
      winid win;
      anything any;
      menu_item *selected = 0;
      tty_clear_nhwindow(BASE_WINDOW);
      curs(BASE_WINDOW,1,0);
      win = create_nhwindow(NHW_MENU);
      start_menu(win);
      any.a_void = 0;
      for (sp = saved; *sp; sp++) {
        any.a_void = (void*) *sp;
        add_menu(win, NO_GLYPH, &any, 0, 0, ATR_NONE, *sp, MENU_UNSELECTED);
      }
      end_menu(win, "Continue which saved game?");
      c = select_menu(win, PICK_ONE, &selected);
      destroy_nhwindow(win);
      if (c != 1) {
        free((genericptr_t) selected);
        goto retry_mode_selection;
      }
      (void) strncpy(plname, (char*)selected[0].item.a_void, sizeof(plname)-1);
      free((genericptr_t) selected);
    }
    break;
  case 'n': case 't': case 'm': /* the new game options */
    /* if the name is forced by the -u option, don't give an option to
       start a new game, as we're forced onto the existing game */
    if (*plname && plname_in_saved) goto reread_char;
    tty_clear_nhwindow(BASE_WINDOW);
    curs(BASE_WINDOW,1,0);
    if (!*plname) {
      tty_askname();
      if (saved)
        for (sp = saved; *sp; sp++) {
          if (!strcmpi(plname, *sp)) {
            pline("A game is running with that name already.");
            tty_dismiss_nhwindow(NHW_MESSAGE); /* force a --More-- */
            *plname = 0;
            goto retry_mode_selection;
          }
        }
    }
    if (c == 'm') {
      tty_clear_nhwindow(BASE_WINDOW);
      curs(BASE_WINDOW,1,0);
      fp = dlb_fopen("modemenu", RDBMODE);
      if (!fp) {
        panic("Cannot find data file 'modemenu'");
      }
      while ((c = dlb_fgetc(fp)) != EOF) xputc(c);
      dlb_fclose(fp);
      do {
        c = lowc(readchar());
        switch(c) {
          /* -D, -X on command line are overriden by selecting a normal game
             explicitly */
        case 'n': wizard = FALSE; discover = FALSE; break;
        case 't': break;
        case 'd':
#ifdef WIZARD
          /* The rules here are a little complex:
             - with no -u (local play, private server play), anyone
               can enter debug mode;
             - with -u set (generally public server play), only an
               authorized user can enter debug mode.
             Thus people can enter it freely in their own compiles,
             but on public servers likely only the admin can enter it,
             as only they can tweak the command line. (-D works too
             to enter wizmode, whatever the -u option, as if it's
             given the player has control over the command line anyway.) */
          if (plname_was_explicit && strcmpi(plname,WIZARD)) {
            extern int wiz_error_flag; /* from unixmain.c */
            tty_clear_nhwindow(BASE_WINDOW);
            curs(BASE_WINDOW,1,0);
            wiz_error_flag = TRUE;
          } else {
            wizard = TRUE;
            c = 'n';
            break;
          }
          /*FALLTHRU*/
#endif /* otherwise fall through, as debug mode isn't compiled in */
        case 'x': discover = TRUE; c = 'n'; break;
        case 's': solo = TRUE; c = 'n'; break;
        case '\033': goto retry_mode_selection;
        default: continue;
        }
      } while (c != 'n' && c != 't');
    }
    if (c == 't') flags.tutorial = 1;
    break;
  case 's': /* show high score list */
    prscore_interactive();
    goto retry_mode_selection;
  case 'o': /* options */
    tty_clear_nhwindow(BASE_WINDOW);
    curs(BASE_WINDOW,1,0);
    doset();
    goto retry_mode_selection;
  case '?': /* help */
    tty_clear_nhwindow(BASE_WINDOW);
    curs(BASE_WINDOW,1,0);
    dohelp();
    tty_dismiss_nhwindow(NHW_MESSAGE); /* force a --More-- */
    goto retry_mode_selection;
  case 'q': case '\033': /* exit */
    bail((char *)0);
    /*NOTREACHED*/
  default:
    goto reread_char;
  }

  if (saved) free_saved_games(saved);
}


struct player_selection_menu_option {
	int x, y;
	char accel;
	char* item;
	int* savein;
	int (*convfunction)();
	boolean (*validator)();
} player_selection_menu_options[] = {
	{ 2, 4, 'a', "Archeologist", &flags.initrole, str2role, ok_role },
        { 2, 5, 'b', "Barbarian",    &flags.initrole, str2role, ok_role },
        { 2, 6, 'c', "Caveman",      &flags.initrole, str2role, ok_role },
        { 2, 7, 'h', "Healer",       &flags.initrole, str2role, ok_role },
        { 2, 8, 'k', "Knight",       &flags.initrole, str2role, ok_role },
        { 2, 9, 'm', "Monk",         &flags.initrole, str2role, ok_role },
        { 2,10, 'p', "Priest",       &flags.initrole, str2role, ok_role },
        { 2,11, 'R', "Ranger",       &flags.initrole, str2role, ok_role },
        { 2,12, 'r', "Rogue",        &flags.initrole, str2role, ok_role },
        { 2,13, 's', "Samurai",      &flags.initrole, str2role, ok_role },
        { 2,14, 't', "Tourist",      &flags.initrole, str2role, ok_role },
        { 2,15, 'v', "Valkyrie",     &flags.initrole, str2role, ok_role },
        { 2,16, 'w', "Wizard",       &flags.initrole, str2role, ok_role },
        { 2,17, '*', "(random)",     0,               0,        0       },
        {21, 4, 'H', "Human",        &flags.initrace, str2race, ok_race },
        {21, 5, 'D', "Dwarf",        &flags.initrace, str2race, ok_race },
        {21, 6, 'E', "Elf",          &flags.initrace, str2race, ok_race },
        {21, 7, 'G', "Gnome",        &flags.initrace, str2race, ok_race },
        {21, 8, 'O', "Orc",          &flags.initrace, str2race, ok_race },
        {21,11, 'M', "Male",         &flags.initgend, str2gend, ok_gend },
        {21,12, 'F', "Female",       &flags.initgend, str2gend, ok_gend },
        {21,15, 'L', "Lawful",       &flags.initalign,str2align,ok_align},
        {21,16, 'N', "Neutral",      &flags.initalign,str2align,ok_align},
        {21,17, 'C', "Chaotic",      &flags.initalign,str2align,ok_align},
        { 2,19, 'q', "quit",         0,               0,        0},
        {13,19, '#', "reroll",       0,               0,        0},
        {26,19, '.', "play!",        0,               0,        0},
        {0,0,0,0,0,0} /* fencepost */
};

void
tty_player_selection()
{
	char obuf[BUFSZ];
        boolean reroll = FALSE, xallowed = TRUE;
        int ch = 0;
        long scumcount = flags.startscum && !solo;
        const char *line1, *line2, *line3;

        /* If in tutorial mode, narrow the character selection */
        if (flags.tutorial) {
            winid win;
            anything any;
            int n;
            menu_item *selected = 0;
            tty_clear_nhwindow(BASE_WINDOW);
            tty_putstr(BASE_WINDOW, 0, "Choose a Character");
            win = create_nhwindow(NHW_MENU);
            start_menu(win);
            any.a_int = 1;
            add_menu(win, NO_GLYPH, &any, 'v', 0, ATR_NONE,
                     "lawful female dwarf Valkyrie (uses melee and thrown weapons)",
                     MENU_UNSELECTED);
            any.a_int = 2;
            add_menu(win, NO_GLYPH, &any, 'w', 0, ATR_NONE,
                     "chaotic male elf Wizard      (relies mostly on spells)",
                     MENU_UNSELECTED);
            any.a_int = 3;
            add_menu(win, NO_GLYPH, &any, 'R', 0, ATR_NONE,
                     "neutral female human Ranger  (good with ranged combat)",
                     MENU_UNSELECTED);
            any.a_int = 4;
            add_menu(win, NO_GLYPH, &any, 'q', 0, ATR_NONE,
                     "quit", MENU_UNSELECTED);
            end_menu(win, "What character do you want to try?");
            n = select_menu(win, PICK_ONE, &selected);
            destroy_nhwindow(win);
            if (n != 1 || selected[0].item.a_int == 4) bail((char*) 0);
            switch (selected[0].item.a_int) {
            case 1:
                flags.initrole = str2role("Valkyrie");
                flags.initrace = str2race("dwarf");
                flags.initgend = str2gend("female");
                flags.initalign = str2align("lawful");
                break;
            case 2:
                flags.initrole = str2role("Wizard");
                flags.initrace = str2race("elf");
                flags.initgend = str2gend("male");
                flags.initalign = str2align("chaotic");
                break;
            case 3:
                flags.initrole = str2role("Ranger");
                flags.initrace = str2race("human");
                flags.initgend = str2gend("female");
                flags.initalign = str2align("neutral");
                break;
            default: panic("Impossible menu selection"); break;
            }
            free((genericptr_t) selected);
        }

	/* If there's some info in the RC, optimise it before using it */
	rigid_role_checks();

        /* Grab ourselves a random character that's as consistent with
           the RC as possible */
        if (flags.initrole < 0)
          flags.initrole = pick_role(flags.initrace, flags.initgend,
                                     flags.initalign, PICK_RANDOM);
        if (flags.initrole < 0)
          flags.initrole = randrole();

	if (flags.initrace < 0 || !validrace(flags.initrole, flags.initrace))
          flags.initrace = pick_race(flags.initrole, flags.initgend,
                                     flags.initalign, PICK_RANDOM);
        if (flags.initrace < 0)
          flags.initrace = randrace(flags.initrole);

	if (flags.initgend < 0 || !validgend(flags.initrole, flags.initrace,
						flags.initgend))
          flags.initgend = pick_gend(flags.initrole, flags.initrace,
                                     flags.initalign, PICK_RANDOM);
        if (flags.initgend < 0)
          flags.initgend = randgend(flags.initrole, flags.initrace);

	if (flags.initalign < 0 || !validalign(flags.initrole, flags.initrace,
							flags.initalign))
          flags.initalign = pick_align(flags.initrole, flags.initrace,
                                       flags.initgend, PICK_RANDOM);
        if (flags.initalign < 0)
          flags.initalign = randalign(flags.initrole, flags.initrace);

        newgame_part_2();

        /* Fixed parts of the window */
        tty_clear_nhwindow(BASE_WINDOW);

        curs(BASE_WINDOW, 6, 1);
        tty_putstr_colored(BASE_WINDOW, 0, CLR_WHITE, "Create your character:");
        curs(BASE_WINDOW, 40, 1);
        tty_putstr(BASE_WINDOW, ATR_INVERSE, "Inventory");
        curs(BASE_WINDOW, 6, 3);
        tty_putstr(BASE_WINDOW, ATR_INVERSE, "Class");
        curs(BASE_WINDOW, 25, 3);
        tty_putstr(BASE_WINDOW, ATR_INVERSE, "Race");
        curs(BASE_WINDOW, 25, 10);
        tty_putstr(BASE_WINDOW, ATR_INVERSE, "Gender");
        curs(BASE_WINDOW, 25, 14);
        tty_putstr(BASE_WINDOW, ATR_INVERSE, "Alignment");

        /* Default advice blurb; 3 lines of 78 characters each */
        curs(BASE_WINDOW, 2, 21);
        tty_putstr(BASE_WINDOW, 0, ((line1=
"Choose the class, race, gender, and alignment of your character. Information")));
        curs(BASE_WINDOW, 2, 22);
        tty_putstr(BASE_WINDOW, 0, ((line2=
"about your choices will appear here as you make them, to help you decide which")));
        curs(BASE_WINDOW, 2, 23);
        tty_putstr(BASE_WINDOW, 0, ((line3=
"character to play; press . when you're happy with what you have.")));

        /* Changing parts of the window */
        do {
            struct player_selection_menu_option* p = player_selection_menu_options;
            int savestat, i;
            struct obj * otmp;
            if (flags.tutorial) break;
            while (p->x) {
                int color = CLR_GRAY;
                curs(BASE_WINDOW, p->x, p->y);
                Sprintf(obuf, "%c %c %s", p->accel,
                        p->savein && p->convfunction(p->item) == *(p->savein) ? '+' : '-',
                        p->item);
                /* We colour a crga in brown if it couldn't be changed to without
                   having to change some other aspect of the character */
                if (p->savein) {
                    savestat = *(p->savein);
                    *(p->savein) = p->convfunction(p->item);
                    if (!p->validator(flags.initrole,flags.initrace,
                                      flags.initgend,flags.initalign)) {
                        color = CLR_BROWN;
                        if (obuf[2] == '+') obuf[2] = '!';
                    }
                    *(p->savein) = savestat;
                }
                if (obuf[2] != '-' || p->accel == '.') color += BRIGHT;
                tty_putstr_colored(BASE_WINDOW, 0, color, obuf);
                p++;
            }
            if (ok_role (flags.initrole,flags.initrace,flags.initgend,flags.initalign) &&
                ok_race (flags.initrole,flags.initrace,flags.initgend,flags.initalign) &&
                ok_gend (flags.initrole,flags.initrace,flags.initgend,flags.initalign) &&
                ok_align(flags.initrole,flags.initrace,flags.initgend,flags.initalign)) {
                if (reroll) {
                    reroll = FALSE;
                    newgame_part_2(); /* generate a character */
                    if (scumcount > 0) scumcount++;
                }
                otmp = invent;
                xallowed = TRUE;
            } else {otmp = 0; xallowed = FALSE;}
            if (scumcount == 0) otmp = 0;
            curs(BASE_WINDOW, 40, 19);
            if (otmp) {
                /* Show starting stats */
                if (ACURR(A_STR) > 18) {
                    if (ACURR(A_STR) > STR18(100))
                        Sprintf(obuf,"St:%2d ",ACURR(A_STR)-100);
                    else if (ACURR(A_STR) < STR18(100))
                        Sprintf(obuf, "St:18/%02d ",ACURR(A_STR)-18);
                    else
                        Sprintf(obuf,"St:18/** ");
                } else
                    Sprintf(obuf, "St:%-1d ",ACURR(A_STR));
                Sprintf(eos(obuf), "Dx:%-1d Co:%-1d In:%-1d Wi:%-1d Ch:%-1d",
                  ACURR(A_DEX), ACURR(A_CON), ACURR(A_INT), ACURR(A_WIS), ACURR(A_CHA));
                tty_putstr(BASE_WINDOW, 0, obuf);
            }
            cl_end();
            for (i = 2; i <= 17; i++) {
                curs(BASE_WINDOW, 40, i);
                if (otmp) {
                    unsigned save_ocknown;
                    int attr, color;
                    char* p;
                    /* Things get a bit complex here, because the item in
                       question is not IDed at this point in the newgame
                       sequence (it gets IDed in part 3). Thus, we temporarily
                       ID it for display purposes now, then revert; that way,
                       one character that isn't used doesn't ID items for
                       another character that is. */
                    save_ocknown = objects[otmp->otyp].oc_name_known;
                    objects[otmp->otyp].oc_name_known = 1;
                    attr = ATR_NONE;
                    color = default_item_color(otmp);
                    Strcpy(obuf,doname(otmp));
                    /* Remove "being worn", etc, from end of the name. */
                    p = strstri(obuf,"(being worn)");
                    if (p) *p = 0;
                    p = strstri(obuf,"(wielded)");
                    if (p) *p = 0;
                    p = strstri(obuf,"(weapon in ");
                    if (p) *p = 0;
                    p = strstri(obuf,"(alternate weapon");
                    if (p) *p = 0;
                    p = strstri(obuf,"(in quiver)");
                    if (p) *p = 0;
                    p = strstri(obuf,"(on right ");
                    if (p) *p = 0;
                    p = strstri(obuf,"(on left ");
                    if (p) *p = 0;
                    if (iflags.use_menu_color) {
                        get_menu_coloring(obuf, &color, &attr);
                    }
                    while (eos(obuf)[-1] == ' ') eos(obuf)[-1] = '\0';
                    if (strlen(obuf) >= 41) {
                        obuf[37]=obuf[38]=obuf[39]='.';
                        obuf[40]=0;
                    }
                    tty_putstr_colored(BASE_WINDOW, attr, color, obuf);
                    objects[otmp->otyp].oc_name_known = save_ocknown;
                    otmp = otmp->nobj;
                } else if (i == 2 && scumcount == 0 && !solo) {
                    tty_putstr(BASE_WINDOW, 0,
                               "(press i to view inventory and stats)");
                } else if (i == 2 && solo) {
                    tty_putstr(BASE_WINDOW, 0,
                               "(not viewable in solo mode)");
                }
                cl_end();
            }
            if (!xallowed) {
                line1 =
"This is not a legal character; two or more of the settings you requested";
                line2 =
"(marked by exclamation marks) are not legal in combination. Try changing some";
                line3 =
"of those settings to other values.";
            }
            curs(BASE_WINDOW, 2, 21); cl_end();
            curs(BASE_WINDOW, 2, 21); tty_putstr(BASE_WINDOW, 0, line1);
            curs(BASE_WINDOW, 2, 22); cl_end();
            curs(BASE_WINDOW, 2, 22); tty_putstr(BASE_WINDOW, 0, line2);
            curs(BASE_WINDOW, 2, 23); cl_end();
            curs(BASE_WINDOW, 2, 23); tty_putstr(BASE_WINDOW, 0, line3);
            curs(BASE_WINDOW, 26, 19); /* the . of . - play! */
            ch = readchar();
            if (ch == 'q' || ch == 'Q') bail((char *)0);
            if (ch == '#') reroll = TRUE;
            if (ch == 'i' && scumcount == 0 && !solo)
                scumcount = 1;
            if (ch == '*') {
                flags.initrole = randrole();
                flags.initrace = randrace(flags.initrole);
                flags.initgend = randgend(flags.initrole, flags.initrace);
                flags.initalign = randalign(flags.initrole, flags.initrace);
                /* switch to blurb about the role selected, because the old
                   blurb is likely either wrong or misleading */
                line1 = roles[flags.initrole].intro1;
                line2 = roles[flags.initrole].intro2;
                line3 = roles[flags.initrole].intro3;
                reroll = TRUE;
            }
            p = player_selection_menu_options;
            while (p->x) {
                if (p->accel == ch && p->savein) {
                    *(p->savein) = p->convfunction(p->item);
                    reroll = TRUE;
                    /* This is not really code duplication, because the
                       "intro1", etc., fields correspond to different
                       memory addresses in the four structures */
                    if (p->validator == ok_role) {
                        line1 = roles[flags.initrole].intro1;
                        line2 = roles[flags.initrole].intro2;
                        line3 = roles[flags.initrole].intro3;
                    }
                    if (p->validator == ok_race) {
                        line1 = races[flags.initrace].intro1;
                        line2 = races[flags.initrace].intro2;
                        line3 = races[flags.initrace].intro3;
                    }
                    if (p->validator == ok_gend) {
                        line1 = genders[flags.initgend].intro1;
                        line2 = genders[flags.initgend].intro2;
                        line3 = genders[flags.initgend].intro3;
                    }
                    if (p->validator == ok_align) {
                        line1 = aligns[flags.initalign].intro1;
                        line2 = aligns[flags.initalign].intro2;
                        line3 = aligns[flags.initalign].intro3;
                    }
                    break;
                }
                p++;
            }
        } while(ch != '.' || !xallowed);

	/* Success! */
	tty_display_nhwindow(BASE_WINDOW, FALSE);
}


/*
 * plname is filled either by an option (-u Player  or	-uPlayer) or
 * explicitly (by being the wizard) or by askname.
 * It may still contain a suffix denoting the role, etc.
 * Always called after init_nhwindows() and before display_gamewindows().
 */
void
tty_askname()
{
    static char who_are_you[] = "Who are you? ";
    register int c, ct, tryct = 0;

    tty_putstr(BASE_WINDOW, 0, "");
    do {
	if (++tryct > 1) {
	    if (tryct > 10) bail("Giving up after 10 tries.\n");
	    tty_curs(BASE_WINDOW, 1, wins[BASE_WINDOW]->cury - 1);
	    tty_putstr(BASE_WINDOW, 0, "Enter a name for your character...");
	    /* erase previous prompt (in case of ESC after partial response) */
	    tty_curs(BASE_WINDOW, 1, wins[BASE_WINDOW]->cury),	cl_end();
	}
	tty_putstr(BASE_WINDOW, 0, who_are_you);
	tty_curs(BASE_WINDOW, (int)(sizeof who_are_you),
		 wins[BASE_WINDOW]->cury - 1);
	ct = 0;
	while((c = tty_nhgetch()) != '\n') {
		if(c == EOF) error("End of input\n");
		if (c == '\033') { ct = 0; break; }  /* continue outer loop */
#if defined(WIN32CON)
		if (c == '\003') bail("^C abort.\n");
#endif
		/* some people get confused when their erase char is not ^H */
		if (c == '\b' || c == '\177') {
			if(ct) {
				ct--;
#ifdef WIN32CON
				ttyDisplay->curx--;
#endif
#if defined(MICRO) || defined(WIN32CON)
# if defined(WIN32CON) || defined(MSDOS)
				backsp();	/* \b is visible on NT */
				(void) putchar(' ');
				backsp();
# else
				msmsg("\b \b");
# endif
#else
				(void) putchar('\b');
				(void) putchar(' ');
				(void) putchar('\b');
#endif
			}
			continue;
		}
#if defined(UNIX) || defined(VMS)
		if(c != '-' && c != '@')
		if(c < 'A' || (c > 'Z' && c < 'a') || c > 'z') c = '_';
#endif
		if (ct < (int)(sizeof plname) - 1) {
#if defined(MICRO)
# if defined(MSDOS)
			if (iflags.grmode) {
				(void) onechar(c);
			} else
# endif
			msmsg("%c", c);
#else
			(void) onechar(c);
#endif
			plname[ct++] = c;
#ifdef WIN32CON
			ttyDisplay->curx++;
#endif
		}
	}
	plname[ct] = 0;
    } while (ct == 0);

    /* move to next line to simulate echo of user's <return> */
    tty_curs(BASE_WINDOW, 1, wins[BASE_WINDOW]->cury + 1);
}

void
tty_get_nh_event()
{
    return;
}

#if !defined(MICRO) && !defined(WIN32CON)
STATIC_OVL void
getret()
{
	xputs("\n");
	if(flags.standout)
		standoutbeg();
	xputs("Hit ");
	xputs(iflags.cbreak ? "space" : "return");
	xputs(" to continue: ");
	if(flags.standout)
		standoutend();
	xwaitforspace(" ");
}
#endif

void
tty_suspend_nhwindows(str)
    const char *str;
{
    settty(str);		/* calls end_screen, perhaps raw_print */
    if (!str) tty_raw_print("");	/* calls fflush(stdout) */
}

void
tty_resume_nhwindows()
{
    gettty();
    setftty();			/* calls start_screen */
    docrt();
}

void
tty_exit_nhwindows(str)
    const char *str;
{
    winid i;

    tty_suspend_nhwindows(str);
    /* Just forget any windows existed, since we're about to exit anyway.
     * Disable windows to avoid calls to window routines.
     */
    for(i=0; i<MAXWIN; i++)
	if (wins[i] && (i != BASE_WINDOW)) {
#ifdef FREE_ALL_MEMORY
	    free_window_info(wins[i], TRUE);
	    free((genericptr_t) wins[i]);
#endif
	    wins[i] = 0;
	}
#ifndef NO_TERMS		/*(until this gets added to the window interface)*/
    tty_shutdown();		/* cleanup termcap/terminfo/whatever */
#endif
    iflags.window_inited = 0;
}

winid
tty_create_nhwindow(type)
    int type;
{
    struct WinDesc* newwin;
    int i;
    int newid;

    if(maxwin == MAXWIN)
	return WIN_ERR;

    newwin = (struct WinDesc*) alloc(sizeof(struct WinDesc));
    newwin->type = type;
    newwin->flags = 0;
    newwin->active = FALSE;
    newwin->curx = newwin->cury = 0;
    newwin->morestr = 0;
    newwin->mlist = (tty_menu_item *) 0;
    newwin->plist = (tty_menu_item **) 0;
    newwin->npages = newwin->plist_size = newwin->nitems = newwin->how = 0;
    switch(type) {
    case NHW_BASE:
	/* base window, used for absolute movement on the screen */
	newwin->offx = newwin->offy = 0;
	newwin->rows = ttyDisplay->rows;
	newwin->cols = ttyDisplay->cols;
	newwin->maxrow = newwin->maxcol = 0;
	break;
    case NHW_MESSAGE:
	/* message window, 1 line long, very wide, top of screen */
	newwin->offx = newwin->offy = 0;
	/* sanity check */
	if(iflags.msg_history < 20) iflags.msg_history = 20;
	else if(iflags.msg_history > 32760) iflags.msg_history = 32760;
	newwin->maxrow = newwin->rows = iflags.msg_history;
	newwin->maxcol = newwin->cols = 0;
	break;
    case NHW_STATUS:
	/* status window, 2 lines long, full width, bottom of screen */
	newwin->offx = 0;
#if defined(USE_TILES) && defined(MSDOS)
	if (iflags.grmode) {
		newwin->offy = ttyDisplay->rows-2;
	} else
#endif
	newwin->offy = min((int)ttyDisplay->rows-2, ROWNO+1);
	newwin->rows = newwin->maxrow = 2;
	newwin->cols = newwin->maxcol = min(ttyDisplay->cols, COLNO);
	break;
    case NHW_MAP:
	/* map window, ROWNO lines long, full width, below message window */
	newwin->offx = 0;
	newwin->offy = 1;
	newwin->rows = ROWNO;
	newwin->cols = COLNO;
	newwin->maxrow = 0;	/* no buffering done -- let gbuf do it */
	newwin->maxcol = 0;
	break;
    case NHW_MENU:
    case NHW_TEXT:
	/* inventory/menu window, variable length, full width, top of screen */
	/* help window, the same, different semantics for display, etc */
	newwin->offx = newwin->offy = 0;
	newwin->rows = 0;
	newwin->cols = ttyDisplay->cols;
	newwin->maxrow = newwin->maxcol = 0;
	break;
   default:
	panic("Tried to create window type %d\n", (int) type);
	return WIN_ERR;
    }

    for(newid = 0; newid<MAXWIN; newid++) {
	if(wins[newid] == 0) {
	    wins[newid] = newwin;
	    break;
	}
    }
    if(newid == MAXWIN) {
	panic("No window slots!");
	return WIN_ERR;
    }

    if(newwin->maxrow) {
	newwin->data =
		(char **) alloc(sizeof(char *) * (unsigned)newwin->maxrow);
	newwin->datlen =
		(short *) alloc(sizeof(short) * (unsigned)newwin->maxrow);
	if(newwin->maxcol) {
	    for (i = 0; i < newwin->maxrow; i++) {
		newwin->data[i] = (char *) alloc((unsigned)newwin->maxcol);
		newwin->datlen[i] = newwin->maxcol;
	    }
	} else {
	    for (i = 0; i < newwin->maxrow; i++) {
		newwin->data[i] = (char *) 0;
		newwin->datlen[i] = 0;
	    }
	}
	if(newwin->type == NHW_MESSAGE)
	    newwin->maxrow = 0;
    } else {
	newwin->data = (char **)0;
	newwin->datlen = (short *)0;
    }

    return newid;
}

STATIC_OVL void
erase_menu_or_text(window, cw, clear)
    winid window;
    struct WinDesc *cw;
    boolean clear;
{
    if(cw->offx == 0)
	if(cw->offy) {
	    tty_curs(window, 1, 0);
	    cl_eos();
	} else if (clear)
	    clear_screen();
	else
	    docrt();
    else
	docorner((int)cw->offx, cw->maxrow+1);
}

STATIC_OVL void
free_window_info(cw, free_data)
    struct WinDesc *cw;
    boolean free_data;
{
    int i;

    if (cw->data) {
	if (cw == wins[WIN_MESSAGE] && cw->rows > cw->maxrow)
	    cw->maxrow = cw->rows;		/* topl data */
	for(i=0; i<cw->maxrow; i++)
	    if(cw->data[i]) {
		free((genericptr_t)cw->data[i]);
		cw->data[i] = (char *)0;
		if (cw->datlen) cw->datlen[i] = 0;
	    }
	if (free_data) {
	    free((genericptr_t)cw->data);
	    cw->data = (char **)0;
	    if (cw->datlen) free((genericptr_t)cw->datlen);
	    cw->datlen = (short *)0;
	    cw->rows = 0;
	}
    }
    cw->maxrow = cw->maxcol = 0;
    if(cw->mlist) {
	tty_menu_item *temp;
	while ((temp = cw->mlist) != 0) {
	    cw->mlist = cw->mlist->next;
	    if (temp->str) free((genericptr_t)temp->str);
	    free((genericptr_t)temp);
	}
    }
    if (cw->plist) {
	free((genericptr_t)cw->plist);
	cw->plist = 0;
    }
    cw->plist_size = cw->npages = cw->nitems = cw->how = 0;
    if(cw->morestr) {
	free((genericptr_t)cw->morestr);
	cw->morestr = 0;
    }
}

void
tty_clear_nhwindow(window)
    winid window;
{
    register struct WinDesc *cw = 0;

    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0)
	panic(winpanicstr,  window);
    ttyDisplay->lastwin = window;

    switch(cw->type) {
    case NHW_MESSAGE:
	if(ttyDisplay->toplin) {
	    home();
	    cl_end();
	    if(cw->cury)
		docorner(1, cw->cury+1);
	    ttyDisplay->toplin = 0;
	}
        fade_topl();
	break;
    case NHW_STATUS:
	tty_curs(window, 1, 0);
	cl_end();
	tty_curs(window, 1, 1);
	cl_end();
	break;
    case NHW_MAP:
	/* cheap -- clear the whole thing and tell nethack to redraw botl */
	flags.botlx = 1;
	/* fall into ... */
    case NHW_BASE:
        onechar(0);
	clear_screen();
	break;
    case NHW_MENU:
    case NHW_TEXT:
	if(cw->active)
	    erase_menu_or_text(window, cw, TRUE);
	free_window_info(cw, FALSE);
	break;
    }
    cw->curx = cw->cury = 0;
}

STATIC_OVL void
dmore(cw, s)
    register struct WinDesc *cw;
    const char *s;			/* valid responses */
{
    const char *prompt = cw->morestr ? cw->morestr : defmorestr;
    int offset = (cw->type == NHW_TEXT) ? 1 : 2;

    tty_curs(BASE_WINDOW,
	     (int)ttyDisplay->curx + offset, (int)ttyDisplay->cury);

    ttyDisplay->curx += strlen(prompt);
    tty_start_attr(ATR_INVERSE);
    while (*prompt) onechar(*prompt++);
    tty_end_attr(ATR_INVERSE);

    xwaitforspace(s);
}

STATIC_OVL void
set_item_state(window, lineno, item)
    winid window;
    int lineno;
    tty_menu_item *item;
{
    char ch = item->selected ? (item->count == -1L ? '+' : '#') : '-';
    tty_curs(window, 4, lineno);
    tty_start_attr(item->attr);
    tty_start_color(item->color);
    (void) onechar(ch);
    ttyDisplay->curx++;
    tty_end_color();
    tty_end_attr(item->attr);
}

STATIC_OVL void
set_all_on_page(window, page_start, page_end)
    winid window;
    tty_menu_item *page_start, *page_end;
{
    tty_menu_item *curr;
    int n;

    for (n = 0, curr = page_start; curr != page_end; n++, curr = curr->next)
	if (curr->identifier.a_void && !curr->selected) {
	    curr->selected = TRUE;
	    set_item_state(window, n, curr);
	}
}

STATIC_OVL void
unset_all_on_page(window, page_start, page_end)
    winid window;
    tty_menu_item *page_start, *page_end;
{
    tty_menu_item *curr;
    int n;

    for (n = 0, curr = page_start; curr != page_end; n++, curr = curr->next)
	if (curr->identifier.a_void && curr->selected) {
	    curr->selected = FALSE;
	    curr->count = -1L;
	    set_item_state(window, n, curr);
	}
}

STATIC_OVL void
invert_all_on_page(window, page_start, page_end, acc)
    winid window;
    tty_menu_item *page_start, *page_end;
    char acc;	/* group accelerator, 0 => all */
{
    tty_menu_item *curr;
    int n;

    for (n = 0, curr = page_start; curr != page_end; n++, curr = curr->next)
	if (curr->identifier.a_void && (acc == 0 || curr->gselector == acc)) {
	    if (curr->selected) {
		curr->selected = FALSE;
		curr->count = -1L;
	    } else
		curr->selected = TRUE;
	    set_item_state(window, n, curr);
	}
}

/*
 * Invert all entries that match the give group accelerator (or all if
 * zero).
 */
STATIC_OVL void
invert_all(window, page_start, page_end, acc)
    winid window;
    tty_menu_item *page_start, *page_end;
    char acc;	/* group accelerator, 0 => all */
{
    tty_menu_item *curr;
    boolean on_curr_page;
    struct WinDesc *cw =  wins[window];

    invert_all_on_page(window, page_start, page_end, acc);

    /* invert the rest */
    for (on_curr_page = FALSE, curr = cw->mlist; curr; curr = curr->next) {
	if (curr == page_start)
	    on_curr_page = TRUE;
	else if (curr == page_end)
	    on_curr_page = FALSE;

	if (!on_curr_page && curr->identifier.a_void
				&& (acc == 0 || curr->gselector == acc)) {
	    if (curr->selected) {
		curr->selected = FALSE;
		curr->count = -1;
	    } else
		curr->selected = TRUE;
	}
    }
}

STATIC_OVL void
process_menu_window(window, cw)
winid window;
struct WinDesc *cw;
{
    tty_menu_item *page_start, *page_end, *curr;
    long count;
    int n, curr_page, page_lines;
    boolean finished, counting, reset_count;
    char *cp, *rp, resp[QBUFSZ], gacc[QBUFSZ],
	 *msave, *morestr;

    curr_page = page_lines = 0;
    page_start = page_end = 0;
    msave = cw->morestr;	/* save the morestr */
    cw->morestr = morestr = (char*) alloc((unsigned) QBUFSZ);
    counting = FALSE;
    count = 0L;
    reset_count = TRUE;
    finished = FALSE;

    /* collect group accelerators; for PICK_NONE, they're ignored;
       for PICK_ONE, only those which match exactly one entry will be
       accepted; for PICK_ANY, those which match any entry are okay */
    gacc[0] = '\0';
    if (cw->how != PICK_NONE) {
	int i, gcnt[128];
#define GSELIDX(c) (c & 127)	/* guard against `signed char' */

	for (i = 0; i < SIZE(gcnt); i++) gcnt[i] = 0;
	for (n = 0, curr = cw->mlist; curr; curr = curr->next)
	    if (curr->gselector && curr->gselector != curr->selector) {
		++n;
		++gcnt[GSELIDX(curr->gselector)];
	    }

	if (n > 0)	/* at least one group accelerator found */
	    for (rp = gacc, curr = cw->mlist; curr; curr = curr->next)
		if (curr->gselector && !index(gacc, curr->gselector) &&
			(cw->how == PICK_ANY ||
			    gcnt[GSELIDX(curr->gselector)] == 1)) {
		    *rp++ = curr->gselector;
		    *rp = '\0';	/* re-terminate for index() */
		}
    }

    /* loop until finished */
    while (!finished) {
	if (reset_count) {
	    counting = FALSE;
	    count = 0;
	} else
	    reset_count = TRUE;

	if (!page_start) {
	    /* new page to be displayed */
	    if (curr_page < 0 || (cw->npages > 0 && curr_page >= cw->npages))
		panic("bad menu screen page #%d", curr_page);

	    /* clear screen */
	    if (!cw->offx) {	/* if not corner, do clearscreen */
		if(cw->offy) {
		    tty_curs(window, 1, 0);
		    cl_eos();
		} else
		    clear_screen();
	    }

	    rp = resp;
	    if (cw->npages > 0) {
		/* collect accelerators */
		page_start = cw->plist[curr_page];
		page_end = cw->plist[curr_page + 1];
		for (page_lines = 0, curr = page_start;
			curr != page_end;
			page_lines++, curr = curr->next) {
		    if (curr->selector)
			*rp++ = curr->selector;

		    tty_curs(window, 1, page_lines);
		    if (cw->offx) cl_end();

		    (void) onechar(' ');
		    ++ttyDisplay->curx;
		    /*
		     * Don't use xputs() because (1) under unix it calls
		     * tputstr() which will interpret a '*' as some kind
		     * of padding information and (2) it calls xputc to
		     * actually output the character.  We're faster doing
		     * this.
		     */
#ifdef MENU_COLOR
                    if (iflags.use_menu_color) {
                        get_menu_coloring(curr->str, &curr->color, &curr->attr);
		        tty_start_attr(curr->attr);
                        tty_start_color(curr->color);
                    } else
#endif
                        tty_start_attr(curr->attr);
		    for (n = 0, cp = curr->str;
#ifndef WIN32CON
			  *cp && (int) ++ttyDisplay->curx < (int) ttyDisplay->cols;
			  cp++, n++)
#else
			  *cp && (int) ttyDisplay->curx < (int) ttyDisplay->cols;
			  cp++, n++, ttyDisplay->curx++)
#endif
			if (n == 2 && curr->identifier.a_void != 0 &&
							curr->selected) {
			    if (curr->count == -1L)
				(void) onechar('+'); /* all selected */
			    else
				(void) onechar('#'); /* count selected */
			} else
			    (void) onechar(*cp);
#ifdef MENU_COLOR
                    tty_end_color();
#endif
                    tty_end_attr(curr->attr);
		}
	    } else {
		page_start = 0;
		page_end = 0;
		page_lines = 0;
	    }
	    *rp = 0;

	    /* corner window - clear extra lines from last page */
	    if (cw->offx) {
		for (n = page_lines + 1; n < cw->maxrow; n++) {
		    tty_curs(window, 1, n);
		    cl_end();
		}
	    }

	    /* set extra chars.. */
	    Strcat(resp, default_menu_cmds);
	    Strcat(resp, "0123456789\033\n\r");	/* counts, quit */
	    Strcat(resp, gacc);			/* group accelerators */
	    Strcat(resp, mapped_menu_cmds);

	    if (cw->npages > 1)
		Sprintf(cw->morestr, "(%d of %d)",
			curr_page + 1, (int) cw->npages);
	    else if (msave)
		Strcpy(cw->morestr, msave);
	    else
		Strcpy(cw->morestr, defmorestr);

	    tty_curs(window, 1, page_lines);
	    cl_end();
	    dmore(cw, resp);
	} else {
	    /* just put the cursor back... */
	    tty_curs(window, (int) strlen(cw->morestr) + 2, page_lines);
	    xwaitforspace(resp);
	}

	morc = map_menu_cmd(morc);
	switch (morc) {
	    case '0':
		/* special case: '0' is also the default ball class */
		if (!counting && index(gacc, morc)) goto group_accel;
		/* fall through to count the zero */
	    case '1': case '2': case '3': case '4':
	    case '5': case '6': case '7': case '8': case '9':
		count = (count * 10L) + (long) (morc - '0');
		/*
		 * It is debatable whether we should allow 0 to
		 * start a count.  There is no difference if the
		 * item is selected.  If not selected, then
		 * "0b" could mean:
		 *
		 *	count starting zero:	"zero b's"
		 *	ignore starting zero:	"select b"
		 *
		 * At present I don't know which is better.
		 */
		if (count != 0L) {	/* ignore leading zeros */
		    counting = TRUE;
		    reset_count = FALSE;
		}
		break;
	    case '\033':	/* cancel - from counting or loop */
		if (!counting) {
		    /* deselect everything */
		    for (curr = cw->mlist; curr; curr = curr->next) {
			curr->selected = FALSE;
			curr->count = -1L;
		    }
		    cw->flags |= WIN_CANCELLED;
		    finished = TRUE;
		}
		/* else only stop count */
		break;
	    case '\0':		/* finished (commit) */
	    case '\n':
	    case '\r':
		/* only finished if we are actually picking something */
		if (cw->how != PICK_NONE) {
		    finished = TRUE;
		    break;
		}
		/* else fall through */
	    case MENU_NEXT_PAGE:
		if (cw->npages > 0 && curr_page != cw->npages - 1) {
		    curr_page++;
		    page_start = 0;
		} else
		    finished = TRUE;	/* questionable behavior */
		break;
	    case MENU_PREVIOUS_PAGE:
		if (cw->npages > 0 && curr_page != 0) {
		    --curr_page;
		    page_start = 0;
		}
		break;
	    case MENU_FIRST_PAGE:
		if (cw->npages > 0 && curr_page != 0) {
		    page_start = 0;
		    curr_page = 0;
		}
		break;
	    case MENU_LAST_PAGE:
		if (cw->npages > 0 && curr_page != cw->npages - 1) {
		    page_start = 0;
		    curr_page = cw->npages - 1;
		}
		break;
	    case MENU_SELECT_PAGE:
		if (cw->how == PICK_ANY)
		    set_all_on_page(window, page_start, page_end);
		break;
	    case MENU_UNSELECT_PAGE:
		unset_all_on_page(window, page_start, page_end);
		break;
	    case MENU_INVERT_PAGE:
		if (cw->how == PICK_ANY)
		    invert_all_on_page(window, page_start, page_end, 0);
		break;
	    case MENU_SELECT_ALL:
		if (cw->how == PICK_ANY) {
		    set_all_on_page(window, page_start, page_end);
		    /* set the rest */
		    for (curr = cw->mlist; curr; curr = curr->next)
			if (curr->identifier.a_void && !curr->selected)
			    curr->selected = TRUE;
		}
		break;
	    case MENU_UNSELECT_ALL:
		unset_all_on_page(window, page_start, page_end);
		/* unset the rest */
		for (curr = cw->mlist; curr; curr = curr->next)
		    if (curr->identifier.a_void && curr->selected) {
			curr->selected = FALSE;
			curr->count = -1;
		    }
		break;
	    case MENU_INVERT_ALL:
		if (cw->how == PICK_ANY)
		    invert_all(window, page_start, page_end, 0);
		break;
	    default:
		if (cw->how == PICK_NONE || !index(resp, morc)) {
		    /* unacceptable input received */
		    tty_nhbell();
		    break;
		} else if (index(gacc, morc)) {
 group_accel:
		    /* group accelerator; for the PICK_ONE case, we know that
		       it matches exactly one item in order to be in gacc[] */
		    invert_all(window, page_start, page_end, morc);
		    if (cw->how == PICK_ONE) finished = TRUE;
		    break;
		}
		/* find, toggle, and possibly update */
		for (n = 0, curr = page_start;
			curr != page_end;
			n++, curr = curr->next)
		    if (morc == curr->selector) {
			if (curr->selected) {
			    if (counting && count > 0) {
				curr->count = count;
				set_item_state(window, n, curr);
			    } else { /* change state */
				curr->selected = FALSE;
				curr->count = -1L;
				set_item_state(window, n, curr);
			    }
			} else {	/* !selected */
			    if (counting && count > 0) {
				curr->count = count;
				curr->selected = TRUE;
				set_item_state(window, n, curr);
			    } else if (!counting) {
				curr->selected = TRUE;
				set_item_state(window, n, curr);
			    }
			    /* do nothing counting&&count==0 */
			}

			if (cw->how == PICK_ONE) finished = TRUE;
			break;	/* from `for' loop */
		    }
		break;
	}

    } /* while */
    cw->morestr = msave;
    free((genericptr_t)morestr);
}

STATIC_OVL void
process_text_window(window, cw)
winid window;
struct WinDesc *cw;
{
    int i, n, attr, color;
    register char *cp;

    for (n = 0, i = 0; i < cw->maxrow; i++) {
	if (!cw->offx && (n + cw->offy == ttyDisplay->rows - 1)) {
	    tty_curs(window, 1, n);
	    cl_end();
	    dmore(cw, quitchars);
	    if (morc == '\033') {
		cw->flags |= WIN_CANCELLED;
		break;
	    }
	    if (cw->offy) {
		tty_curs(window, 1, 0);
		cl_eos();
	    } else
		clear_screen();
	    n = 0;
	}
	tty_curs(window, 1, n++);
	if (cw->offx) cl_end();
	if (cw->data[i]) {
	    color = cw->data[i][0] - 1;
	    attr = cw->data[i][1] - 1;
	    if (cw->offx) {
		(void) onechar(' '); ++ttyDisplay->curx;
	    }
	    tty_start_attr(attr);
            tty_start_color(color);
	    for (cp = &cw->data[i][2];
#ifndef WIN32CON
		    *cp && (int) ++ttyDisplay->curx < (int) ttyDisplay->cols;
		    cp++)
#else
		    *cp && (int) ttyDisplay->curx < (int) ttyDisplay->cols;
		    cp++, ttyDisplay->curx++)
#endif
		(void) onechar(*cp);
            tty_end_color();
	    tty_end_attr(attr);
	}
    }
    if (i == cw->maxrow) {
	tty_curs(BASE_WINDOW, (int)cw->offx + 1,
		 (cw->type == NHW_TEXT) ? (int) ttyDisplay->rows - 1 : n);
	cl_end();
	dmore(cw, quitchars);
	if (morc == '\033')
	    cw->flags |= WIN_CANCELLED;
    }
}

/*ARGSUSED*/
void
tty_display_nhwindow(window, blocking)
    winid window;
    boolean blocking;	/* with ttys, all windows are blocking */
{
    register struct WinDesc *cw = 0;

    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0)
	panic(winpanicstr,  window);
    if(cw->flags & WIN_CANCELLED)
	return;
    ttyDisplay->lastwin = window;
    ttyDisplay->rawprint = 0;

    switch(cw->type) {
    case NHW_MESSAGE:
	if(ttyDisplay->toplin == 1) {
	    more();
	    ttyDisplay->toplin = 1; /* more resets this */
	    tty_clear_nhwindow(window);
	} else {
	    ttyDisplay->toplin = 0;
            fade_topl(); /* this can be necessary during a screen redraw */
        }
	cw->curx = cw->cury = 0;
	if(!cw->active)
	    iflags.window_inited = TRUE;
	break;
    case NHW_MAP:
	end_glyphout();
	if(blocking) {
	    if(!ttyDisplay->toplin) ttyDisplay->toplin = 1;
	    tty_display_nhwindow(WIN_MESSAGE, TRUE);
	    return;
	}
    case NHW_BASE:
	(void) fflush(stdout);
	break;
    case NHW_TEXT:
	cw->maxcol = ttyDisplay->cols; /* force full-screen mode */
	/*FALLTHRU*/
    case NHW_MENU:
	cw->active = 1;
	/* avoid converting to uchar before calculations are finished */
	cw->offx = (uchar) (int)
	    max((int) 10, (int) (ttyDisplay->cols - cw->maxcol - 1));
	if(cw->type == NHW_MENU)
	    cw->offy = 0;
	if(ttyDisplay->toplin == 1)
	    tty_display_nhwindow(WIN_MESSAGE, TRUE);
	if(cw->offx == 10 || cw->maxrow >= (int) ttyDisplay->rows) {
	    cw->offx = 0;
	    if(cw->offy) {
		tty_curs(window, 1, 0);
		cl_eos();
	    } else
		clear_screen();
	    ttyDisplay->toplin = 0;
	} else
	    tty_clear_nhwindow(WIN_MESSAGE);

	if (cw->data || !cw->maxrow)
	    process_text_window(window, cw);
	else
	    process_menu_window(window, cw);
	break;
    }
    cw->active = 1;
}

void
tty_dismiss_nhwindow(window)
    winid window;
{
    register struct WinDesc *cw = 0;

    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0)
	panic(winpanicstr,  window);

    switch(cw->type) {
    case NHW_MESSAGE:
	if (ttyDisplay->toplin)
	    tty_display_nhwindow(WIN_MESSAGE, TRUE);
	/*FALLTHRU*/
    case NHW_STATUS:
    case NHW_BASE:
    case NHW_MAP:
	/*
	 * these should only get dismissed when the game is going away
	 * or suspending
	 */
	tty_curs(BASE_WINDOW, 1, (int)ttyDisplay->rows-1);
	cw->active = 0;
	break;
    case NHW_MENU:
    case NHW_TEXT:
	if(cw->active) {
	    if (iflags.window_inited) {
		/* otherwise dismissing the text endwin after other windows
		 * are dismissed tries to redraw the map and panics.  since
		 * the whole reason for dismissing the other windows was to
		 * leave the ending window on the screen, we don't want to
		 * erase it anyway.
		 */
		erase_menu_or_text(window, cw, FALSE);
	    }
	    cw->active = 0;
	}
	break;
    }
    cw->flags = 0;
}

void
tty_destroy_nhwindow(window)
    winid window;
{
    register struct WinDesc *cw = 0;

    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0)
	panic(winpanicstr,  window);

    if(cw->active)
	tty_dismiss_nhwindow(window);
    if(cw->type == NHW_MESSAGE)
	iflags.window_inited = 0;
    if(cw->type == NHW_MAP)
	clear_screen();

    free_window_info(cw, TRUE);
    free((genericptr_t)cw);
    wins[window] = 0;
}

void
tty_curs(window, x, y)
winid window;
register int x, y;	/* not xchar: perhaps xchar is unsigned and
			   curx-x would be unsigned as well */
{
    struct WinDesc *cw = 0;
    int cx = ttyDisplay->curx;
    int cy = ttyDisplay->cury;

    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0)
	panic(winpanicstr,  window);
    ttyDisplay->lastwin = window;

#if defined(USE_TILES) && defined(MSDOS)
    adjust_cursor_flags(cw);
#endif
    cw->curx = --x;	/* column 0 is never used */
    cw->cury = y;
#ifdef DEBUG
    if(x<0 || y<0 || y >= cw->rows || x > cw->cols) {
	const char *s = "[unknown type]";
	switch(cw->type) {
	case NHW_MESSAGE: s = "[topl window]"; break;
	case NHW_STATUS: s = "[status window]"; break;
	case NHW_MAP: s = "[map window]"; break;
	case NHW_MENU: s = "[corner window]"; break;
	case NHW_TEXT: s = "[text window]"; break;
	case NHW_BASE: s = "[base window]"; break;
	}
	impossible("bad curs positioning win %d %s (%d,%d)", window, s, x, y);
	return;
    }
#endif
    x += cw->offx;
    y += cw->offy;

#ifdef CLIPPING
    if(clipping && window == WIN_MAP) {
	x -= clipx;
	y -= clipy;
    }
#endif

    if (y == cy && x == cx)
	return;

    if(cw->type == NHW_MAP)
	end_glyphout();

#ifndef NO_TERMS
    if(!nh_ND && (cx != x || x <= 3)) { /* Extremely primitive */
	cmov(x, y); /* bunker!wtm */
	return;
    }
#endif

    if((cy -= y) < 0) cy = -cy;
    if((cx -= x) < 0) cx = -cx;
    if(cy <= 3 && cx <= 3) {
	nocmov(x, y);
#ifndef NO_TERMS
    } else if ((x <= 3 && cy <= 3) || (!nh_CM && x < cx)) {
	(void) onechar('\r');
	ttyDisplay->curx = 0;
	nocmov(x, y);
    } else if (!nh_CM) {
	nocmov(x, y);
#endif
    } else
	cmov(x, y);

    ttyDisplay->curx = x;
    ttyDisplay->cury = y;
}

STATIC_OVL void
tty_putsym(window, x, y, ch)
    winid window;
    int x, y;
    char ch;
{
    register struct WinDesc *cw = 0;

    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0)
	panic(winpanicstr,  window);

    switch(cw->type) {
    case NHW_STATUS:
    case NHW_MAP:
    case NHW_BASE:
	tty_curs(window, x, y);
	(void) onechar(ch);
	ttyDisplay->curx++;
	cw->curx++;
	break;
    case NHW_MESSAGE:
    case NHW_MENU:
    case NHW_TEXT:
	impossible("Can't putsym to window type %d", cw->type);
	break;
    }
}


STATIC_OVL const char*
compress_str(str)
const char *str;
{
	static char cbuf[BUFSZ];
	/* compress in case line too long */
	if((int)strlen(str) >= CO) {
		register const char *bp0 = str;
		register char *bp1 = cbuf;

		do {
#ifdef CLIPPING
			if(*bp0 != ' ' || bp0[1] != ' ')
#else
			if(*bp0 != ' ' || bp0[1] != ' ' || bp0[2] != ' ')
#endif
				*bp1++ = *bp0;
		} while(*bp0++);
	} else
	    return str;
	return cbuf;
}

/* Semantics: prevents --More-- being shown after the current message,
   if any, but does not prevent it after future messages. */
void
tty_suppress_more()
{
    if (ttyDisplay->toplin == 1) ttyDisplay->toplin = 2;
}

void
tty_putstr(window, attr, str)
    winid window;
    int attr;
    const char *str;
{
    tty_putstr_colored(window, attr, NO_COLOR, str);
}

void
tty_putstr_colored(window, attr, color, str)
    winid window;
    int attr;
    int color;
    const char *str;
{
    register struct WinDesc *cw = 0;
    register char *ob;
    register const char *nb;
    register int i, j, n0;

    /* Assume there's a real problem if the window is missing --
     * probably a panic message
     */
    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0) {
	tty_raw_print(str);
	return;
    }

    if(str == (const char*)0 ||
	((cw->flags & WIN_CANCELLED) && (cw->type != NHW_MESSAGE)))
	return;
    if(cw->type != NHW_MESSAGE)
	str = compress_str(str);

    ttyDisplay->lastwin = window;

    switch(cw->type) {
    case NHW_MESSAGE:
	/* really do this later */
#if defined(USER_SOUNDS) && defined(WIN32CON)
	play_sound_for_message(str);
#endif
	update_topl(str);
	break;

    case NHW_STATUS:
        tty_start_color(color);
        tty_start_attr(attr);
	ob = &cw->data[cw->cury][j = cw->curx];
	if(flags.botlx) *ob = 0;
	if(!cw->cury && (int)strlen(str) >= CO) {
	    /* the characters before "St:" are unnecessary */
	    nb = index(str, ':');
	    if(nb && nb > str+2)
		str = nb - 2;
	}
	nb = str;
	for(i = cw->curx+1, n0 = cw->cols; i < n0; i++, nb++) {
	    if(!*nb) {
		if(*ob || flags.botlx) {
		    /* last char printed may be in middle of line */
		    tty_curs(WIN_STATUS, i, cw->cury);
		    cl_end();
		}
		break;
	    }
            tty_putsym(WIN_STATUS, i, cw->cury, *nb);
	    if(*ob) ob++;
	}

	(void) strncpy(&cw->data[cw->cury][j], str, cw->cols - j - 1);
	cw->data[cw->cury][cw->cols-1] = '\0'; /* null terminate */
	cw->cury = (cw->cury+1) % 2;
	cw->curx = 0;
        tty_end_color();
        tty_end_attr(attr);
	break;
    case NHW_MAP:
	tty_curs(window, cw->curx+1, cw->cury);
	tty_start_attr(attr);
	while(*str && (int) ttyDisplay->curx < (int) ttyDisplay->cols-1) {
	    (void) onechar(*str);
	    str++;
	    ttyDisplay->curx++;
	}
	cw->curx = 0;
	cw->cury++;
	tty_end_attr(attr);
	break;
    case NHW_BASE:
	tty_curs(window, cw->curx+1, cw->cury);
	tty_start_attr(attr);
        tty_start_color(color);
	while (*str) {
	    if ((int) ttyDisplay->curx >= (int) ttyDisplay->cols-1) {
		cw->curx = 0;
		cw->cury++;
		tty_curs(window, cw->curx+1, cw->cury);
	    }
	    (void) onechar(*str);
	    str++;
	    ttyDisplay->curx++;
	}
	cw->curx = 0;
	cw->cury++;
        tty_end_color();
	tty_end_attr(attr);
	break;
    case NHW_MENU:
    case NHW_TEXT:
	if(cw->type == NHW_TEXT && cw->cury == ttyDisplay->rows-1) {
	    /* not a menu, so save memory and output 1 page at a time */
	    cw->maxcol = ttyDisplay->cols; /* force full-screen mode */
	    tty_display_nhwindow(window, TRUE);
	    for(i=0; i<cw->maxrow; i++)
		if(cw->data[i]){
		    free((genericptr_t)cw->data[i]);
		    cw->data[i] = 0;
		}
	    cw->maxrow = cw->cury = 0;
	}
	/* always grows one at a time, but alloc 12 at a time */
	if(cw->cury >= cw->rows) {
	    char **tmp;

	    cw->rows += 12;
	    tmp = (char **) alloc(sizeof(char *) * (unsigned)cw->rows);
	    for(i=0; i<cw->maxrow; i++)
		tmp[i] = cw->data[i];
	    if(cw->data)
		free((genericptr_t)cw->data);
	    cw->data = tmp;

	    for(i=cw->maxrow; i<cw->rows; i++)
		cw->data[i] = 0;
	}
	if(cw->data[cw->cury])
	    free((genericptr_t)cw->data[cw->cury]);
	n0 = strlen(str) + 2;
	ob = cw->data[cw->cury] = (char *)alloc((unsigned)n0 + 1);
	*ob++ = (char)(color + 1);	/* avoid nuls, for convenience */
	*ob++ = (char)(attr + 1);	/* avoid nuls, for convenience */
	Strcpy(ob, str);

	if(n0 > cw->maxcol)
	    cw->maxcol = n0;
	if(++cw->cury > cw->maxrow)
	    cw->maxrow = cw->cury;
	if(n0 > CO) {
	    /* attempt to break the line */
	    for(i = CO-1; i && str[i] != ' ' && str[i] != '\n';)
		i--;
	    if(i) {
		cw->data[cw->cury-1][++i] = '\0';
		tty_putstr(window, attr, &str[i]);
	    }

	}
	break;
    }
}

void
tty_display_file(fname, complain)
const char *fname;
boolean complain;
{
#ifdef DEF_PAGER			/* this implies that UNIX is defined */
    {
	/* use external pager; this may give security problems */
	register int fd = open(fname, 0);

	if(fd < 0) {
	    if(complain) pline("Cannot open %s.", fname);
	    else docrt();
	    return;
	}
	if(child(1)) {
	    /* Now that child() does a setuid(getuid()) and a chdir(),
	       we may not be able to open file fname anymore, so make
	       it stdin. */
	    (void) close(0);
	    if(dup(fd)) {
		if(complain) raw_printf("Cannot open %s as stdin.", fname);
	    } else {
		(void) execlp(catmore, "page", (char *)0);
		if(complain) raw_printf("Cannot exec %s.", catmore);
	    }
	    if(complain) sleep(10); /* want to wait_synch() but stdin is gone */
	    terminate(EXIT_FAILURE);
	}
	(void) close(fd);
    }
#else	/* DEF_PAGER */
    {
	dlb *f;
	char buf[BUFSZ];
	char *cr;

	tty_clear_nhwindow(WIN_MESSAGE);
	f = dlb_fopen(fname, "r");
	if (!f) {
	    if(complain) {
		home();	 tty_mark_synch();  tty_raw_print("");
		perror(fname);	tty_wait_synch();
		pline("Cannot open \"%s\".", fname);
	    } else if(u.ux) docrt();
	} else {
	    winid datawin = tty_create_nhwindow(NHW_TEXT);
	    boolean empty = TRUE;

	    if(complain
#ifndef NO_TERMS
		&& nh_CD
#endif
	    ) {
		wins[datawin]->offy = 0;
	    }
	    while (dlb_fgets(buf, BUFSZ, f)) {
		if ((cr = index(buf, '\n')) != 0) *cr = 0;
#ifdef MSDOS
		if ((cr = index(buf, '\r')) != 0) *cr = 0;
#endif
		if (index(buf, '\t') != 0) (void) tabexpand(buf);
		empty = FALSE;
		tty_putstr(datawin, 0, buf);
		if(wins[datawin]->flags & WIN_CANCELLED)
		    break;
	    }
	    if (!empty) tty_display_nhwindow(datawin, FALSE);
	    tty_destroy_nhwindow(datawin);
	    (void) dlb_fclose(f);
	}
    }
#endif /* DEF_PAGER */
}

void
tty_start_menu(window)
    winid window;
{
    tty_clear_nhwindow(window);
    return;
}

/*ARGSUSED*/
/*
 * Add a menu item to the beginning of the menu list.  This list is reversed
 * later.
 */
void
tty_add_menu_colored(window, glyph, identifier, ch, gch, attr, color, str, preselected)
    winid window;	/* window to use, must be of type NHW_MENU */
    int glyph;		/* glyph to display with item (unused) */
    const anything *identifier;	/* what to return if selected */
    char ch;		/* keyboard accelerator (0 = pick our own) */
    char gch;		/* group accelerator (0 = no group) */
    int attr;		/* attribute for string (like tty_putstr()) */
    int color;          /* default color if menucolors doesn't override it */
    const char *str;	/* menu string */
    boolean preselected; /* item is marked as selected */
{
    register struct WinDesc *cw = 0;
    tty_menu_item *item;
    const char *newstr;
    char buf[4+BUFSZ];

    if (str == (const char*) 0)
	return;

    if (window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0
		|| cw->type != NHW_MENU)
	panic(winpanicstr,  window);

    cw->nitems++;
    if (identifier->a_void) {
	int len = strlen(str);
	if (len >= BUFSZ) {
	    /* We *think* everything's coming in off at most BUFSZ bufs... */
	    impossible("Menu item too long (%d).", len);
	    len = BUFSZ - 1;
	}
	Sprintf(buf, "%c - ", ch ? ch : '?');
	(void) strncpy(buf+4, str, len);
	buf[4+len] = '\0';
	newstr = buf;
    } else
	newstr = str;

    item = (tty_menu_item *) alloc(sizeof(tty_menu_item));
    item->identifier = *identifier;
    item->count = -1L;
    item->selected = preselected;
    item->selector = ch;
    item->gselector = gch;
    item->attr = attr;
    item->color = color;
    item->str = copy_of(newstr);

    item->next = cw->mlist;
    cw->mlist = item;
}

void
tty_add_menu(window, glyph, identifier, ch, gch, attr, str, preselected)
    winid window;	/* window to use, must be of type NHW_MENU */
    int glyph;		/* glyph to display with item (unused) */
    const anything *identifier;	/* what to return if selected */
    char ch;		/* keyboard accelerator (0 = pick our own) */
    char gch;		/* group accelerator (0 = no group) */
    int attr;		/* attribute for string (like tty_putstr()) */
    const char *str;	/* menu string */
    boolean preselected; /* item is marked as selected */
{
  tty_add_menu_colored(window, glyph, identifier, ch, gch, attr, NO_COLOR, str,
                       preselected);
}

/* Invert the given list, can handle NULL as an input. */
STATIC_OVL tty_menu_item *
reverse(curr)
    tty_menu_item *curr;
{
    tty_menu_item *next, *head = 0;

    while (curr) {
	next = curr->next;
	curr->next = head;
	head = curr;
	curr = next;
    }
    return head;
}

/*
 * End a menu in this window, window must a type NHW_MENU.  This routine
 * processes the string list.  We calculate the # of pages, then assign
 * keyboard accelerators as needed.  Finally we decide on the width and
 * height of the window.
 */
void
tty_end_menu(window, prompt)
    winid window;	/* menu to use */
    const char *prompt;	/* prompt to for menu */
{
    struct WinDesc *cw = 0;
    tty_menu_item *curr;
    short len;
    int lmax, n;
    char menu_ch;

    if (window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0 ||
		cw->type != NHW_MENU)
	panic(winpanicstr,  window);

    /* Reverse the list so that items are in correct order. */
    cw->mlist = reverse(cw->mlist);

    /* Put the promt at the beginning of the menu. */
    if (prompt) {
	anything any;

	any.a_void = 0;	/* not selectable */
	tty_add_menu(window, NO_GLYPH, &any, 0, 0, ATR_NONE, "", MENU_UNSELECTED);
	tty_add_menu(window, NO_GLYPH, &any, 0, 0, ATR_NONE, prompt, MENU_UNSELECTED);
    }

    lmax = min(52, (int)ttyDisplay->rows - 1);		/* # lines per page */
    cw->npages = (cw->nitems + (lmax - 1)) / lmax;	/* # of pages */

    /* make sure page list is large enough */
    if (cw->plist_size < cw->npages+1 /*need 1 slot beyond last*/) {
	if (cw->plist) free((genericptr_t)cw->plist);
	cw->plist_size = cw->npages + 1;
	cw->plist = (tty_menu_item **)
			alloc(cw->plist_size * sizeof(tty_menu_item *));
    }

    cw->cols = 0; /* cols is set when the win is initialized... (why?) */
    menu_ch = '?';	/* lint suppression */
    for (n = 0, curr = cw->mlist; curr; n++, curr = curr->next) {
	/* set page boundaries and character accelerators */
	if ((n % lmax) == 0) {
	    menu_ch = 'a';
	    cw->plist[n/lmax] = curr;
	}
	if (curr->identifier.a_void && !curr->selector) {
	    curr->str[0] = curr->selector = menu_ch;
	    if (menu_ch++ == 'z') menu_ch = 'A';
	}

	/* cut off any lines that are too long */
	len = strlen(curr->str) + 2;	/* extra space at beg & end */
	if (len > (int)ttyDisplay->cols) {
	    curr->str[ttyDisplay->cols-2] = 0;
	    len = ttyDisplay->cols;
	}
	if (len > cw->cols) cw->cols = len;
    }
    cw->plist[cw->npages] = 0;	/* plist terminator */

    /*
     * If greater than 1 page, morestr is "(x of y) " otherwise, "(end) "
     */
    if (cw->npages > 1) {
	char buf[QBUFSZ];
	/* produce the largest demo string */
	Sprintf(buf, "(%d of %d) ", cw->npages, cw->npages);
	len = strlen(buf);
	cw->morestr = copy_of("");
    } else {
	cw->morestr = copy_of("(end) ");
	len = strlen(cw->morestr);
    }

    if (len > (int)ttyDisplay->cols) {
	/* truncate the prompt if its too long for the screen */
	if (cw->npages <= 1)	/* only str in single page case */
	    cw->morestr[ttyDisplay->cols] = 0;
	len = ttyDisplay->cols;
    }
    if (len > cw->cols) cw->cols = len;

    cw->maxcol = cw->cols;

    /*
     * The number of lines in the first page plus the morestr will be the
     * maximum size of the window.
     */
    if (cw->npages > 1)
	cw->maxrow = cw->rows = lmax + 1;
    else
	cw->maxrow = cw->rows = cw->nitems + 1;
}

int
tty_select_menu(window, how, menu_list)
    winid window;
    int how;
    menu_item **menu_list;
{
    register struct WinDesc *cw = 0;
    tty_menu_item *curr;
    menu_item *mi;
    int n, cancelled;

    if(window == WIN_ERR || (cw = wins[window]) == (struct WinDesc *) 0
       || cw->type != NHW_MENU)
	panic(winpanicstr,  window);

    *menu_list = (menu_item *) 0;
    cw->how = (short) how;
    morc = 0;
    tty_display_nhwindow(window, TRUE);
    cancelled = !!(cw->flags & WIN_CANCELLED);
    tty_dismiss_nhwindow(window);	/* does not destroy window data */

    if (cancelled) {
	n = -1;
    } else {
	for (n = 0, curr = cw->mlist; curr; curr = curr->next)
	    if (curr->selected) n++;
    }

    if (n > 0) {
	*menu_list = (menu_item *) alloc(n * sizeof(menu_item));
	for (mi = *menu_list, curr = cw->mlist; curr; curr = curr->next)
	    if (curr->selected) {
		mi->item = curr->identifier;
		mi->count = curr->count;
		mi++;
	    }
    }

    return n;
}

/* special hack for treating top line --More-- as a one item menu */
char
tty_message_menu(let, how, mesg)
char let;
int how;
const char *mesg;
{
    /* "menu" without selection; use ordinary pline, no more() */
    if (how == PICK_NONE) {
	pline("%s", mesg);
	return 0;
    }

    ttyDisplay->dismiss_more = let;
    morc = 0;
    /* barebones pline(); since we're only supposed to be called after
       response to a prompt, we'll assume that the display is up to date */
    tty_putstr(WIN_MESSAGE, 0, mesg);
    /* if `mesg' didn't wrap (triggering --More--), force --More-- now */
    if (ttyDisplay->toplin == 1) {
	more();
	ttyDisplay->toplin = 1; /* more resets this */
	tty_clear_nhwindow(WIN_MESSAGE);
    }
    /* normally <ESC> means skip further messages, but in this case
       it means cancel the current prompt; any other messages should
       continue to be output normally */
    wins[WIN_MESSAGE]->flags &= ~WIN_CANCELLED;
    ttyDisplay->dismiss_more = 0;

    return ((how == PICK_ONE && morc == let) || morc == '\033') ? morc : '\0';
}

void
tty_update_inventory()
{
    return;
}

void
tty_mark_synch()
{
    (void) fflush(stdout);
}

void
tty_wait_synch()
{
    /* we just need to make sure all windows are synch'd */
    if(!ttyDisplay || ttyDisplay->rawprint) {
	getret();
	if(ttyDisplay) ttyDisplay->rawprint = 0;
    } else {
	tty_display_nhwindow(WIN_MAP, FALSE);
	if(ttyDisplay->inmore) {
	    addtopl("--More--");
	    (void) fflush(stdout);
	} else if(ttyDisplay->inread > program_state.gameover) {
	    /* this can only happen if we were reading and got interrupted */
	    ttyDisplay->toplin = 3;
	    /* do this twice; 1st time gets the Quit? message again */
	    (void) tty_doprev_message();
	    (void) tty_doprev_message();
	    ttyDisplay->intr++;
	    (void) fflush(stdout);
	}
    }
}

void
docorner(xmin, ymax)
    register int xmin, ymax;
{
    register int y;
    register struct WinDesc *cw = wins[WIN_MAP];

    if (u.uswallow) {	/* Can be done more efficiently */
	swallowed(1);
	return;
    }

#if defined(SIGWINCH) && defined(CLIPPING)
    if(ymax > LI) ymax = LI;		/* can happen if window gets smaller */
#endif
    for (y = 0; y < ymax; y++) {
	tty_curs(BASE_WINDOW, xmin,y);	/* move cursor */
	cl_end();			/* clear to end of line */
#ifdef CLIPPING
	if (y<(int) cw->offy || y+clipy > ROWNO)
		continue; /* only refresh board */
#if defined(USE_TILES) && defined(MSDOS)
	if (iflags.tile_view)
		row_refresh((xmin/2)+clipx-((int)cw->offx/2),COLNO-1,y+clipy-(int)cw->offy);
	else
#endif
	row_refresh(xmin+clipx-(int)cw->offx,COLNO-1,y+clipy-(int)cw->offy);
#else
	if (y<cw->offy || y > ROWNO) continue; /* only refresh board  */
	row_refresh(xmin-(int)cw->offx,COLNO-1,y-(int)cw->offy);
#endif
    }

    end_glyphout();
    if (ymax >= (int) wins[WIN_STATUS]->offy) {
					/* we have wrecked the bottom line */
	flags.botlx = 1;
	bot();
    }
    fade_topl(); /* and we have also wrecked the top line */
}

void
end_glyphout()
{
#if defined(ASCIIGRAPH) && !defined(NO_TERMS)
    if (GFlag) {
	GFlag = FALSE;
	graph_off();
    }
#endif
    tty_end_color();
}

#ifndef WIN32
void
g_putch(in_ch)
int in_ch;
{
    register char ch = (char)in_ch;

# if defined(ASCIIGRAPH) && !defined(NO_TERMS)
    if (iflags.IBMgraphics || iflags.eight_bit_tty) {
	/* IBM-compatible displays don't need other stuff */
	(void) onechar(ch);
    } else if (ch & 0x80) {
	if (!GFlag || HE_resets_AS) {
	    graph_on();
	    GFlag = TRUE;
	}
	(void) onechar((ch ^ 0x80)); /* Strip 8th bit */
    } else {
	if (GFlag) {
	    graph_off();
	    GFlag = FALSE;
	}
	(void) onechar(ch);
    }

#else
    (void) onechar(ch);

#endif	/* ASCIIGRAPH && !NO_TERMS */

    return;
}
#endif /* !WIN32 */

#ifdef CLIPPING
void
setclipped()
{
	clipping = TRUE;
	clipx = clipy = 0;
	clipxmax = CO;
	clipymax = LI - 3;
}

void
tty_cliparound(x, y)
int x, y;
{
	extern boolean restoring;
	int oldx = clipx, oldy = clipy;

	if (!clipping) return;
	if (x < clipx + 5) {
		clipx = max(0, x - 20);
		clipxmax = clipx + CO;
	}
	else if (x > clipxmax - 5) {
		clipxmax = min(COLNO, clipxmax + 20);
		clipx = clipxmax - CO;
	}
	if (y < clipy + 2) {
		clipy = max(0, y - (clipymax - clipy) / 2);
		clipymax = clipy + (LI - 3);
	}
	else if (y > clipymax - 2) {
		clipymax = min(ROWNO, clipymax + (clipymax - clipy) / 2);
		clipy = clipymax - (LI - 3);
	}
	if (clipx != oldx || clipy != oldy) {
	    if (on_level(&u.uz0, &u.uz) && !restoring)
		(void) doredraw();
	}
}
#endif /* CLIPPING */


/*
 *  tty_print_glyph
 *
 *  Print the glyph to the output device.  Don't flush the output device.
 *
 *  Since this is only called from show_glyph(), it is assumed that the
 *  position and glyph are always correct (checked there)!
 */

void
tty_print_glyph(window, x, y, glyph)
    winid window;
    xchar x, y;
    int glyph;
{
    int ch;
    boolean reverse_on = FALSE;
    boolean uline_on = FALSE;
    int	    color;
    unsigned special;
    
#ifdef CLIPPING
    if(clipping) {
	if(x <= clipx || y < clipy || x >= clipxmax || y >= clipymax)
	    return;
    }
#endif
    /* map glyph to character and color */
    mapglyph(glyph, &ch, &color, &special, x, y);

    /* Move the cursor. */
    tty_curs(window, x,y);

#ifndef NO_TERMS
    if (ul_hack && ch == '_') {		/* non-destructive underscore */
	(void) onechar((char) ' ');
	backsp();
    }
#endif

#ifdef TEXTCOLOR
    tty_start_color(color);
#endif /* TEXTCOLOR */

    if (((special & MG_PET) && iflags.hilite_pet) ||
	((special & MG_DETECT) && iflags.use_inverse)) {
	tty_start_attr(ATR_INVERSE);
	reverse_on = TRUE;
    }
    if ((special & MG_ULINE)) {
        tty_start_attr(ATR_ULINE);
        uline_on = TRUE;
    }

#if defined(USE_TILES) && defined(MSDOS)
    if (iflags.grmode && iflags.tile_view)
      xputg(glyph,ch,special);
    else
#endif
	g_putch(ch);		/* print the character */

#ifdef TEXTCOLOR
    tty_end_color();
#endif /* TEXTCOLOR */

    tty_end_attr(ATR_INVERSE);
    tty_end_attr(ATR_ULINE);

    wins[window]->curx++;	/* one character over */
    ttyDisplay->curx++;		/* the real cursor moved too */
}

void
tty_raw_print(str)
    const char *str;
{
    if(ttyDisplay) ttyDisplay->rawprint++;
#if defined(MICRO) || defined(WIN32CON)
    msmsg("%s\n", str);
#else
    puts(str); (void) fflush(stdout);
#endif
}

void
tty_raw_print_bold(str)
    const char *str;
{
    if(ttyDisplay) ttyDisplay->rawprint++;
    term_start_raw_bold();
#if defined(MICRO) || defined(WIN32CON)
    msmsg("%s", str);
#else
    (void) fputs(str, stdout);
#endif
    term_end_raw_bold();
#if defined(MICRO) || defined(WIN32CON)
    msmsg("\n");
#else
    puts("");
    (void) fflush(stdout);
#endif
}

int
tty_nhgetch()
{
    int i;
#ifdef UNIX
    /* kludge alert: Some Unix variants return funny values if getc()
     * is called, interrupted, and then called again.  There
     * is non-reentrant code in the internal _filbuf() routine, called by
     * getc().
     */
    static volatile int nesting = 0;
    char nestbuf;
#endif

    (void) fflush(stdout);
    /* Note: if raw_print() and wait_synch() get called to report terminal
     * initialization problems, then wins[] and ttyDisplay might not be
     * available yet.  Such problems will probably be fatal before we get
     * here, but validate those pointers just in case...
     */
    if (WIN_MESSAGE != WIN_ERR && wins[WIN_MESSAGE])
	    wins[WIN_MESSAGE]->flags &= ~WIN_STOP;
#ifdef UNIX
    i = ((++nesting == 1) ? tgetch() :
	 (read(fileno(stdin), (genericptr_t)&nestbuf,1) == 1 ? (int)nestbuf :
								EOF));
    --nesting;
#else
    i = tgetch();
#endif
    if (!i) i = '\033'; /* map NUL to ESC since nethack doesn't expect NUL */
    if (ttyDisplay && ttyDisplay->toplin == 1)
	ttyDisplay->toplin = 2;
    return i;
}

/*
 * return a key, or 0, in which case a mouse button was pressed
 * mouse events should be returned as character postitions in the map window.
 * Since normal tty's don't have mice, just return a key.
 */
/*ARGSUSED*/
int
tty_nh_poskey(x, y, mod)
    int *x, *y, *mod;
{
# if defined(WIN32CON)
    int i;
    (void) fflush(stdout);
    /* Note: if raw_print() and wait_synch() get called to report terminal
     * initialization problems, then wins[] and ttyDisplay might not be
     * available yet.  Such problems will probably be fatal before we get
     * here, but validate those pointers just in case...
     */
    if (WIN_MESSAGE != WIN_ERR && wins[WIN_MESSAGE])
	    wins[WIN_MESSAGE]->flags &= ~WIN_STOP;
    i = ntposkey(x, y, mod);
    if (!i && mod && *mod == 0)
	i = '\033'; /* map NUL to ESC since nethack doesn't expect NUL */
    if (ttyDisplay && ttyDisplay->toplin == 1)
		ttyDisplay->toplin = 2;
    return i;
# else
    return tty_nhgetch();
# endif
}

void
win_tty_init()
{
# if defined(WIN32CON)
    nttty_open();
# endif
    return;
}

#ifdef POSITIONBAR
void
tty_update_positionbar(posbar)
char *posbar;
{
# ifdef MSDOS
	video_update_positionbar(posbar);
# endif
}
#endif

/*
 * Allocate a copy of the given string.	 If null, return a string of
 * zero length.
 *
 * This is an exact duplicate of copy_of() in X11/winmenu.c.
 */
static char *
copy_of(s)
    const char *s;
{
    if (!s) s = "";
    return strcpy((char *) alloc((unsigned) (strlen(s) + 1)), s);
}

/*
 * The routine that does all the actual drawing. (onechar is used to
 * write characters from the user's point of view; xputs/xputc is used
 * for control codes).
 */
static int current_color = CLR_GRAY;
static int current_background = CLR_BLACK;
static int current_attrs = 0;
static int last_color = NO_COLOR;
static int last_background = NO_COLOR;
static boolean last_wc_color = FALSE;
static int last_attrs = 0;
void
onechar(c)
int c;
{
    boolean repeatattrs = FALSE;
    int temp_ca = current_attrs;
    int temp_fg = current_color;
    int temp_bg = current_background;
    if (iflags.wc_color != last_wc_color) {
        repeatattrs = TRUE;
        if (!iflags.wc_color) {
            term_end_color();
            term_end_background();
        }
        last_wc_color = iflags.wc_color;
        last_color = NO_COLOR;
        last_background = NO_COLOR;
    } else {
        /* Check for, say, green-on-green; change it to inverse green. */
        if (temp_fg == temp_bg) {
          temp_fg = CLR_BLACK;
        }
    }
    /* This is asymmetrical, in that turning one attr off sometimes
       (but not always) turns other attrs and color off too, but turning
       an attr on always works fine. */
    if (temp_ca != last_attrs || repeatattrs) {
        /* Anything turned off? */
        if ((last_attrs & 32) && !(temp_fg & BRIGHT)) {
            /* The value of bold is unknown. Turn it off, and mark it as
               turned off, so it gets set to the right value, unless
               we're about to write a bright color anyway and it doesn't
               matter. */
            if (temp_ca & 1) last_attrs &= ~1; else last_attrs |= 1;
            last_attrs &= ~32;
        }
        if (last_attrs & ~temp_ca &  1) term_end_attr(ATR_BOLD);
        if (last_attrs & ~temp_ca &  2) term_end_attr(ATR_DIM);
        if (last_attrs & ~temp_ca &  4) term_end_attr(ATR_ULINE);
        if (last_attrs & ~temp_ca &  8) term_end_attr(ATR_BLINK);
        if (last_attrs & ~temp_ca & 16) term_end_attr(ATR_INVERSE);
        if (last_attrs & ~temp_ca) repeatattrs = TRUE;
        if (repeatattrs) last_attrs = 0; /* do them all again */
    }
    if (iflags.wc_color && (temp_fg != last_color || repeatattrs)) {
        term_start_color(temp_fg);
        last_color = temp_fg;
        /* Setting the color to a bright one may implicitly turn on bold. */
        if (temp_fg & BRIGHT) last_attrs |= 32;
        repeatattrs = TRUE;
    }
    if (temp_ca != last_attrs || repeatattrs) {
        /* Anything turned on? */
        if (~last_attrs & temp_ca &  1) term_start_attr(ATR_BOLD);
        if (~last_attrs & temp_ca &  2) term_start_attr(ATR_DIM);
        if (~last_attrs & temp_ca &  4) term_start_attr(ATR_ULINE);
        if (~last_attrs & temp_ca &  8) term_start_attr(ATR_BLINK);
        if (~last_attrs & temp_ca & 16) term_start_attr(ATR_INVERSE);
        last_attrs = temp_ca | (last_attrs & 32);
    }
    if (iflags.wc_color && (temp_bg != last_background || repeatattrs)) {
        /* Set the background color */
        term_start_background(temp_bg);
        last_background = temp_bg;
    }
    if (c != 0) putchar(c);
}
static void
tty_start_attr(a)
int a;
{
    switch(a) {
    case 0: break;
    case ATR_BOLD:    current_attrs |= 1; break;
    case ATR_DIM:     current_attrs |= 2; break;
    case ATR_ULINE:   current_attrs |= 4; break;
    case ATR_BLINK:   current_attrs |= 8; break;
    case ATR_INVERSE: current_attrs |= 16; break;
    default: panic ("Unknown attribute in tty_start_attr: %d", a);
    }
}
static void
tty_end_attr(a)
int a;
{
    switch(a) {
    case 0: break;
    case ATR_BOLD:    current_attrs &= ~1; break;
    case ATR_DIM:     current_attrs &= ~2; break;
    case ATR_ULINE:   current_attrs &= ~4; break;
    case ATR_BLINK:   current_attrs &= ~8; break;
    case ATR_INVERSE: current_attrs &= ~16; break;
    default: panic ("Unknown attribute in tty_end_attr: %d", a);
    }
}
static void
tty_start_color(c)
int c;
{
    current_color = c % CLR_MAX;
    current_background = c / CLR_MAX;
    if (current_color == NO_COLOR) current_color = CLR_GRAY;
    if (current_color == CLR_BLACK) current_color = CLR_BLUE;
}
static void
tty_end_color()
{
    /* We no longer use NO_COLOR, except with color turned off, because
       if you set the background explicitly, you should also set the
       foreground explicitly. */
    current_color = CLR_GRAY;
    current_background = CLR_BLACK;
}

/* Some public versions, to stop people trying to mess with attributes
   directly. */
void
tty_inverse_start()
{
    tty_start_attr(ATR_INVERSE);
}
void
tty_inverse_end()
{
    tty_end_attr(ATR_INVERSE);
}
void
tty_start_fade()
{
    tty_start_color(CLR_BLUE);
}
void
tty_end_fade()
{
    tty_end_color();
}


#endif /* TTY_GRAPHICS */

/*wintty.c*/
