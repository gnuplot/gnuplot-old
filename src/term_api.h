/*
 * $Id: term_api.h,v 1.3.2.1 2000/06/22 12:57:39 broeker Exp $
 */

/* GNUPLOT - term_api.h */

/*[
 * Copyright 1999   Thomas Williams, Colin Kelley
 *
 * Permission to use, copy, and distribute this software and its
 * documentation for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.
 *
 * Permission to modify the software is granted, but not the right to
 * distribute the complete modified source code.  Modifications are to
 * be distributed as patches to the released version.  Permission to
 * distribute binaries produced by compiling modified sources is granted,
 * provided you
 *   1. distribute the corresponding source modifications from the
 *    released version in the form of a patch file along with the binaries,
 *   2. add special version identification to distinguish your version
 *    in addition to the base release version number,
 *   3. provide your name and address as the primary contact for the
 *    support of your modified version, and
 *   4. retain our contact information in regard to use of the base
 *    software.
 * Permission to distribute the released version of the source code along
 * with corresponding source modifications in the form of a patch file is
 * granted with same provisions 2 through 4 for binary distributions.
 *
 * This software is provided "as is" without express or implied warranty
 * to the extent permitted by applicable law.
]*/

#ifndef GNUPLOT_TERM_API_H
# define GNUPLOT_TERM_API_H

/* #if... / #include / #define collection: */

#include "syscfg.h"
#include "stdfn.h"
#include "gp_types.h"

/* Type definitions */

/* this order means we can use  x-(just*strlen(text)*t->h_char)/2 if
 * term cannot justify
 */
typedef enum JUSTIFY {
    LEFT,
    CENTRE,
    RIGHT
} JUSTIFY;

/* we use a similar trick for vertical justification of multi-line labels */
typedef enum VERT_JUSTIFY {
    JUST_TOP,
    JUST_CENTRE,
    JUST_BOT,
} VERT_JUSTIFY;

typedef struct lp_style_type {	/* contains all Line and Point properties */
    int     pointflag;		/* 0 if points not used, otherwise 1 */
    int     l_type;		/* -3 if line is not to be drawn */
    int	    p_type;
    double  l_width;
    double  p_size;
    /* ... more to come ? */
} lp_style_type;

/* values for the optional flags field - choose sensible defaults
 * these aren't really very sensible names - multiplot attributes
 * depend on whether stdout is redirected or not. Remember that
 * the default is 0. Thus most drivers can do multiplot only if
 * the output is redirected
 */
#define TERM_CAN_MULTIPLOT    1  /* tested if stdout not redirected */
#define TERM_CANNOT_MULTIPLOT 2  /* tested if stdout is redirected  */
#define TERM_BINARY           4  /* open output file with "b"       */

/* The terminal interface structure --- heart of the terminal layer.
 *
 * It should go without saying that additional entries may be made
 * only at the end of this structure. Any fields added must be
 * optional - a value of 0 (or NULL pointer) means an older driver
 * does not support that feature - gnuplot must still be able to
 * function without that terminal feature
 */

typedef struct TERMENTRY {
    const char *name;
#ifdef WIN16
    const char GPFAR description[80];  /* to make text go in FAR segment */
#else
    const char *description;
#endif
    unsigned int xmax,ymax,v_char,h_char,v_tic,h_tic;

    void (*options) __PROTO((void));
    void (*init) __PROTO((void));
    void (*reset) __PROTO((void));
    void (*text) __PROTO((void));
    int (*scale) __PROTO((double, double));
    void (*graphics) __PROTO((void));
    void (*move) __PROTO((unsigned int, unsigned int));
    void (*vector) __PROTO((unsigned int, unsigned int));
    void (*linetype) __PROTO((int));
    void (*put_text) __PROTO((unsigned int, unsigned int, const char*));
    /* the following are optional. set term ensures they are not NULL */
    int (*text_angle) __PROTO((int));
    int (*justify_text) __PROTO((enum JUSTIFY));
    void (*point) __PROTO((unsigned int, unsigned int,int));
    void (*arrow) __PROTO((unsigned int, unsigned int, unsigned int, unsigned int, TBOOLEAN));
    int (*set_font) __PROTO((const char *font));
    void (*pointsize) __PROTO((double)); /* change pointsize */
    int flags;
    void (*suspend) __PROTO((void)); /* called after one plot of multiplot */
    void (*resume)  __PROTO((void)); /* called before plots of multiplot */
    void (*fillbox) __PROTO((int, unsigned int, unsigned int, unsigned int, unsigned int)); /* clear in multiplot mode */
    void (*linewidth) __PROTO((double linewidth));
#ifdef USE_MOUSE
    int (*waitforinput) __PROTO((void));     /* used for mouse input */
    void (*put_tmptext) __PROTO((int, const char []));   /* draws temporary text; int determines where: 0=statusline, 1,2: at corners of zoom box, with \r separating text above and below the point */
    void (*set_ruler) __PROTO((int, int));    /* set ruler location; x<0 switches ruler off */
    void (*set_cursor) __PROTO((int, int, int));   /* set cursor style and corner of rubber band */
    void (*set_clipboard) __PROTO((const char[]));  /* write text into cut&paste buffer (clipboard) */
#endif
} TERMENTRY;

#ifdef WIN16
# define termentry TERMENTRY far
#else
# define termentry TERMENTRY
#endif

enum set_encoding_id {
   S_ENC_DEFAULT, S_ENC_ISO8859_1, S_ENC_CP437, S_ENC_CP850,
   S_ENC_INVALID
};


/* Variables of term.c needed by other modules: */

/* the terminal info structure, being the heart of the whole module */
extern struct termentry *term;
/* Options string of the currently used terminal driver */
extern char term_options[];

/* Current 'output' file: name and open filehandle */
extern char *outstr;
extern FILE *gpoutfile;

extern TBOOLEAN multiplot;

/* 'set encoding' support: index of current encoding ... */
extern enum set_encoding_id encoding;
/* ... in table of encoding names: */
extern const char *encoding_names[];
/* parsing table for encodings */
extern struct gen_table set_encoding_tbl[];

/* Prototypes of functions exported by term.c */

void term_set_output __PROTO((char *));
void term_init __PROTO((void));
void term_start_plot __PROTO((void));
void term_end_plot __PROTO((void));
void term_start_multiplot __PROTO((void));
void term_end_multiplot __PROTO((void));
/* void term_suspend __PROTO((void)); */
void term_reset __PROTO((void));
void term_apply_lp_properties __PROTO((struct lp_style_type *lp));
void term_check_multiplot_okay __PROTO((TBOOLEAN));

extern void write_multiline __PROTO((unsigned int, unsigned int, char *, JUSTIFY, VERT_JUSTIFY, int, const char *));
GP_INLINE int term_count __PROTO((void));
void list_terms __PROTO((void));
struct termentry *set_term __PROTO((int));
void init_terminal __PROTO((void));
void test_term __PROTO((void));

#ifdef LINUXVGA
void LINUX_setup __PROTO((void));
#endif

#ifdef VMS
void vms_reset();
#endif

#endif /* GNUPLOT_TERM_API_H */
