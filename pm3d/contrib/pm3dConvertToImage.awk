# pm3dConvertToImage.awk
# by Petr Mikulik, mikulik@physics.muni.cz
# Version: 11. 5. 2000
#
# This awk script tries to compress a postscript file created by pm3d or
# gnuplot with pm3d splotting mode. If the input data formed a rectangular
# equidistant map (matrix), then its postscript representation is converted
# into an image operator with 8bit gray (i.e., no more colour). This conversion
# makes the image 20 times smaller.
#
# Usage:
#    gnuplot>set out "|awk -f pm3dConvertToImage.awk >image.ps"
# or
#    your_shell>awk -f pm3dConvertToImage.awk <map.ps >image.ps
#
# Distribution policy: this script belongs to the pm3d and gnuplot programs.
#
# Notes:
#  - works for 8bit grayscale images only
#  - future versions should make fully use of the 'G' definition.
#    But how to read data and make the figure? No more use of 'image'
#    operator, user-made reading? Can a postscript guru help?
#  - no usage of run length encoding etc. 

BEGIN {
err = "/dev/stderr"

if (ARGC!=1) {
  print "pm3dConvertToImage.awk --- (c) Petr Mikulik, Brno. Version 11. 5. 2000" >err
  print "Compression of matrix-like pm3d .ps files into 8bit gray image. See also header" >err
  print "of this script for more info." >err
  print "Usage:\n\t[stdout | ] awk -f pm3dConvertToImage.awk [<inp_file.ps] >out_file.ps" >err
  print "Example for gnuplot:" >err
  print "\tset out \"|awk -f pm3dConvertToImage.awk >smaller.ps\"" >err
  print "Hint: the region to be converted is between %pm3d_map_begin and %pm3d_map_end" >err
  print "keywords. Rename them to avoid converting specified region." >err
  error = -1
  exit(1)
}

# Setup of main variables.
inside_pm3d_map = 0
pm3d_images = 0

# The following global variables will be used:
error=0
pm3dline=0
scans=0; scan_pts=0; scans_in_x=0
x1=0; y1 = 0; cell_x=0; cell_y=0
x2=0; y2 = 0; x2last=0; y2last=0
}


########################################
# Start pm3d map region.

!inside_pm3d_map && $1 == "%pm3d_map_begin" {
inside_pm3d_map = 1
pm3d_images++
# initialize variables for the image description
pm3dline = 0
scans = 1
x2 = -29999.123; y2 = -29999.123
next
}


########################################
# Outside pm3d map regin.

!inside_pm3d_map {
if ($1 == "%%Creator:")
    print $0 ", compressed by pm3dConvertToImage.awk"
else print
next
}

########################################
# End of pm3d map region: write all.

$1 == "%pm3d_map_end" {
inside_pm3d_map = 0

if (scans_in_x) { grid_y = scan_pts; grid_x = scans; }
  else  { grid_x = scan_pts; grid_y = scans; }

print "Info on pm3d image region number " pm3d_images ": grid " grid_x " x " grid_y >err
print "\tpoints: " pm3dline "  scans: " scans "  start point: " x1","y1 "  end point: " x2","y2 >err

# write image header
print "%pm3d_image_begin"
print "gsave"
print "/readstring {currentfile exch readhexstring pop} bind def"
print "/picstr " grid_x " string def"


if (x1 > x2) { x1+=cell_x; x2+=cell_x; }
if (y1 > y2) { y1+=cell_y; y2+=cell_y; }

if (x1 < x2) {
  # scansforward case
  print x1 " " y1 " translate " (x1-x2)*(grid_y/(grid_y-1)) " " (y1-y2)*(grid_x/(grid_x-1)) " scale"
  print "-90 rotate"
  printf grid_x " " grid_y " 8 [ "grid_x " 0 0 -"grid_y " 0 0] "
} else {
  # scansbackward case
  print x1 " " y1 " translate " (x1-x2)*(grid_y/(grid_y-1)) " " (y2-y1)*(grid_x/(grid_x-1))  " scale"
  print "-90 rotate"
  printf grid_x " " grid_y " 8 [-"grid_x " 0 0 -"grid_y " 0 0] "
}

printf "{picstr readstring} "
print "image"


if (scan_pts*scans != pm3dline) {
  print "ERROR: pm3d image is not a grid, exiting." >err
  error=1
  exit(8)
}

# write the substituted image stuff
for (i=1; i<=scans; i++) 
  print row[i];

# write the tail of the image environment
print "grestore"
print "%pm3d_image_end"

next
}


########################################
# Read in the pm3d map/image data.

{
if (NF!=12 || toupper($2)!="G" || $5!="N") {
	print "ERROR: Wrong (non-pm3d map) data on line " NR ", exiting." >err
	error=1
	exit(8)
}

pm3dline++;

if (pm3dline==1) { # first point of the map
	x1=$3; y1=$4; cell_x=$8;
	x2=x1; y2=y1; cell_y=$9;
} else {
	x2last=x2; y2last=y2;	# remember previous point
	x2=$3; y2=$4;		# remember the current point
}

if (pm3dline==2) { # determine whether data are scans in x or in y
    if (y1==y2) { # y=const, scan in x
	scan_in_x = 1;
	if (x1==x2) { 
	    print "ERROR: First two points are identical?! Exiting." >err
	    error=1
	    exit(5)
	}
    } else { # x=const, scan in y
	if (x1!=x2) {
		print "ERROR: Map is obviously not rectangular, exiting." >err
		error=1
		exit(5)
	}
	scan_in_x = 0;
    }
}

if ( pm3dline>2 && ((scan_in_x && y2!=y2last) || (!scan_in_x && x2!=x2last)) ) {
	if (scans==1) scan_pts = pm3dline-1
	scans++
	row[scans] = ""
}

# now remember the intensity
row[scans] = row[scans] sprintf( "%02X", $1*255 );
next
} # reading map/image data


########################################
# The end.

END {
if (error == 0 && inside_pm3d_map) {
    print "ERROR: Corrupted pm3d block:  \"%pm3d_map_end\"  not found." >err
    error=1
}
if (error==0) {
    if (pm3d_images==0) 
	print "No pm3d image found in the input file." >err
    else
	print "There were " pm3d_images " pm3d image(s) found in the input file." >err
} else if (error>0) {
	    print "An ERROR has been reported. This file is INCORRECT."
	    print "An ERROR has been reported. Output file is INCORRECT." >err
	}
}


# eof pm3dConvertToImage.awk
