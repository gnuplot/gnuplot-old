#ifndef lint
static char *RCSid() { return RCSid("$Id: graph3d.c,v 1.26.2.3 2000/06/22 12:57:38 broeker Exp $"); }
#endif

/* GNUPLOT - graph3d.c */

/*[
 * Copyright 1986 - 1993, 1998   Thomas Williams, Colin Kelley
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


/*
 * AUTHORS
 *
 *   Original Software:
 *       Gershon Elber and many others.
 *
 * 19 September 1992  Lawrence Crowl  (crowl@cs.orst.edu)
 * Added user-specified bases for log scaling.
 *
 * 3.6 - split graph3d.c into graph3d.c (graph),
 *                            util3d.c (intersections, etc)
 *                            hidden3d.c (hidden-line removal code)
 *
 */

#include "graph3d.h"

#include "alloc.h"
#include "axis.h"
#include "gadgets.h"
/*  #include "graphics.h" */		/* HBB 20000506: put in again, for label_width() */
#include "hidden3d.h"
#include "misc.h"
/*  #include "setshow.h" */
#include "term_api.h"
#include "util3d.h"
#include "util.h"

static int p_height;
static int p_width;		/* pointsize * t->h_tic */
static int key_entry_height;	/* bigger of t->v_size, pointsize*t->v_tick */

/* is contouring wanted ? */
t_contour_placement draw_contour = CONTOUR_NONE;
/* different linestyles are used for contours when set */
TBOOLEAN label_contours = TRUE;	

/* Want to draw surfaces? FALSE mainly useful in contouring mode */
TBOOLEAN draw_surface = TRUE;

/* Was hidden3d display selected by user? */
TBOOLEAN hidden3d = FALSE;

/* rotation and scale of the 3d view, as controlled by 'set view */
float surface_rot_z = 30.0;
float surface_rot_x = 60.0;
float surface_scale = 1.0;
float surface_zscale = 1.0;

/* position of the base plane, as given by 'set ticslevel' */
float ticslevel = 0.5;

/* 'set isosamples' settings */
int iso_samples_1 = ISO_SAMPLES;
int iso_samples_2 = ISO_SAMPLES;

double xscale3d, yscale3d, zscale3d;

static void plot3d_impulses __PROTO((struct surface_points * plot));
static void plot3d_lines __PROTO((struct surface_points * plot));
static void plot3d_points __PROTO((struct surface_points * plot));
static void plot3d_dots __PROTO((struct surface_points * plot));
static void cntr3d_impulses __PROTO((struct gnuplot_contours * cntr,
				     struct lp_style_type * lp));
static void cntr3d_lines __PROTO((struct gnuplot_contours * cntr,
				  struct lp_style_type * lp));
static void cntr3d_linespoints __PROTO((struct gnuplot_contours * cntr,
					struct lp_style_type * lp));
static void cntr3d_points __PROTO((struct gnuplot_contours * cntr,
				   struct lp_style_type * lp));
static void cntr3d_dots __PROTO((struct gnuplot_contours * cntr));
static void check_corner_height __PROTO((struct coordinate GPHUGE * point,
					 double height[2][2], double depth[2][2]));
static void setup_3d_box_corners __PROTO((void));
static void draw_3d_graphbox __PROTO((struct surface_points * plot,
				      int plot_count));
static void xtick_callback __PROTO((AXIS_INDEX, double place, char *text,
				    struct lp_style_type grid));
static void ytick_callback __PROTO((AXIS_INDEX, double place, char *text,
				    struct lp_style_type grid));
static void ztick_callback __PROTO((AXIS_INDEX, double place, char *text,
				    struct lp_style_type grid));

static int find_maxl_cntr __PROTO((struct gnuplot_contours * contours, int *count));
static int find_maxl_keys3d __PROTO((struct surface_points *plots, int count, int *kcnt));
static void boundary3d __PROTO((TBOOLEAN scaling, struct surface_points * plots,
				int count));

/* put entries in the key */
static void key_sample_line __PROTO((int xl, int yl));
static void key_sample_point __PROTO((int xl, int yl, int pointtype));
static void key_text __PROTO((int xl, int yl, char *text));


/*
 * The Amiga SAS/C 6.2 compiler moans about macro envocations causing
 * multiple calls to functions. I converted these macros to inline
 * functions coping with the problem without loosing speed.
 * (MGR, 1993)
 */
#ifdef AMIGA_SC_6_1
GP_INLINE static TBOOLEAN
i_inrange(int z, int min, int max)
{
    return ((min < max) ? ((z >= min) && (z <= max)) : ((z >= max) && (z <= min)));
}

GP_INLINE static double
f_max(double a, double b)
{
    return (max(a, b));
}

GP_INLINE static double
f_min(double a, double b)
{
    return (min(a, b));
}

#else
# define f_max(a,b) GPMAX((a),(b))
# define f_min(a,b) GPMIN((a),(b))
# define i_inrange(z,a,b) inrange((z),(a),(b))
#endif

#define apx_eq(x,y) (fabs(x-y) < 0.001)
#define ABS(x) ((x) >= 0 ? (x) : -(x))
#define SQR(x) ((x) * (x))

/* Define the boundary of the plot
 * These are computed at each call to do_plot, and are constant over
 * the period of one do_plot. They actually only change when the term
 * type changes and when the 'set size' factors change. 
 */

int xmiddle, ymiddle, xscaler, yscaler;
static int ptitl_cnt;
static int max_ptitl_len;
static int titlelin;
static int key_sample_width, key_rows, key_cols, key_col_wth, yl_ref;
static int ktitle_lines = 0;


/* Boundary and scale factors, in user coordinates */

/* There are several z's to take into account - I hope I get these
 * right !
 *
 * ceiling_z is the highest z in use
 * floor_z   is the lowest z in use
 * base_z is the z of the base
 * min3d_z is the lowest z of the graph area
 * max3d_z is the highest z of the graph area
 *
 * ceiling_z is either max3d_z or base_z, and similarly for floor_z
 * There should be no part of graph drawn outside
 * min3d_z:max3d_z  - apart from arrows, perhaps
 */

double floor_z;
static double ceiling_z, base_z;

transform_matrix trans_mat;

/* x and y input range endpoints where the three axes are to be
 * displayed (left, front-left, and front-right edges of the cube) */
static double xaxis_y, yaxis_x, zaxis_x, zaxis_y;

/* ... and the same for the back, right, and front corners */
static double back_x, back_y;
static double right_x, right_y;
static double front_x, front_y;

#ifdef USE_MOUSE
int axis3d_o_x, axis3d_o_y, axis3d_x_dx, axis3d_x_dy, axis3d_y_dx, axis3d_y_dy;
#endif

/* the penalty for convenience of using tic_gen to make callbacks
 * to tick routines is that we cant pass parameters very easily.
 * We communicate with the tick_callbacks using static variables
 */

/* unit vector (terminal coords) */
static double tic_unitx, tic_unity;

/* calculate the number and max-width of the keys for an splot.
 * Note that a blank line is issued after each set of contours
 */
static int
find_maxl_keys3d(plots, count, kcnt)
    struct surface_points *plots;
    int count, *kcnt;
{
    int mlen, len, surf, cnt;
    struct surface_points *this_plot;

    mlen = cnt = 0;
    this_plot = plots;
    for (surf = 0; surf < count; this_plot = this_plot->next_sp, surf++) {

	/* we draw a main entry if there is one, and we are
	 * drawing either surface, or unlabelled contours
	 */
	if (this_plot->title && *this_plot->title &&
	    (draw_surface || (draw_contour && !label_contours))) {
	    ++cnt;
	    len = strlen(this_plot->title);
	    if (len > mlen)
		mlen = len;
	}
	if (draw_contour && label_contours && this_plot->contours != NULL) {
	    len = find_maxl_cntr(this_plot->contours, &cnt);
	    if (len > mlen)
		mlen = len;
	}
    }

    if (kcnt != NULL)
	*kcnt = cnt;
    return (mlen);
}

static int
find_maxl_cntr(contours, count)
    struct gnuplot_contours *contours;
    int *count;
{
    register int cnt;
    register int mlen, len;
    register struct gnuplot_contours *cntrs = contours;

    mlen = cnt = 0;
    while (cntrs) {
	if (label_contours && cntrs->isNewLevel) {
	    len = strlen(cntrs->label);
	    if (len)
		cnt++;
	    if (len > mlen)
		mlen = len;
	}
	cntrs = cntrs->next;
    }
    *count += cnt;
    return (mlen);
}


/* borders of plotting area */
/* computed once on every call to do_plot */
static void
boundary3d(scaling, plots, count)
TBOOLEAN scaling;		/* TRUE if terminal is doing the scaling */
struct surface_points *plots;
int count;
{
    register struct termentry *t = term;
    int ytlen, i;

    titlelin = 0;

    p_height = pointsize * t->v_tic;
    p_width = pointsize * t->h_tic;
    if (key_swidth >= 0)
	key_sample_width = key_swidth * (t->h_char) + pointsize * (t->h_tic);
    else
	key_sample_width = 0;
    key_entry_height = pointsize * (t->v_tic) * 1.25 * key_vert_factor;
    if (key_entry_height < (t->v_char)) {
	/* is this reasonable ? */
	key_entry_height = (t->v_char) * key_vert_factor;
    }
    /* count max_len key and number keys (plot-titles and contour labels) with len > 0 */
    max_ptitl_len = find_maxl_keys3d(plots, count, &ptitl_cnt);
    if ((ytlen = label_width(key_title, &ktitle_lines)) > max_ptitl_len)
	max_ptitl_len = ytlen;
    key_col_wth = (max_ptitl_len + 4) * (t->h_char) + key_sample_width;

    /* luecken@udel.edu modifications
       sizes the plot to take up more of available resolution */
    if (lmargin >= 0)
	xleft = (t->h_char) * lmargin;
    else
	xleft = (t->h_char) * 2 + (t->h_tic);
    xright = (scaling ? 1 : xsize) * (t->xmax) - (t->h_char) * 2 - (t->h_tic);
    key_rows = ptitl_cnt;
    key_cols = 1;
    if (key == KEY_AUTO_PLACEMENT && key_vpos == TUNDER) {
	if (ptitl_cnt > 0) {
	    /* calculate max no cols, limited by label-length */
	    key_cols = (int) (xright - xleft) / ((max_ptitl_len + 4) * (t->h_char) + key_sample_width);
	    /* HBB 991019: fix division by zero problem */
	    if (key_cols == 0)
		key_cols = 1;
	    key_rows = (int) (ptitl_cnt / key_cols) + ((ptitl_cnt % key_cols) > 0);
	    /* now calculate actual no cols depending on no rows */
	    key_cols = (int) (ptitl_cnt / key_rows) + ((ptitl_cnt % key_rows) > 0);
	    key_col_wth = (int) (xright - xleft) / key_cols;
	    /* key_rows += ktitle_lines; - messes up key - div */
	} else {
	    key_rows = key_cols = key_col_wth = 0;
	}
    }
    /* this should also consider the view and number of lines in
     * xformat || yformat || xlabel || ylabel */

    /* an absolute 1, with no terminal-dependent scaling ? */
    ybot = (t->v_char) * 2.5 + 1;
    if (key_rows && key == KEY_AUTO_PLACEMENT && key_vpos == TUNDER)
	ybot += key_rows * key_entry_height + ktitle_lines * t->v_char;

    if (strlen(title.text)) {
	titlelin++;
	for (i = 0; i < strlen(title.text); i++) {
	    if (title.text[i] == '\\')
		titlelin++;
	}
    }
    ytop = (scaling ? 1 : ysize) * (t->ymax) - (t->v_char) * (titlelin + 1.5) - 1;
    if (key == KEY_AUTO_PLACEMENT && key_vpos != TUNDER) {
	/* calculate max no rows, limited be ytop-ybot */
	i = (int) (ytop - ybot) / (t->v_char) - 1 - ktitle_lines;
	if (ptitl_cnt > i) {
	    key_cols = (int) (ptitl_cnt / i) + ((ptitl_cnt % i) > 0);
	    /* now calculate actual no rows depending on no cols */
	    key_rows = (int) (ptitl_cnt / key_cols) + ((ptitl_cnt % key_cols) > 0);
	}
	key_rows += ktitle_lines;
    }
    if (key == KEY_AUTO_PLACEMENT && key_hpos == TOUT) {
	xright -= key_col_wth * (key_cols - 1) + key_col_wth - 2 * (t->h_char);
    }
    xleft += t->xmax * xoffset;
    xright += t->xmax * xoffset;
    ytop += t->ymax * yoffset;
    ybot += t->ymax * yoffset;
    xmiddle = (xright + xleft) / 2;
    ymiddle = (ytop + ybot) / 2;
    /* HBB 980308: sigh... another 16bit glitch: on term's with more than
     * 8000 pixels in either direction, these calculations produce garbage
     * results if done in (16bit) ints */
    xscaler = ((xright - xleft) * 4L) / 7L;	/* HBB: Magic number alert! */
    yscaler = ((ytop - ybot) * 4L) / 7L;
}

/* we precalculate features of the key, to save lots of nested
 * ifs in code - x,y = user supplied or computed position of key
 * taken to be inner edge of a line sample
 */
static int key_sample_left;	/* offset from x for left of line sample */
static int key_sample_right;	/* offset from x for right of line sample */
static int key_point_offset;	/* offset from x for point sample */
static int key_text_left;	/* offset from x for left-justified text */
static int key_text_right;	/* offset from x for right-justified text */
static int key_size_left;	/* distance from x to left edge of box */
static int key_size_right;	/* distance from x to right edge of box */

void
do_3dplot(plots, pcount, quick)
struct surface_points *plots;
int pcount;			/* count of plots in linked list */
int quick;		 	/* !=0 means plot only axes etc., for quick rotation */
{
    struct termentry *t = term;
    int surface;
    struct surface_points *this_plot = NULL;
    unsigned int xl, yl;
    /* double ztemp, temp; unused */
    struct text_label *this_label;
    struct arrow_def *this_arrow;
    TBOOLEAN scaling;
    transform_matrix mat;
    int key_count;
    char *s, *e;

    /* Initiate transformation matrix using the global view variables. */
    mat_rot_z(surface_rot_z, trans_mat);
    mat_rot_x(surface_rot_x, mat);
    mat_mult(trans_mat, trans_mat, mat);
    mat_scale(surface_scale / 2.0, surface_scale / 2.0, surface_scale / 2.0, mat);
    mat_mult(trans_mat, trans_mat, mat);

#if 0
    /* HBB 19990609: this is *not* the way to implement 'set view' <z_scale> */
    /* modify min_z/max_z so it will zscale properly. */
    ztemp = (max_array[FIRST_Z_AXIS] - min_array[FIRST_Z_AXIS]) / (2.0 * surface_zscale);
    temp = (max_array[FIRST_Z_AXIS] + min_array[FIRST_Z_AXIS]) / 2.0;
    min_array[FIRST_Z_AXIS] = temp - ztemp;
    max_array[FIRST_Z_AXIS] = temp + ztemp;
#endif /* 0 */

    /* The extrema need to be set even when a surface is not being
     * drawn.   Without this, gnuplot used to assume that the X and
     * Y axis started at zero.   -RKC
     */

    if (polar)
	graph_error("Cannot splot in polar coordinate system.");

    /* done in plot3d.c
     *    if (z_min3d == VERYLARGE || z_max3d == -VERYLARGE ||
     *      x_min3d == VERYLARGE || x_max3d == -VERYLARGE ||
     *      y_min3d == VERYLARGE || y_max3d == -VERYLARGE)
     *      graph_error("all points undefined!");
     */

    /* If we are to draw the bottom grid make sure zmin is updated properly. */
    if (axis_tics[FIRST_X_AXIS] || axis_tics[FIRST_Y_AXIS] || grid_selection) {
	base_z = min_array[FIRST_Z_AXIS]
	    - (max_array[FIRST_Z_AXIS] - min_array[FIRST_Z_AXIS]) * ticslevel;
	if (ticslevel >= 0)
	    floor_z = base_z;
	else
	    floor_z = min_array[FIRST_Z_AXIS];

	if (ticslevel < -1)
	    ceiling_z = base_z;
	else
	    ceiling_z = max_array[FIRST_Z_AXIS];
    } else {
	floor_z = base_z = min_array[FIRST_Z_AXIS];
	ceiling_z = max_array[FIRST_Z_AXIS];
    }

    /*  see comment accompanying similar tests of x_min/x_max and y_min/y_max
     *  in graphics.c:do_plot(), for history/rationale of these tests */
    if (min_array[FIRST_X_AXIS] == max_array[FIRST_X_AXIS])
	graph_error("x_min3d should not equal x_max3d!");
    if (min_array[FIRST_Y_AXIS] == max_array[FIRST_Y_AXIS])
	graph_error("y_min3d should not equal y_max3d!");
    if (min_array[FIRST_Z_AXIS] == max_array[FIRST_Z_AXIS])
	graph_error("z_min3d should not equal z_max3d!");

#ifndef LITE
    if (hidden3d) {
	struct surface_points *plot;
	int p = 0;
	/* Verify data is hidden line removable - grid based. */
	for (plot = plots; ++p <= pcount; plot = plot->next_sp) {
	    if (plot->plot_type == DATA3D && !plot->has_grid_topology) {
		fprintf(stderr, "Notice: Cannot remove hidden lines from non grid data\n");
		return;
	    }
	}
    }
#endif /* not LITE */

    term_start_plot();

    screen_ok = FALSE;
    scaling = (*t->scale) (xsize, ysize);

    /* now compute boundary for plot (xleft, xright, ytop, ybot) */
    boundary3d(scaling, plots, pcount);

    axis_set_graphical_range(FIRST_X_AXIS, xleft, xright);
    axis_set_graphical_range(FIRST_Y_AXIS, ybot, ytop);
    axis_set_graphical_range(FIRST_Z_AXIS, floor_z, ceiling_z);

    /* SCALE FACTORS */
    zscale3d = 2.0 / (ceiling_z - floor_z) * surface_zscale;
    yscale3d = 2.0 / (max_array[FIRST_Y_AXIS] - min_array[FIRST_Y_AXIS]);
    xscale3d = 2.0 / (max_array[FIRST_X_AXIS] - min_array[FIRST_X_AXIS]);

    term_apply_lp_properties(&border_lp);	/* border linetype */

    /* PLACE TITLE */
    if (*title.text != 0) {
	write_multiline((unsigned int) ((xleft + xright) / 2 + title.xoffset * t->h_char),
			(unsigned int) (ytop + (titlelin + title.yoffset) * (t->h_char)),
			title.text, CENTRE, JUST_TOP, 0, title.font);
    }

    /* PLACE TIMEDATE */
    if (*timelabel.text) {
	char str[MAX_LINE_LEN+1];
	time_t now;
	unsigned int x = t->v_char + timelabel.xoffset * t->h_char;
	unsigned int y = timelabel_bottom
	    ? yoffset * max_array[FIRST_Y_AXIS] + (timelabel.yoffset + 1) * t->v_char
	    : ytop + (timelabel.yoffset - 1) * t->v_char;

	time(&now);
	strftime(str, MAX_LINE_LEN, timelabel.text, localtime(&now));

	if (timelabel_rotate && (*t->text_angle) (1)) {
	    if (timelabel_bottom)
		write_multiline(x, y, str, LEFT, JUST_TOP, 1, timelabel.font);
	    else
		write_multiline(x, y, str, RIGHT, JUST_TOP, 1, timelabel.font);

	    (*t->text_angle) (0);
	} else {
	    if (timelabel_bottom)
		write_multiline(x, y, str, LEFT, JUST_BOT, 0, timelabel.font);
	    else
		write_multiline(x, y, str, LEFT, JUST_TOP, 0, timelabel.font);
	}
    }

    /* PLACE LABELS */
    if ((*t->pointsize)) {
	(*t->pointsize)(pointsize);
    }
    for (this_label = first_label; this_label != NULL;
	 this_label = this_label->next) {

	unsigned int x, y;
	int htic;
	int vtic;

	get_offsets(this_label, t, &htic, &vtic);

	if (this_label->layer)
	    continue;
	map3d_position(&this_label->place, &x, &y, "label");
	if (this_label->rotate && (*t->text_angle) (1)) {
	    write_multiline(x + htic, y + vtic, this_label->text, this_label->pos, CENTRE, 1, this_label->font);
	    (*t->text_angle) (0);
	} else {
	    write_multiline(x + htic, y + vtic, this_label->text, this_label->pos, CENTRE, 0, this_label->font);
	}
	if (-1 != this_label->pointstyle) {
	    (*t->point)(x, y, this_label->pointstyle);
	}
    }

    /* PLACE ARROWS */
    for (this_arrow = first_arrow; this_arrow != NULL;
	 this_arrow = this_arrow->next) {
	unsigned int sx, sy, ex, ey;

	if (this_arrow->layer)
	    continue;
	map3d_position(&this_arrow->start, &sx, &sy, "arrow");
	map3d_position(&this_arrow->end, &ex, &ey, "arrow");
	term_apply_lp_properties(&(this_arrow->lp_properties));
	(*t->arrow) (sx, sy, ex, ey, this_arrow->head);
    }

#ifndef LITE
    if (hidden3d && draw_surface && !quick) {
	init_hidden_line_removal();
	reset_hidden_line_removal();
    }
#endif /* not LITE */

    /* WORK OUT KEY SETTINGS AND DO KEY TITLE / BOX */

    if (key_reverse) {
	key_sample_left = -key_sample_width;
	key_sample_right = 0;
	key_text_left = t->h_char;
	key_text_right = (t->h_char) * (max_ptitl_len + 1);
	key_size_right = (t->h_char) * (max_ptitl_len + 2 + key_width_fix);
	key_size_left = (t->h_char) + key_sample_width;
    } else {
	key_sample_left = 0;
	key_sample_right = key_sample_width;
	key_text_left = -(int) ((t->h_char) * (max_ptitl_len + 1));
	key_text_right = -(int) (t->h_char);
	key_size_left = (t->h_char) * (max_ptitl_len + 2 + key_width_fix);
	key_size_right = (t->h_char) + key_sample_width;
    }
    key_point_offset = (key_sample_left + key_sample_right) / 2;

    if (key == KEY_AUTO_PLACEMENT) {
	if (key_vpos == TUNDER) {
#if 0
	    yl = yoffset * t->ymax + (key_rows) * key_entry_height + (ktitle_lines + 2) * t->v_char;
	    xl = max_ptitl_len * 1000 / (key_sample_width / (t->h_char) + max_ptitl_len + 2);
	    xl *= (xright - xleft) / key_cols;
	    xl /= 1000;
	    xl += xleft;
#else
	    /* HBB 19990608: why calculate these again? boundary3d has already 
	     * done it... */
	    if (ptitl_cnt > 0) {
		/* maximise no cols, limited by label-length */
		key_cols = (int) (xright - xleft) / key_col_wth;
		key_rows = (int) (ptitl_cnt + key_cols - 1) / key_cols;
		/* now calculate actual no cols depending on no rows */
		key_cols = (int) (ptitl_cnt + key_rows - 1) / key_rows;
		key_col_wth = (int) (xright - xleft) / key_cols;
		/* we divide into columns, then centre in column by considering
		 * ratio of key_left_size to key_right_size
		 * key_size_left/(key_size_left+key_size_right) * (xright-xleft)/key_cols
		 * do one integer division to maximise accuracy (hope we dont
		 * overflow !)
		 */
		xl = xleft + ((xright - xleft) * key_size_left) / (key_cols * (key_size_left + key_size_right));
		yl = yoffset * t->ymax + (key_rows) * key_entry_height + (ktitle_lines + 2) * t->v_char;
	    }
#endif
	} else {
	    if (key_vpos == TTOP) {
		yl = ytop - (t->v_tic) - t->v_char;
	    } else {
		yl = ybot + (t->v_tic) + key_entry_height * key_rows + ktitle_lines * t->v_char;
	    }
	    if (key_hpos == TOUT) {
		/* keys outside plot border (right) */
		xl = xright + (t->h_tic) + key_size_left;
	    } else if (key_hpos == TLEFT) {
		xl = xleft + (t->h_tic) + key_size_left;
	    } else {
		xl = xright - key_size_right - key_col_wth * (key_cols - 1);
	    }
	}
	yl_ref = yl - ktitle_lines * (t->v_char);
    }
    if (key == KEY_USER_PLACEMENT) {
	map3d_position(&key_user_pos, &xl, &yl, "key");
    }
    if (key != KEY_NONE && key_box.l_type > -3) {
	int yt = yl;
	int yb = yl - key_entry_height * (key_rows - ktitle_lines) - ktitle_lines * t->v_char;
	int key_xr = xl + key_col_wth * (key_cols - 1) + key_size_right;
	/* key_rows seems to contain title at this point ??? */
	term_apply_lp_properties(&key_box);
	(*t->move) (xl - key_size_left, yb);
	(*t->vector) (xl - key_size_left, yt);
	(*t->vector) (key_xr, yt);
	(*t->vector) (key_xr, yb);
	(*t->vector) (xl - key_size_left, yb);

	/* draw a horizontal line between key title and first entry  JFi */
	(*t->move) (xl - key_size_left, yt - (ktitle_lines) * t->v_char);
	(*t->vector) (xl + key_size_right, yt - (ktitle_lines) * t->v_char);
    }
    /* DRAW SURFACES AND CONTOURS */

#ifndef LITE
    if (hidden3d && draw_surface && !quick)
	plot3d_hidden(plots, pcount);
#endif /* not LITE */

    /* KEY TITLE */
    if (key != KEY_NONE && strlen(key_title)) {
	char *ss = gp_alloc(strlen(key_title) + 2, "tmp string ss");
	strcpy(ss, key_title);
	strcat(ss, "\n");
	s = ss;
	yl -= t->v_char / 2;
	while ((e = (char *) strchr(s, '\n')) != NULL) {
	    *e = '\0';
	    if (key_just == JLEFT) {
		(*t->justify_text) (LEFT);
		(*t->put_text) (xl + key_text_left, yl, s);
	    } else {
		if ((*t->justify_text) (RIGHT)) {
		    (*t->put_text) (xl + key_text_right,
				    yl, s);
		} else {
		    int x = xl + key_text_right - (t->h_char) * strlen(s);
		    if (inrange(x, xleft, xright))
			(*t->put_text) (x, yl, s);
		}
	    }
	    s = ++e;
	    yl -= t->v_char;
	}
	yl += t->v_char / 2;
	free(ss);
    }
    key_count = 0;
    yl_ref = yl -= key_entry_height / 2;	/* centralise the keys */

#define NEXT_KEY_LINE()					\
    if ( ++key_count >= key_rows ) {			\
	yl = yl_ref; xl += key_col_wth; key_count = 0;	\
    } else						\
	yl -= key_entry_height

    this_plot = plots;
    if (!quick)
	for (surface = 0;
	     surface < pcount;
	     this_plot = this_plot->next_sp, surface++) {


	    if (draw_surface) {
		TBOOLEAN lkey = (key != 0 && this_plot->title && this_plot->title[0]);
		term_apply_lp_properties(&(this_plot->lp_properties));

		if (lkey) {
		    key_text(xl, yl, this_plot->title);
		}
		switch (this_plot->plot_style) {
		case BOXES:	/* can't do boxes in 3d yet so use impulses */
		case IMPULSES:{
		    if (lkey) {
			key_sample_line(xl, yl);
		    }
		    if (!(hidden3d && draw_surface))
			plot3d_impulses(this_plot);
		    break;
		}
		case STEPS:	/* HBB: I think these should be here */
		case FSTEPS:
		case HISTEPS:
		case LINES:{
		    if (lkey) {
			key_sample_line(xl, yl);
		    }
		    if (!(hidden3d && draw_surface))
			plot3d_lines(this_plot);
		    break;
		}
		case YERRORLINES:	/* ignored; treat like points */
		case XERRORLINES:	/* ignored; treat like points */
		case XYERRORLINES:	/* ignored; treat like points */
		case YERRORBARS:	/* ignored; treat like points */
		case XERRORBARS:	/* ignored; treat like points */
		case XYERRORBARS:	/* ignored; treat like points */
		case BOXXYERROR:	/* HBB: ignore these as well */
		case BOXERROR:
		case CANDLESTICKS:	/* HBB: dito */
		case FINANCEBARS:
		case VECTOR:
		case POINTSTYLE:
		    if (lkey) {
			key_sample_point(xl, yl, this_plot->lp_properties.p_type);
		    }
		    if (!(hidden3d && draw_surface))
			plot3d_points(this_plot);
		    break;

		case LINESPOINTS:
		    /* put lines */
		    if (lkey)
			key_sample_line(xl, yl);

		    if (!(hidden3d && draw_surface))
			plot3d_lines(this_plot);

		    /* put points */
		    if (lkey)
			key_sample_point(xl, yl, this_plot->lp_properties.p_type);

		    if (!(hidden3d && draw_surface))
			plot3d_points(this_plot);

		    break;

		case DOTS:
		    if (lkey) {
			if (key == KEY_USER_PLACEMENT) {
			    if (!clip_point(xl + key_point_offset, yl))
				(*t->point) (xl + key_point_offset, yl, -1);
			} else {
			    (*t->point) (xl + key_point_offset, yl, -1);
			    /* (*t->point)(xl+2*(t->h_char),yl, -1); */
			}
		    }
		    if (!(hidden3d && draw_surface))
			plot3d_dots(this_plot);

		    break;


		}			/* switch(plot-style) */

		/* move key on a line */
		if (lkey) {
		    NEXT_KEY_LINE();
		}
	    }			/* draw_surface */

	    if (draw_contour && this_plot->contours != NULL) {
		struct gnuplot_contours *cntrs = this_plot->contours;
		struct lp_style_type thiscontour_lp_properties =
		    this_plot->lp_properties;

		thiscontour_lp_properties.l_type += (hidden3d ? 2 : 1);

		term_apply_lp_properties(&(thiscontour_lp_properties));

		if (key != KEY_NONE && this_plot->title && this_plot->title[0]
		    && !draw_surface && !label_contours) {
		    /* unlabelled contours but no surface : put key entry in now */
		    key_text(xl, yl, this_plot->title);

		    switch (this_plot->plot_style) {
		    case IMPULSES:
		    case LINES:
		    case BOXES:	/* HBB: I think these should be here... */
		    case STEPS:
		    case FSTEPS:
		    case HISTEPS:
			key_sample_line(xl, yl);
			break;
		    case YERRORLINES:	/* ignored; treat like points */
		    case XERRORLINES:	/* ignored; treat like points */
		    case XYERRORLINES:	/* ignored; treat like points */
		    case YERRORBARS:	/* ignored; treat like points */
		    case XERRORBARS:	/* ignored; treat like points */
		    case XYERRORBARS:	/* ignored; treat like points */
		    case BOXERROR:	/* HBB: ignore these as well */
		    case BOXXYERROR:
		    case CANDLESTICKS:	/* HBB: dito */
		    case FINANCEBARS:
		    case VECTOR:
		    case POINTSTYLE:
			key_sample_point(xl, yl, this_plot->lp_properties.p_type);
			break;
		    case LINESPOINTS:
			key_sample_line(xl, yl);
			break;
		    case DOTS:
			key_sample_point(xl, yl, -1);
			break;
		    }
		    NEXT_KEY_LINE();
		}
		while (cntrs) {
		    if (label_contours && cntrs->isNewLevel) {
			(*t->linetype) (thiscontour_lp_properties.l_type++);

			if (key != KEY_NONE) {

			    key_text(xl, yl, cntrs->label);

			    switch (this_plot->plot_style) {
			    case IMPULSES:
			    case LINES:
			    case LINESPOINTS:
			    case BOXES:	/* HBB: these should be treated as well... */
			    case STEPS:
			    case FSTEPS:
			    case HISTEPS:
				key_sample_line(xl, yl);
				break;
			    case YERRORLINES:	/* ignored; treat like points */
			    case XERRORLINES:	/* ignored; treat like points */
			    case XYERRORLINES:	/* ignored; treat like points */
			    case YERRORBARS:	/* ignored; treat like points */
			    case XERRORBARS:	/* ignored; treat like points */
			    case XYERRORBARS:	/* ignored; treat like points */
			    case BOXERROR:		/* HBB: treat these likewise */
			    case BOXXYERROR:
			    case CANDLESTICKS:	/* HBB: dito */
			    case FINANCEBARS:
			    case VECTOR:
			    case POINTSTYLE:
				key_sample_point(xl, yl, this_plot->lp_properties.p_type);
				break;
			    case DOTS:
				key_sample_point(xl, yl, -1);
				break;
			    }	/* switch */

			    NEXT_KEY_LINE();

			}		/* key */
		    }		/* label_contours */
		    /* now draw the contour */
		    switch (this_plot->plot_style) {
		    case BOXES:
			/* treat boxes like impulses: */
		    case IMPULSES:
			cntr3d_impulses(cntrs, &thiscontour_lp_properties);
			break;
		    case STEPS:	
		    case FSTEPS:
		    case HISTEPS:
			/* treat all the above like 'lines' */
		    case LINES:
			cntr3d_lines(cntrs, &thiscontour_lp_properties);
			break;
		    case YERRORLINES:	
		    case XERRORLINES:
		    case XYERRORLINES:
		    case YERRORBARS:
		    case XERRORBARS:
		    case XYERRORBARS:
		    case BOXERROR:
		    case BOXXYERROR:
		    case CANDLESTICKS:
		    case FINANCEBARS:
		    case VECTOR:
			/* treat all the above like points */
		    case POINTSTYLE:
			cntr3d_points(cntrs, &thiscontour_lp_properties);
			break;
		    case LINESPOINTS:
			cntr3d_linespoints(cntrs, &thiscontour_lp_properties);
			break;
		    case DOTS:
			cntr3d_dots(cntrs);
			break;
		    }		/*switch */

		    cntrs = cntrs->next;
		}			/* loop over contours */
	    }			/* draw contours */
	}				/* loop over surfaces */

    setup_3d_box_corners();

    draw_3d_graphbox(plots, pcount);
    
    /* PLACE LABELS */
    if ((*t->pointsize)) {
	(*t->pointsize)(pointsize);
    }
    for (this_label = first_label; this_label != NULL;
	 this_label = this_label->next) {

	unsigned int x, y;
	int htic;
	int vtic;

	get_offsets(this_label, t, &htic, &vtic);

	if (this_label->layer == 0)
	    continue;
	map3d_position(&this_label->place, &x, &y, "label");
	if (this_label->rotate && (*t->text_angle) (1)) {
	    write_multiline(x + htic, y + vtic, this_label->text, this_label->pos, CENTRE, 1, this_label->font);
	    (*t->text_angle) (0);
	} else {
	    write_multiline(x + htic, y + vtic, this_label->text, this_label->pos, CENTRE, 0, this_label->font);
	}
	if (-1 != this_label->pointstyle) {
	    (*t->point)(x, y, this_label->pointstyle);
	}
    }

    /* PLACE ARROWS */
    for (this_arrow = first_arrow; this_arrow != NULL;
	 this_arrow = this_arrow->next) {
	unsigned int sx, sy, ex, ey;

	if (this_arrow->layer == 0)
	    continue;
	map3d_position(&this_arrow->start, &sx, &sy, "arrow");
	map3d_position(&this_arrow->end, &ex, &ey, "arrow");
	term_apply_lp_properties(&(this_arrow->lp_properties));
	(*t->arrow) (sx, sy, ex, ey, this_arrow->head);
    }

#ifdef USE_MOUSE
    /* finally, store the 2d projection of the x and y axis, to enable zooming by mouse */
    {
	unsigned int o_x, o_y, x, y;
	map3d_xy(min_array[FIRST_X_AXIS], min_array[FIRST_Y_AXIS], base_z, &o_x, &o_y);
	axis3d_o_x = (int)o_x;
	axis3d_o_y = (int)o_y;
	map3d_xy(max_array[FIRST_X_AXIS], min_array[FIRST_Y_AXIS], base_z, &x, &y);
	axis3d_x_dx = (int)x - axis3d_o_x;
	axis3d_x_dy = (int)y - axis3d_o_y;
	map3d_xy(min_array[FIRST_X_AXIS], max_array[FIRST_Y_AXIS], base_z, &x, &y);
	axis3d_y_dx = (int)x - axis3d_o_x;
	axis3d_y_dy = (int)y - axis3d_o_y;
    }
#endif

    term_end_plot();

#ifndef LITE
    if (hidden3d && draw_surface) {
	term_hidden_line_removal();
    }
#endif /* not LITE */

}

/* plot3d_impulses:
 * Plot the surfaces in IMPULSES style
 */
static void
plot3d_impulses(plot)
struct surface_points *plot;
{
    int i;				/* point index */
    unsigned int x, y, xx0, yy0;	/* point in terminal coordinates */
    struct iso_curve *icrvs = plot->iso_crvs;

    while (icrvs) {
	struct coordinate GPHUGE *points = icrvs->points;

	for (i = 0; i < icrvs->p_count; i++) {
	    switch (points[i].type) {
	    case INRANGE:
		{
		    map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		    if (inrange(0.0, min_array[FIRST_Z_AXIS], max_array[FIRST_Z_AXIS])) {
			map3d_xy(points[i].x, points[i].y, 0.0, &xx0, &yy0);
		    } else if (inrange(min_array[FIRST_Z_AXIS], 0.0, points[i].z)) {
			map3d_xy(points[i].x, points[i].y, min_array[FIRST_Z_AXIS], &xx0, &yy0);
		    } else {
			map3d_xy(points[i].x, points[i].y, max_array[FIRST_Z_AXIS], &xx0, &yy0);
		    }

		    clip_move(xx0, yy0);
		    clip_vector(x, y);

		    break;
		}
	    case OUTRANGE:
		{
		    if (!inrange(points[i].x, min_array[FIRST_X_AXIS], max_array[FIRST_X_AXIS]) ||
			!inrange(points[i].y, min_array[FIRST_Y_AXIS], max_array[FIRST_Y_AXIS]))
			break;

		    if (inrange(0.0, min_array[FIRST_Z_AXIS], max_array[FIRST_Z_AXIS])) {
			/* zero point is INRANGE */
			map3d_xy(points[i].x, points[i].y, 0.0, &xx0, &yy0);

			/* must cross z = min_array[FIRST_Z_AXIS] or max_array[FIRST_Z_AXIS] limits */
			if (inrange(min_array[FIRST_Z_AXIS], 0.0, points[i].z) &&
			    min_array[FIRST_Z_AXIS] != 0.0 && min_array[FIRST_Z_AXIS] != points[i].z) {
			    map3d_xy(points[i].x, points[i].y, min_array[FIRST_Z_AXIS], &x, &y);
			} else {
			    map3d_xy(points[i].x, points[i].y, max_array[FIRST_Z_AXIS], &x, &y);
			}
		    } else {
			/* zero point is also OUTRANGE */
			if (inrange(min_array[FIRST_Z_AXIS], 0.0, points[i].z) &&
			    inrange(max_array[FIRST_Z_AXIS], 0.0, points[i].z)) {
			    /* crosses z = min_array[FIRST_Z_AXIS] or max_array[FIRST_Z_AXIS] limits */
			    map3d_xy(points[i].x, points[i].y, max_array[FIRST_Z_AXIS], &x, &y);
			    map3d_xy(points[i].x, points[i].y, min_array[FIRST_Z_AXIS], &xx0, &yy0);
			} else {
			    /* doesn't cross z = min_array[FIRST_Z_AXIS] or max_array[FIRST_Z_AXIS] limits */
			    break;
			}
		    }

		    clip_move(xx0, yy0);
		    clip_vector(x, y);

		    break;
		}
	    default:		/* just a safety */
	    case UNDEFINED:{
		    break;
		}
	    }
	}

	icrvs = icrvs->next;
    }
}


/* plot3d_lines:
 * Plot the surfaces in LINES style
 */
/* We want to always draw the lines in the same direction, otherwise when
   we draw an adjacent box we might get the line drawn a little differently
   and we get splotches.  */

static void
plot3d_lines(plot)
struct surface_points *plot;
{
    int i;
    unsigned int x, y, xx0, yy0;	/* point in terminal coordinates */
    double clip_x, clip_y, clip_z;
    struct iso_curve *icrvs = plot->iso_crvs;
    struct coordinate GPHUGE *points;
    enum coord_type prev = UNDEFINED;
    double lx[2], ly[2], lz[2];	/* two edge points */

#ifndef LITE
/* These are handled elsewhere.  */
    if (plot->has_grid_topology && hidden3d)
	return;
#endif /* not LITE */

    while (icrvs) {
	prev = UNDEFINED;	/* type of previous plot */

	for (i = 0, points = icrvs->points; i < icrvs->p_count; i++) {
	    switch (points[i].type) {
	    case INRANGE:{
		    map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		    if (prev == INRANGE) {
			clip_vector(x, y);
		    } else {
			if (prev == OUTRANGE) {
			    /* from outrange to inrange */
			    if (!clip_lines1) {
				clip_move(x, y);
			    } else {
				/*
				 * Calculate intersection point and draw
				 * vector from there
				 */
				edge3d_intersect(points, i, &clip_x, &clip_y, &clip_z);

				map3d_xy(clip_x, clip_y, clip_z, &xx0, &yy0);

				clip_move(xx0, yy0);
				clip_vector(x, y);
			    }
			} else {
			    clip_move(x, y);
			}
		    }

		    break;
		}
	    case OUTRANGE:{
		    if (prev == INRANGE) {
			/* from inrange to outrange */
			if (clip_lines1) {
			    /*
			     * Calculate intersection point and draw
			     * vector to it
			     */

			    edge3d_intersect(points, i, &clip_x, &clip_y, &clip_z);

			    map3d_xy(clip_x, clip_y, clip_z, &xx0, &yy0);

			    clip_vector(xx0, yy0);
			}
		    } else if (prev == OUTRANGE) {
			/* from outrange to outrange */
			if (clip_lines2) {
			    /*
			     * Calculate the two 3D intersection points
			     * if present
			     */
			    if (two_edge3d_intersect(points, i, lx, ly, lz)) {

				map3d_xy(lx[0], ly[0], lz[0], &x, &y);

				map3d_xy(lx[1], ly[1], lz[1], &xx0, &yy0);

				clip_move(x, y);
				clip_vector(xx0, yy0);
			    }
			}
		    }
		    break;
		}
	    case UNDEFINED:{
		    break;
	    default:
		    graph_error("Unknown point type in plot3d_lines");
		}
	    }

	    prev = points[i].type;
	}

	icrvs = icrvs->next;
    }
}

/* plot3d_points:
 * Plot the surfaces in POINTSTYLE style
 */
static void
plot3d_points(plot)
struct surface_points *plot;
{
    int i;
    unsigned int x, y;
    struct termentry *t = term;
    struct iso_curve *icrvs = plot->iso_crvs;

    while (icrvs) {
	struct coordinate GPHUGE *points = icrvs->points;

	for (i = 0; i < icrvs->p_count; i++) {
	    if (points[i].type == INRANGE) {
		map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		if (!clip_point(x, y))
		    (*t->point) (x, y, plot->lp_properties.p_type);
	    }
	}

	icrvs = icrvs->next;
    }
}

/* plot3d_dots:
 * Plot the surfaces in DOTS style
 */
static void
plot3d_dots(plot)
struct surface_points *plot;
{
    int i;
    struct termentry *t = term;
    struct iso_curve *icrvs = plot->iso_crvs;

    while (icrvs) {
	struct coordinate GPHUGE *points = icrvs->points;

	for (i = 0; i < icrvs->p_count; i++) {
	    if (points[i].type == INRANGE) {
		unsigned int x, y;
		map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		if (!clip_point(x, y))
		    (*t->point) (x, y, -1);
	    }
	}

	icrvs = icrvs->next;
    }
}

/* cntr3d_impulses:
 * Plot a surface contour in IMPULSES style
 */
static void
cntr3d_impulses(cntr, lp)
    struct gnuplot_contours *cntr;
    struct lp_style_type *lp;
{
    int i;				/* point index */
    vertex vertex_on_surface, vertex_on_base;

    if (draw_contour & CONTOUR_SRF) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &vertex_on_surface);
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		      &vertex_on_base);
	    draw3d_line(&vertex_on_surface, &vertex_on_base, lp);
	}
    } else {
	/* Must be on base grid, so do points. */
	cntr3d_points(cntr, lp);
    }
}

/* cntr3d_lines:
 * Plot a surface contour in LINES style
 */
static void
cntr3d_lines(cntr, lp)
    struct gnuplot_contours *cntr;
    struct lp_style_type *lp;
{
    int i;			/* point index */
    vertex previous_vertex, this_vertex;

    if (draw_contour & CONTOUR_SRF) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, cntr->coords[0].z,
		  &previous_vertex);
	/* move slightly frontward, to make sure the contours are
	 * visible in front of the the triangles they're in, if this
	 * is a hidden3d plot */
	if (hidden3d && !VERTEX_IS_UNDEFINED(previous_vertex))
	    previous_vertex.z += 1e-2;

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &this_vertex);
	    /* move slightly frontward, to make sure the contours are
	     * visible in front of the the triangles they're in, if this
	     * is a hidden3d plot */
	    if (hidden3d && !VERTEX_IS_UNDEFINED(this_vertex))
		this_vertex.z += 1e-2;
	    draw3d_line(&previous_vertex, &this_vertex, lp);
	    previous_vertex = this_vertex;
	}
    }

    if (draw_contour & CONTOUR_BASE) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, base_z,
		  &previous_vertex);

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		      &this_vertex);
	    draw3d_line(&previous_vertex, &this_vertex, lp);
	    previous_vertex=this_vertex;
	}
    }
}

/* cntr3d_linespoints:
 * Plot a surface contour in LINESPOINTS style
 */
static void
cntr3d_linespoints(cntr, lp)
    struct gnuplot_contours *cntr;
    struct lp_style_type *lp;
{
    int i;			/* point index */
    vertex previous_vertex, this_vertex;

    if (draw_contour & CONTOUR_SRF) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, cntr->coords[0].z,
		  &previous_vertex);
	/* move slightly frontward, to make sure the contours are
	 * visible in front of the the triangles they're in, if this
	 * is a hidden3d plot */
	if (hidden3d && !VERTEX_IS_UNDEFINED(previous_vertex))
	    previous_vertex.z += 1e-2;
	draw3d_point(&previous_vertex, lp);

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &this_vertex);
	    /* move slightly frontward, to make sure the contours are
	     * visible in front of the the triangles they're in, if this
	     * is a hidden3d plot */
	    if (hidden3d && !VERTEX_IS_UNDEFINED(this_vertex))
		this_vertex.z += 1e-2;
	    draw3d_line(&previous_vertex, &this_vertex, lp);
	    draw3d_point(&this_vertex, lp);
	    previous_vertex = this_vertex;
	}
    }

    if (draw_contour & CONTOUR_BASE) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, base_z,
		  &previous_vertex);
	draw3d_point(&previous_vertex, lp);

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		      &this_vertex);
	    draw3d_line(&previous_vertex, &this_vertex, lp);
	    draw3d_point(&this_vertex, lp);
	    previous_vertex=this_vertex;
	}
    }
}

/* cntr3d_points:
 * Plot a surface contour in POINTSTYLE style
 */
static void
cntr3d_points(cntr, lp)
struct gnuplot_contours *cntr;
struct lp_style_type *lp;
{
    int i;
    vertex v;

    if (draw_contour & CONTOUR_SRF) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &v);
	    /* move slightly frontward, to make sure the contours and
	     * points are visible in front of the triangles they're
	     * in, if this is a hidden3d plot */
	    if (hidden3d && !VERTEX_IS_UNDEFINED(v))
		v.z += 1e-2;
	    draw3d_point(&v, lp);
	}
    }
    if (draw_contour & CONTOUR_BASE) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		     &v);
	    draw3d_point(&v, lp);
	}
    }
}

/* cntr3d_dots:
 * Plot a surface contour in DOTS style
 */
/* FIXME HBB 20000621: this is the only contour output routine left
 * without hiddenlining. It should probably deleted altogether, and
 * its call replaced by one of cntr3d_points */
static void
cntr3d_dots(cntr)
struct gnuplot_contours *cntr;
{
    int i;
    unsigned int x, y;
    struct termentry *t = term;

    if (draw_contour & CONTOUR_SRF) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xy(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z, &x, &y);

	    if (!clip_point(x, y))
		(*t->point) (x, y, -1);
	}
    }
    if (draw_contour & CONTOUR_BASE) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xy(cntr->coords[i].x, cntr->coords[i].y, base_z,
		     &x, &y);

	    if (!clip_point(x, y))
		(*t->point) (x, y, -1);
	}
    }
}



/* map xmin | xmax to 0 | 1 and same for y
 * 0.1 avoids any rounding errors
 */
#define MAP_HEIGHT_X(x) ( (int) (((x)-min_array[FIRST_X_AXIS])/(max_array[FIRST_X_AXIS]-min_array[FIRST_X_AXIS])+0.1) )
#define MAP_HEIGHT_Y(y) ( (int) (((y)-min_array[FIRST_Y_AXIS])/(max_array[FIRST_Y_AXIS]-min_array[FIRST_Y_AXIS])+0.1) )

/* if point is at corner, update height[][] and depth[][]
 * we are still assuming that extremes of surfaces are at corners,
 * but we are not assuming order of corners
 */
static void
check_corner_height(p, height, depth)
struct coordinate GPHUGE *p;
double height[2][2];
double depth[2][2];
{
    if (p->type != INRANGE)
	return;
    if ((fabs(p->x - min_array[FIRST_X_AXIS]) < zero || fabs(p->x - max_array[FIRST_X_AXIS]) < zero) &&
	(fabs(p->y - min_array[FIRST_Y_AXIS]) < zero || fabs(p->y - max_array[FIRST_Y_AXIS]) < zero)) {
	unsigned int x = MAP_HEIGHT_X(p->x);
	unsigned int y = MAP_HEIGHT_Y(p->y);
	if (height[x][y] < p->z)
	    height[x][y] = p->z;
	if (depth[x][y] > p->z)
	    depth[x][y] = p->z;
    }
}

/* work out where the axes and tics are drawn */
static void
setup_3d_box_corners()
{
    int quadrant = surface_rot_z / 90;
    if ((quadrant + 1) & 2) {
	zaxis_x = max_array[FIRST_X_AXIS];
	right_x = min_array[FIRST_X_AXIS];
	back_y  = min_array[FIRST_Y_AXIS];
	front_y  = max_array[FIRST_Y_AXIS];
    } else {
	zaxis_x = min_array[FIRST_X_AXIS];
	right_x = max_array[FIRST_X_AXIS];
	back_y  = max_array[FIRST_Y_AXIS];
	front_y  = min_array[FIRST_Y_AXIS];
    }

    if (quadrant & 2) {
	zaxis_y = max_array[FIRST_Y_AXIS];
	right_y = min_array[FIRST_Y_AXIS];
	back_x  = max_array[FIRST_X_AXIS];
	front_x  = min_array[FIRST_X_AXIS];
    } else {
	zaxis_y = min_array[FIRST_Y_AXIS];
	right_y = max_array[FIRST_Y_AXIS];
	back_x  = min_array[FIRST_X_AXIS];
	front_x  = max_array[FIRST_X_AXIS];
    }

    if (surface_rot_x > 90) {
	/* labels on the back axes */
	yaxis_x = back_x;
	xaxis_y = back_y;
    } else {
	yaxis_x = front_x;
	xaxis_y = front_y;
    }
}

/* Draw all elements of the 3d graph box, including borders, zeroaxes,
 * tics, gridlines, ticmarks, axis labels and the base plane. */
static void
draw_3d_graphbox(plot, plot_num)
    struct surface_points *plot;
    int plot_num;
{
    unsigned int x, y;		/* point in terminal coordinates */
    struct termentry *t = term;

    if (draw_border) {
	/* the four corners of the base plane, in normalized view
	 * coordinates (-1..1) * on all three axes: */
	vertex bl, bb, br, bf;  

	/* map to normalized view coordinates the corners of the
	 * baseplane: left, back, right and front, in that order: */
	map3d_xyz(zaxis_x, zaxis_y, base_z, &bl);
	map3d_xyz(back_x , back_y , base_z, &bb);
	map3d_xyz(right_x, right_y, base_z, &br);
	map3d_xyz(front_x, front_y, base_z, &bf);

	if (draw_border & 4)
	    draw3d_line(&br, &bf, &border_lp);
	if (draw_border & 1)
	    draw3d_line(&bl, &bf, &border_lp);
	if (draw_border & 2)
	    draw3d_line(&bl, &bb, &border_lp);
	if (draw_border & 8)
	    draw3d_line(&br, &bb, &border_lp);

	/* if surface is drawn, draw the rest of the graph box, too: */
	if (draw_surface || (draw_contour & CONTOUR_SRF)) {
	    vertex fl, fb, fr, ff; /* floor left/back/right/front corners */
	    vertex tl, tb, tr, tf; /* top left/back/right/front corners */
	    
	    map3d_xyz(zaxis_x, zaxis_y, floor_z, &fl);
	    map3d_xyz(back_x , back_y , floor_z, &fb);
	    map3d_xyz(right_x, right_y, floor_z, &fr);
	    map3d_xyz(front_x, front_y, floor_z, &ff);

	    map3d_xyz(zaxis_x, zaxis_y, ceiling_z, &tl);
	    map3d_xyz(back_x , back_y , ceiling_z, &tb);
	    map3d_xyz(right_x, right_y, ceiling_z, &tr);
	    map3d_xyz(front_x, front_y, ceiling_z, &tf);

	    if ((draw_border & 0xf0) == 0xf0) {
		/* all four verticals are drawn - save some time by
		 * drawing them to the full height, regardless of
		 * where the surface lies */
		draw3d_line(&fl, &tl, &border_lp);
		draw3d_line(&fb, &tb, &border_lp);
		draw3d_line(&fr, &tr, &border_lp);
		draw3d_line(&ff, &tf, &border_lp);
	    } else {
		/* find heights of surfaces at the corners of the xy
		 * rectangle */
		double height[2][2];
		double depth[2][2];
		unsigned int zaxis_i = MAP_HEIGHT_X(zaxis_x);
		unsigned int zaxis_j = MAP_HEIGHT_Y(zaxis_y);
		unsigned int back_i = MAP_HEIGHT_X(back_x);
		unsigned int back_j = MAP_HEIGHT_Y(back_y);

		height[0][0] = height[0][1]
		    = height[1][0] = height[1][1] = base_z;
		depth[0][0] = depth[0][1]
		    = depth[1][0] = depth[1][1] = base_z;

		/* FIXME HBB 20000617: this method contains the
		 * assumption that the topological corners of the
		 * surface mesh(es) are also the geometrical ones of
		 * their xy projections. This is only true for
		 * 'explicit' surface datasets, i.e. z(x,y) */
		for (; --plot_num >= 0; plot = plot->next_sp) {
		    struct iso_curve *curve = plot->iso_crvs;
		    int count = curve->p_count;
		    int iso;
		    if (plot->plot_type == DATA3D) {
			if (!plot->has_grid_topology)
			    continue;
			iso = plot->num_iso_read;
		    } else
			iso = iso_samples_2;

		    check_corner_height(curve->points, height, depth);
		    check_corner_height(curve->points + count - 1, height, depth);
		    while (--iso)
			curve = curve->next;
		    check_corner_height(curve->points, height, depth);
		    check_corner_height(curve->points + count - 1, height, depth);
		}

#define VERTICAL(mask,x,y,i,j,bottom,top)			\
		if (draw_border&mask) {				\
		    draw3d_line(bottom,top, &border_lp);	\
		} else if (height[i][j] != depth[i][j]) {	\
		    vertex a, b;				\
		    map3d_xyz(x,y,depth[i][j],&a);		\
		    map3d_xyz(x,y,height[i][j],&b);		\
		    draw3d_line(&a, &b, &border_lp);		\
		}

		VERTICAL(16, zaxis_x, zaxis_y, zaxis_i, zaxis_j, &fl, &tl);
		VERTICAL(32, back_x, back_y, back_i, back_j, &fb, &tb);
		VERTICAL(64, right_x, right_y, 1 - zaxis_i, 1 - zaxis_j,
			 &fr, &tr);
		VERTICAL(128, front_x, front_y, 1 - back_i, 1 - back_j,
			 &ff, &tf);
#undef VERTICAL
	    } /* else (all 4 verticals drawn?) */

	    /* now border lines on top */
	    if (draw_border & 256)
		draw3d_line(&tl, &tb, &border_lp);
	    if (draw_border & 512)
		draw3d_line(&tr, &tb, &border_lp);
	    if (draw_border & 1024)
		draw3d_line(&tl, &tf, &border_lp);
	    if (draw_border & 2048)
		draw3d_line(&tr, &tf, &border_lp);
	} /* else (surface is drawn) */
    } /* if (draw_border) */

    /* Draw ticlabels and axis labels. x axis, first:*/
    if (axis_tics[FIRST_X_AXIS] || *axis_label[FIRST_X_AXIS].text) {
	vertex v0, v1;
	double other_end =
	    min_array[FIRST_Y_AXIS] + max_array[FIRST_Y_AXIS] - xaxis_y;
	double mid_x =
	    (max_array[FIRST_X_AXIS] + min_array[FIRST_X_AXIS]) / 2;

	map3d_xyz(mid_x, xaxis_y, base_z, &v0);
	map3d_xyz(mid_x, other_end, base_z, &v1);
	{	
	    double dx = v1.x - v0.x;
	    double dy = v1.y - v0.y;
	    double len = sqrt(dx * dx + dy * dy);

	    if (len != 0) {
		tic_unitx = dx / len / xscaler;
		tic_unity = dy / len / yscaler;
	    } else {
		tic_unitx = tic_unity = 0;
	    }
	}
	if (axis_tics[FIRST_X_AXIS]) {
	    gen_tics(FIRST_X_AXIS, grid_selection & (GRID_X | GRID_MX),
		     xtick_callback);
	}

	if (*axis_label[FIRST_X_AXIS].text) {
	    /* label at xaxis_y + 1/4 of (xaxis_y-other_y) */
	    double step = (xaxis_y - other_end) / 4;
	    unsigned int x1, y1;

	    map3d_xyz(mid_x, xaxis_y + step, base_z, &v1);
	    if (!tic_in) {
		v1.x -= tic_unitx * ticscale * (t->h_tic);
		v1.y -= tic_unity * ticscale * (t->v_tic);
	    }
	    TERMCOORD(&v1, x1, y1);
	    x1 += axis_label[FIRST_X_AXIS].xoffset * t->h_char;
	    y1 += axis_label[FIRST_X_AXIS].yoffset * t->v_char;
	    /* write_multiline mods it */
	    write_multiline(x1, y1, axis_label[FIRST_X_AXIS].text, CENTRE, JUST_TOP, 0, axis_label[FIRST_X_AXIS].font);
	}
    }

    /* y axis: */
    if (axis_tics[FIRST_Y_AXIS] || *axis_label[FIRST_Y_AXIS].text) {
	vertex v0, v1;
	double other_end =
	    min_array[FIRST_X_AXIS] + max_array[FIRST_X_AXIS] - yaxis_x;
	double mid_y =
	    (max_array[FIRST_Y_AXIS] + min_array[FIRST_Y_AXIS]) / 2;

	map3d_xyz(yaxis_x, mid_y, base_z, &v0);
	map3d_xyz(other_end, mid_y, base_z, &v1);
	{			/* take care over unsigned quantities */
	    double dx = v1.x - v0.x;
	    double dy = v1.y - v0.y;
	    double len = sqrt(dx * dx + dy * dy);
	    
	    if (len != 0) {
		tic_unitx = dx / len / xscaler;
		tic_unity = dy / len / yscaler;
	    } else {
		tic_unitx = tic_unity = 0;
	    }
	}
	if (axis_tics[FIRST_Y_AXIS]) {
	    gen_tics(FIRST_Y_AXIS, grid_selection & (GRID_Y | GRID_MY),
		     ytick_callback);
	}
	if (*axis_label[FIRST_Y_AXIS].text) {
	    double step = (other_end - yaxis_x) / 4;
	    unsigned int x1, y1;

	    map3d_xyz(yaxis_x - step, mid_y, base_z, &v1);
	    if (!tic_in) {
		v1.x -= tic_unitx * ticscale * (t->h_tic);
		v1.y -= tic_unity * ticscale * (t->v_tic);
	    }
	    TERMCOORD(&v1, x1, y1);
	    x1 += axis_label[FIRST_Y_AXIS].xoffset * t->h_char;
	    y1 += axis_label[FIRST_Y_AXIS].yoffset * t->v_char;
	    /* write_multiline mods it */
	    write_multiline(x1, y1, axis_label[FIRST_Y_AXIS].text, CENTRE, JUST_TOP, 0, axis_label[FIRST_Y_AXIS].font);
	}
    }

    /* do z tics */
    if (axis_tics[FIRST_Z_AXIS]
	&& (draw_surface || (draw_contour & CONTOUR_SRF))) {
	gen_tics(FIRST_Z_AXIS, grid_selection & (GRID_Z | GRID_MZ),
		 ztick_callback);
    }
    if ((axis_zeroaxis[FIRST_Y_AXIS].l_type >= -2)
	&& !log_array[FIRST_X_AXIS]
	&& inrange(0, min_array[FIRST_X_AXIS], max_array[FIRST_X_AXIS])
	) {
	vertex v1, v2;

	/* line through x=0 */
	map3d_xyz(0.0, min_array[FIRST_Y_AXIS], base_z, &v1);
	map3d_xyz(0.0, max_array[FIRST_Y_AXIS], base_z, &v2);
	draw3d_line(&v1, &v2, &axis_zeroaxis[FIRST_Y_AXIS]);
    }
    if ((axis_zeroaxis[FIRST_X_AXIS].l_type >= -2)
	&& !log_array[FIRST_Y_AXIS]
	&& inrange(0, min_array[FIRST_Y_AXIS], max_array[FIRST_Y_AXIS])
	) {
	vertex v1, v2;

	term_apply_lp_properties(&axis_zeroaxis[FIRST_X_AXIS]);
	/* line through y=0 */
	map3d_xyz(min_array[FIRST_X_AXIS], 0.0, base_z, &v1);
	map3d_xyz(max_array[FIRST_X_AXIS], 0.0, base_z, &v2);
	draw3d_line(&v1, &v2, &axis_zeroaxis[FIRST_X_AXIS]);
    }
    /* PLACE ZLABEL - along the middle grid Z axis - eh ? */
    if (*axis_label[FIRST_Z_AXIS].text
	&& (draw_surface || (draw_contour & CONTOUR_SRF))
	) {
	map3d_xy(zaxis_x, zaxis_y,
		 max_array[FIRST_Z_AXIS]
		 + (max_array[FIRST_Z_AXIS] - base_z) / 4,
		 &x, &y);

	x += axis_label[FIRST_Z_AXIS].xoffset * t->h_char;
	y += axis_label[FIRST_Z_AXIS].yoffset * t->v_char;

	write_multiline(x, y, axis_label[FIRST_Z_AXIS].text,
			CENTRE, CENTRE, 0,
			axis_label[FIRST_Z_AXIS].font);
    }
}

static void
xtick_callback(axis, place, text, grid)
AXIS_INDEX axis;
double place;
char *text;
struct lp_style_type grid;	/* linetype or -2 for none */
{
    vertex v1, v2;
    double scale = (text ? ticscale : miniticscale);
    double other_end =
	min_array[FIRST_Y_AXIS] + max_array[FIRST_Y_AXIS] - xaxis_y;
    int dirn = tic_in ? 1 : -1;
    register struct termentry *t = term;

    map3d_xyz(place, xaxis_y, base_z, &v1);
    if (grid.l_type > -2) {
	/* to save mapping twice, map non-axis y */
	map3d_xyz(place, other_end, base_z, &v2);
	draw3d_line(&v1, &v2, &grid);
    }
    if ((axis_tics[FIRST_X_AXIS] & TICS_ON_AXIS)
	&& !log_array[FIRST_Y_AXIS]
	&& inrange (0.0, min_array[FIRST_Y_AXIS], max_array[FIRST_Y_AXIS])
	) {
	map3d_xyz(place, 0.0, base_z, &v1);
    }
    v2.x = v1.x + tic_unitx * scale * (t->h_tic) * dirn;
    v2.y = v1.y + tic_unity * scale * (t->v_tic) * dirn;
    /* FIXME HBB 20000617: I have no real idea whether this z value is
     * correct... */
    v2.z = v1.z;
    draw3d_line(&v1, &v2, &border_lp);
    term_apply_lp_properties(&border_lp);
	
    if (text) {
	int just;
	unsigned int x2, y2;

	if (tic_unitx * xscaler < -0.9)
	    just = LEFT;
	else if (tic_unitx * xscaler < 0.9)
	    just = CENTRE;
	else
	    just = RIGHT;
	v2.x = v1.x - tic_unitx * (t->h_char) * 1;
	v2.y = v1.y - tic_unity * (t->v_char) * 1;
	if (!tic_in) {
	    v2.x -= tic_unitx * (t->h_tic) * ticscale;
	    v2.y -= tic_unity * (t->v_tic) * ticscale;
	}
	TERMCOORD(&v2, x2, y2);
	clip_put_text_just(x2, y2, text, just);
    }

    if (axis_tics[FIRST_X_AXIS] & TICS_MIRROR) {
	map3d_xyz(place, other_end, base_z, &v1);
	v2.x = v1.x - tic_unitx * scale * (t->h_tic) * dirn;
	v2.y = v1.y - tic_unity * scale * (t->v_tic) * dirn;
	v2.z = v1.z;
	draw3d_line(&v1, &v2, &border_lp);
    }
}

static void
ytick_callback(axis, place, text, grid)
AXIS_INDEX axis;
double place;
char *text;
struct lp_style_type grid;
{
    vertex v1, v2;
    double scale = (text ? ticscale : miniticscale);
    double other_end =
	min_array[FIRST_X_AXIS] + max_array[FIRST_X_AXIS] - yaxis_x;
    int dirn = tic_in ? 1 : -1;
    register struct termentry *t = term;

    map3d_xyz(yaxis_x, place, base_z, &v1);
    if (grid.l_type > -2) {
	map3d_xyz(other_end, place, base_z, &v2);
	draw3d_line(&v1, &v2, &grid);
    }
    if (axis_tics[FIRST_Y_AXIS] & TICS_ON_AXIS
	&& !log_array[FIRST_X_AXIS]
	&& inrange (0.0, min_array[FIRST_X_AXIS], max_array[FIRST_Y_AXIS])
	) {
	map3d_xyz(0.0, place, base_z, &v1);
    }
    
    v2.x = v1.x + tic_unitx * scale * dirn * (t->h_tic);
    v2.y = v1.y + tic_unity * scale * dirn * (t->v_tic);
    /* FIXME HBB 20000716: (see xtick_callback()) */
    v2.z = v1.z;
    draw3d_line(&v1, &v2, &border_lp);

    if (text) {
	int just;
	unsigned int x2, y2;

	if (tic_unitx * xscaler < -0.9)
	    just = LEFT;
	else if (tic_unitx * xscaler < 0.9)
	    just = CENTRE;
	else
	    just = RIGHT;
	v2.x = v1.x - tic_unitx * (t->h_char) * 1;
	v2.y = v1.y - tic_unity * (t->v_char) * 1;
	if (!tic_in) {
	    v2.x -= tic_unitx * (t->h_tic) * ticscale;
	    v2.y -= tic_unity * (t->v_tic) * ticscale;
	}
	TERMCOORD(&v2, x2, y2);
	clip_put_text_just(x2, y2, text, just);
    }

    if (axis_tics[FIRST_Y_AXIS] & TICS_MIRROR) {
	map3d_xyz(other_end, place, base_z, &v1);
	v2.x = v1.x - tic_unitx * scale * (t->h_tic) * dirn;
	v2.y = v1.y - tic_unity * scale * (t->v_tic) * dirn;
	v2.z = v1.z;
	draw3d_line(&v1, &v2, &border_lp);
    }
}

static void
ztick_callback(axis, place, text, grid)
AXIS_INDEX axis;
double place;
char *text;
struct lp_style_type grid;
{
/* HBB: inserted some ()'s to shut up gcc -Wall, here and below */
    int len = (text ? ticscale : miniticscale)
	* (tic_in ? 1 : -1) * (term->h_tic);
    vertex v1, v2, v3;

    map3d_xyz(zaxis_x, zaxis_y, place, &v1);
    if (grid.l_type > -2) {
	map3d_xyz(back_x, back_y, place, &v2);
	map3d_xyz(right_x, right_y, place, &v3);
	draw3d_line(&v1, &v2, &grid);
	draw3d_line(&v2, &v3, &grid);
    }
    v2.x = v1.x + len / xscaler;
    v2.y = v1.y;
    v2.z = v1.z;
    draw3d_line(&v1, &v2, &border_lp);

    if (text) {
	unsigned int x1, y1;
	
	TERMCOORD(&v1, x1, y1);
	x1 -= (term->h_tic) * 2;
	if (!tic_in)
	    x1 -= (term->h_tic) * ticscale;
	clip_put_text_just(x1, y1, text, RIGHT);
    }

    if (axis_tics[FIRST_Z_AXIS] & TICS_MIRROR) {
	map3d_xyz(right_x, right_y, place, &v1);
	v2.x = v1.x - len / xscaler;
	v2.y = v1.y;
	v2.z = v1.z;
	draw3d_line(&v1, &v2, &border_lp);
    }
}


void
map3d_position(pos, x, y, what)
struct position *pos;
unsigned int *x, *y;
const char *what;
{
    double xpos = pos->x;
    double ypos = pos->y;
    double zpos = pos->z;
    int screens = 0;		/* need either 0 or 3 screen co-ordinates */

    switch (pos->scalex) {
    case first_axes:
    case second_axes:
	xpos = axis_log_value_checked(FIRST_X_AXIS, xpos, what);
	break;
    case graph:
	xpos = min_array[FIRST_X_AXIS] +
	    xpos * (max_array[FIRST_X_AXIS] - min_array[FIRST_X_AXIS]);
	break;
    case screen:
	++screens;
    }

    switch (pos->scaley) {
    case first_axes:
    case second_axes:
	ypos = axis_log_value_checked(FIRST_Y_AXIS, ypos, what);
	break;
    case graph:
	ypos = min_array[FIRST_Y_AXIS] +
	    ypos * (max_array[FIRST_Y_AXIS] - min_array[FIRST_Y_AXIS]);
	break;
    case screen:
	++screens;
    }

    switch (pos->scalez) {
    case first_axes:
    case second_axes:
	zpos = axis_log_value_checked(FIRST_Z_AXIS, zpos, what);
	break;
    case graph:
	zpos = min_array[FIRST_Z_AXIS] +
	    zpos * (max_array[FIRST_Z_AXIS] - min_array[FIRST_Z_AXIS]);
	break;
    case screen:
	++screens;
    }

    if (screens == 0) {
	map3d_xy(xpos, ypos, zpos, x, y);
	return;
    }
    if (screens != 3) {
	graph_error("Cannot mix screen co-ordinates with other types");
    } 
    {
	register struct termentry *t = term;
	*x = pos->x * (t->xmax) + 0.5;
	*y = pos->y * (t->ymax) + 0.5;
    }

    return;
}


/*
 * these code blocks were moved to functions, to make the code simpler
 */

static void
key_text(xl, yl, text)
int xl, yl;
char *text;
{
    if (key_just == JLEFT && key == KEY_AUTO_PLACEMENT) {
	(*term->justify_text) (LEFT);
	(*term->put_text) (xl + key_text_left, yl, text);
    } else {
	if ((*term->justify_text) (RIGHT)) {
	    if (key == KEY_USER_PLACEMENT)
		clip_put_text(xl + key_text_right, yl, text);
	    else
		(*term->put_text) (xl + key_text_right, yl, text);
	} else {
	    int x = xl + key_text_right - (term->h_char) * strlen(text);
	    if (key == KEY_USER_PLACEMENT) {
		if (i_inrange(x, xleft, xright))
		    clip_put_text(x, yl, text);
	    } else {
		(*term->put_text) (x, yl, text);
	    }
	}
    }
}

static void
key_sample_line(xl, yl)
int xl, yl;
{
    if (key == KEY_AUTO_PLACEMENT) {
	(*term->move) (xl + key_sample_left, yl);
	(*term->vector) (xl + key_sample_right, yl);
    } else {
	clip_move(xl + key_sample_left, yl);
	clip_vector(xl + key_sample_right, yl);
    }
}

static void
key_sample_point(xl, yl, pointtype)
int xl, yl;
int pointtype;
{
    /* HBB 20000412: fixed incorrect clipping: the point sample was
     * clipped against the graph box, even if in 'below' or 'outside'
     * position. But the result of that clipping was utterly ignored,
     * because the 'else' part did exactly the same thing as the
     * 'then' one. Some callers of this routine thus did their own
     * clipping, which I removed, along with this change.
     *
     * Now, all 'automatically' placed cases will never be clipped,
     * only user-specified ones. */
    if ((key == KEY_AUTO_PLACEMENT)            /* ==-1 means auto-placed key */
	|| !clip_point(xl + key_point_offset, yl)) {
	(*term->point) (xl + key_point_offset, yl, pointtype);
    }
}
