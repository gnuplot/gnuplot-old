/* GNUPLOT - pm3d.c */

/*[
 *
 * Petr Mikulik, December 1998 -- November 1999
 * Copyright: open source as much as possible
 *
 * 
 * What is here: global variables and routines for the pm3d splotting mode
 * This file is included only if PM3D is defined
 *
]*/

#ifdef HAVE_CONFIG_H
#   include "config.h"
#endif

#ifdef PM3D

#include "plot.h"
#include "pm3d.h"
#include "setshow.h" /* for surface_rot_z */
#include "term_api.h" /* for lp_use_properties() */


/********************************************************************/

/*
  Global options for pm3d algorithm (to be accessed by set / show)
*/

pm3d_struct pm3d = {
    "",                   /* where[6] */
    PM3D_FLUSH_BEGIN,     /* flush */
    PM3D_SCANS_AUTOMATIC, /* scans direction is determined automatically */
    PM3D_CLIP_1IN,        /* clipping: at least 1 point in the ranges */
    0, 0,                 /* use zmin, zmax from `set zrange` */
    0.0, 100.0,           /* pm3d's zmin, zmax */
    0,                    /* no pm3d hidden3d is drawn */
    0,                    /* solid (off by default, that means `transparent') */
};



/* global variables */
double used_pm3d_zmin, used_pm3d_zmax;


/****************************************************************/
/* Now the routines which are really those exactly for pm3d.c
*/

/* declare variables and routines from external files */
extern struct surface_points *first_3dplot;

void map3d_xy(double x, double y, double z, /* from graph3d.c */
    unsigned int *xt, unsigned int *yt);

extern double min_array[], max_array[];


/*
   Check and set the z-range for use by pm3d
   Return 0 on wrong range, otherwise 1
 */
int set_pm3d_zminmax ()
{
    extern double log_base_array[];
    extern TBOOLEAN is_log_z;
    if (!pm3d.pm3d_zmin)
	used_pm3d_zmin = min_array[FIRST_Z_AXIS];
    else {
	used_pm3d_zmin = pm3d.zmin;
	if (is_log_z) {
	    if (used_pm3d_zmin<0) {
		fprintf(stderr,"pm3d: log of negative z-min?\n");
		return 0;
	    }
	    used_pm3d_zmin = log(used_pm3d_zmin)/log_base_array[FIRST_Z_AXIS];
	}
    }
    if (!pm3d.pm3d_zmax)
	used_pm3d_zmax = max_array[FIRST_Z_AXIS];
    else {
	used_pm3d_zmax = pm3d.zmax;
	if (is_log_z) {
	    if (used_pm3d_zmax<0) {
		fprintf(stderr,"p3md: log of negative z-max?\n");
		return 0;
	    }
	    used_pm3d_zmax = log(used_pm3d_zmax)/log_base_array[FIRST_Z_AXIS];
	}
    }
    if (used_pm3d_zmin == used_pm3d_zmax) {
	fprintf(stderr,"pm3d: colouring requires not equal zmin and zmax\n");
	return 0;
    }
    if (used_pm3d_zmin > used_pm3d_zmax) { /* exchange min and max values */
	double tmp = used_pm3d_zmax;
	used_pm3d_zmax = used_pm3d_zmin;
	used_pm3d_zmin = tmp;
    }
    return 1;
}


/*
   Rescale z into the interval [0,1]. It's OK also for logarithmic z axis too
 */
double z2gray ( double z )
{
    if ( z <= used_pm3d_zmin ) return 0;
    if ( z >= used_pm3d_zmax ) return 1;
    z = ( z - used_pm3d_zmin ) / ( used_pm3d_zmax - used_pm3d_zmin );
    return z;
}





/*
   Now the implementation of the pm3d (s)plotting mode
 */

void
pm3d_plot(struct surface_points* this_plot, char at_which_z)
{
    int i, j, ii, from, curve, scan, up_to, up_to_minus, invert = 0;
    struct iso_curve *scanA, *scanB;
    struct coordinate GPHUGE *pointsA, *pointsB;
    struct iso_curve **scan_array;
    double avgZ, gray;
    gpdPoint corners[4];
    extern double base_z, ceiling_z; /* defined in graph3d.c */

    if (this_plot == NULL)
	return;

    if (at_which_z != PM3D_AT_BASE && at_which_z != PM3D_AT_TOP && at_which_z != PM3D_AT_SURFACE)
	return;

    /* return if the terminal does not support filled polygons */
    if (!term->filled_polygon) return;

    switch (at_which_z) {
	case PM3D_AT_BASE:
	    corners[0].z = corners[1].z = corners[2].z = corners[3].z = base_z;
	    break;
	case PM3D_AT_TOP:
	    corners[0].z = corners[1].z = corners[2].z = corners[3].z = ceiling_z;
	    break;
	    /* the 3rd possibility is surface, PM3D_AT_SURFACE, and it'll come later */
    }

    scanA = this_plot->iso_crvs;
    curve = 0;

    /* loop over scans in one surface
       Scans are linked from this_plot->iso_crvs in the opposite order than
       they are in the datafile.
       Therefore it is necessary to make vector scan_array of iso_curves.
       Scans are sorted in scan_array according to pm3d.direction (this can
       be PM3D_SCANS_FORWARD or PM3D_SCANS_BACKWARD).
     */
    scan_array = malloc( this_plot->num_iso_read * sizeof(scanA) );
    scanA = this_plot->iso_crvs;

    if (pm3d.direction == PM3D_SCANS_AUTOMATIC) {
	/* check the y ordering between scans */
	if (scanA && scanA->p_count) {
	    scanB = scanA->next;
	    if (scanB && scanB->p_count) {
		if (scanB->points[0].y < scanA->points[0].y)
		    invert = 1;
		else
		    invert = 0;
	    }
	}
    }

    for ( scan=this_plot->num_iso_read, i=0; --scan>=0; ) {
	if (pm3d.direction == PM3D_SCANS_AUTOMATIC) {
	    double angle = fmod(surface_rot_z, 360.0);
	    switch (invert) {
		case 1:
		    if (angle > 90.0 && angle < 270.0) {
			scan_array[scan] = scanA;
		    } else {
			scan_array[i++] = scanA;
		    }
		    break;
		case 0:
		default:
		    if (angle > 90.0 && angle < 270.0) {
			scan_array[i++] = scanA;
		    } else {
			scan_array[scan] = scanA;
		    }
		    break;
	    }
	}
	else if (pm3d.direction == PM3D_SCANS_FORWARD)
	    scan_array[scan] = scanA;
	else /* PM3D_SCANS_BACKWARD: i counts scans */
	    scan_array[ i++ ] = scanA;
	scanA = scanA->next;
    }

#if 0
    /* debugging: print scan_array */
    for ( scan=0; scan<this_plot->num_iso_read; scan++ ) {
	printf("**** SCAN=%d  points=%d\n", scan, scan_array[scan]->p_count );
    }
#endif

#if 0
    /* debugging: this loop prints properties of all scans */
    for ( scan=0; scan<this_plot->num_iso_read; scan++ ) {
	struct coordinate GPHUGE *points;
	scanA = scan_array[scan];
	printf( "\n#IsoCurve = scan nb %d, %d points\n#x y z type(in,out,undef)\n", scan, scanA->p_count );
	for ( i = 0, points = scanA->points; i < scanA->p_count; i++ ) {
	    printf("%g %g %g %c\n",
		points[i].x, points[i].y, points[i].z,
		points[i].type == INRANGE ? 'i' : points[i].type == OUTRANGE ? 'o' : 'u');
	    /* Note: INRANGE, OUTRANGE, UNDEFINED */
	}
    }
    printf("\n");
#endif

    if (pm3d.hidden3d_tag) {
	struct lp_style_type lp;
	lp_use_properties(&lp, pm3d.hidden3d_tag, 1);
	term_apply_lp_properties(&lp);
    }

    if (pm3d.direction == PM3D_SCANS_AUTOMATIC) {
	double angle = fmod(surface_rot_z, 360.0);
	int rev = (range_flags[FIRST_X_AXIS] ^ range_flags[FIRST_Y_AXIS]) & RANGE_REVERSE;
	if ((angle > 180.0 && !rev) || (angle < 180.0 && rev)) {
	    invert = 1;
	} else {
	    invert = 0;
	}
    }

    /*
       this loop does the pm3d draw of joining two curves

       How the loop below works:
     * scanB = scan last read; scanA = the previous one
     * link the scan from A to B, then move B to A, then read B, then draw
     */
    for ( scan=0; scan<this_plot->num_iso_read-1; scan++ ) {
	scanA = scan_array[scan];
	scanB = scan_array[scan+1];
#if 0
	printf( "\n#IsoCurveA = scan nb %d has %d points   ScanB has %d points\n", scan, scanA->p_count, scanB->p_count );
#endif
	pointsA = scanA->points; pointsB = scanB->points;
	/* if the number of points in both scans is not the same, then the starting
	   index (offset) of scan B according to the flushing setting has to be
	   determined
	 */
	from = 0; /* default is pm3d.flush==PM3D_FLUSH_BEGIN */
	if (pm3d.flush == PM3D_FLUSH_END)
	    from = abs( scanA->p_count - scanB->p_count );
	else if (pm3d.flush == PM3D_FLUSH_CENTER)
	    from = abs( scanA->p_count - scanB->p_count ) / 2;
	/* find the minimal number of points in both scans */
	up_to = GPMIN(scanA->p_count,scanB->p_count) - 1;
	/* go over the minimal number of points from both scans.
Notice: if it would be once necessary to go over points in `backward'
direction, then the loop body below would require to replace the data
point indices `i' by `up_to-i' and `i+1' by `up_to-i-1'.
	 */
	up_to_minus = up_to - 1; /* calculate only once */
	for ( j = 0; j < up_to; j++ ) {
	    i = j;
	    if (PM3D_SCANS_AUTOMATIC == pm3d.direction && invert) {
		i = up_to_minus - j;
	    }
	    ii = i + from; /* index to the B array */
	    /* choose the clipping method */
	    if (pm3d.clip == PM3D_CLIP_4IN) {
		/* (1) all 4 points of the quadrangle must be in x and y range */
		if (!( pointsA[i].type == INRANGE && pointsA[i+1].type == INRANGE &&
			pointsB[ii].type == INRANGE && pointsB[ii+1].type == INRANGE ))
		    continue;
	    }
	    else { /* (pm3d.clip == PM3D_CLIP_1IN) */
		/* (2) all 4 points of the quadrangle must be defined */
		if ( pointsA[i].type == UNDEFINED || pointsA[i+1].type == UNDEFINED ||
		    pointsB[ii].type == UNDEFINED || pointsB[ii+1].type == UNDEFINED )
		    continue;
		/* and at least 1 point of the quadrangle must be in x and y range */
		if ( pointsA[i].type == OUTRANGE && pointsA[i+1].type == OUTRANGE &&
		    pointsB[ii].type == OUTRANGE && pointsB[ii+1].type == OUTRANGE )
		    continue;
	    }
#ifdef EXTENDED_COLOR_SPECS
	    if (!supply_extended_color_specs) {
#endif
	    /* get the gray as the average of the corner z positions (note: log already in)
	       I always wonder what is faster: d*0.25 or d/4? Someone knows? -- 0.25 (joze) */
	    avgZ = ( pointsA[i].z + pointsA[i+1].z + pointsB[ii].z + pointsB[ii+1].z ) * 0.25;
	    /* transform z value to gray, i.e. to interval [0,1] */
	    gray = z2gray ( avgZ );
	    /* print the quadrangle with the given colour */
#if 0
	    printf( "averageZ %g\tgray=%g\tM %g %g L %g %g L %g %g L %g %g\n",
		avgZ,
		gray,
		pointsA[i].x, pointsA[i].y,
		pointsB[ii].x, pointsB[ii].y,
		pointsB[ii+1].x, pointsB[ii+1].y,
		pointsA[i+1].x, pointsA[i+1].y );
#endif
	    set_color( gray );
#ifdef EXTENDED_COLOR_SPECS
	    }
#endif
	    corners[0].x = pointsA[i].x;    corners[0].y = pointsA[i].y;
	    corners[1].x = pointsB[ii].x;   corners[1].y = pointsB[ii].y;
	    corners[2].x = pointsB[ii+1].x; corners[2].y = pointsB[ii+1].y;
	    corners[3].x = pointsA[i+1].x;  corners[3].y = pointsA[i+1].y;
	    if (at_which_z == PM3D_AT_SURFACE
#ifdef EXTENDED_COLOR_SPECS
	    || supply_extended_color_specs
#endif
		) {
		/* always supply the z value if
		 * EXTENDED_COLOR_SPECS is defined
		 */
		corners[0].z = pointsA[i].z;
		corners[1].z = pointsB[ii].z;
		corners[2].z = pointsB[ii+1].z;
		corners[3].z = pointsA[i+1].z;
	    }

	    /* filled_polygon( 4, corners ); */
	    filled_quadrangle( corners );
	} /* loop over points of two subsequent scans */
    } /* loop over scans */
    /* printf("\n"); */

    /* free memory allocated by scan_array */
    free( scan_array );

} /* end of pm3d splotting mode */



/*
   Now the implementation of the filled colour contour plot

contours_where: equals either CONTOUR_SRF or CONTOUR_BASE

Note: z2gray() uses used_pm3d_zmin, used_pm3d_zmax
Check that if accessing this routine otherwise then via `set pm3d at`
code block in graph3d.c
 */

void filled_color_contour_plot ( this_plot, contours_where )
    struct surface_points *this_plot;
    int contours_where;
{
    double gray;
    extern double base_z; /* defined in graph3d.c */
    struct gnuplot_contours *cntr;

    if (this_plot == NULL || this_plot->contours == NULL)
	return;
    if (contours_where != CONTOUR_SRF && contours_where != CONTOUR_BASE)
	return;

    /* return if the terminal does not support filled polygons */
    if (!term->filled_polygon) return;

    /* TODO: CHECK FOR NUMBER OF POINTS IN CONTOUR: IF TOO SMALL, THEN IGNORE! */
    cntr = this_plot->contours;
    while (cntr) {
	printf("# Contour: points %i, z %g, label: %s\n", cntr->num_pts, cntr->coords[0].z, (cntr->label)?cntr->label:"<no>");
	if (cntr->isNewLevel) {
	    printf("\t...it isNewLevel\n");
	    /* contour split across chunks */
	    /* fprintf(gpoutfile, "\n# Contour %d, label: %s\n", number++, c->label); */
	    /* What is the colour? */
	    /* get the z-coordinate */
	    /* transform contour z-coordinate value to gray, i.e. to interval [0,1] */
	    gray = z2gray(cntr->coords[0].z);
	    set_color(gray);
	}
	/* draw one countour */
	if (contours_where == CONTOUR_SRF) /* at CONTOUR_SRF */
	    filled_polygon_3dcoords ( cntr->num_pts, cntr->coords );
	else /* at CONTOUR_BASE */
	    filled_polygon_3dcoords_zfixed ( cntr->num_pts, cntr->coords, base_z );
	/* next contour */
	cntr = cntr->next;
    }
} /* end of filled colour contour plot splot mode */

void
pm3d_reset(void)
{
    pm3d.where[0] = 0;
    pm3d.flush = PM3D_FLUSH_BEGIN;
    pm3d.direction = PM3D_SCANS_AUTOMATIC;
    pm3d.clip = PM3D_CLIP_1IN;
    pm3d.pm3d_zmin = 0;
    pm3d.pm3d_zmax = 0;
    pm3d.zmin = 0.0;
    pm3d.zmax = 100.0;
    pm3d.hidden3d_tag = 0;
    pm3d.solid = 0;
}

/* DRAW PM3D ALL COLOUR SURFACES */
void pm3d_draw_all(struct surface_points* plots, int pcount)
{
    int i = 0;
    int surface;
    extern FILE *gpoutfile;
    struct surface_points *this_plot = NULL;

    if (!strcmp(term->name,"postscript") || !strcmp(term->name,"pstex"))
	fprintf(gpoutfile,"%%pm3d_map_begin\n"); /* for pm3dCompress.awk */
    for ( ; pm3d.where[i]; i++ ) {
	this_plot = plots;
	for (surface = 0;
	    surface < pcount;
	    this_plot = this_plot->next_sp, surface++)
	    pm3d_plot(this_plot, pm3d.where[i]);
    }

    if (strchr(pm3d.where,'C') != NULL)
	/* !!!!! CONTOURS, UNDOCUMENTED
	   !!!!! LATER CHANGE TO STH LIKE (if_filled_contours_requested)
	   !!!!! ... */
	for (this_plot = plots; this_plot; this_plot = this_plot->next_sp) {
	    if (draw_contour & CONTOUR_SRF)
		filled_color_contour_plot(this_plot, CONTOUR_SRF);
	    if (draw_contour & CONTOUR_BASE)
		filled_color_contour_plot(this_plot, CONTOUR_BASE);
	}

    if (!strcmp(term->name,"postscript") || !strcmp(term->name,"pstex"))
	fprintf(gpoutfile,"%%pm3d_map_end\n"); /* for pm3dCompress.awk */

    /* draw colour box */
    draw_color_smooth_box();

    /* release the palette we have made use of (some terminals may need this)
       ...no, remove this, also remove it from plot.h !!!!
     */
    if (term->previous_palette)
	term->previous_palette();
}

/* eof pm3d.c */

#endif
