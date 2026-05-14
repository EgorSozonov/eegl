# Makefile for Eegl on Unix and Unix-like systems
#
# This Makefile is loosely based on the GNU Makefile conventions found in standards.info.
#
# Compiling Eegl, summary:
#
#	3. make
#	5. make install
#
# Compiling Eegl, details:
#
# Edit this file for adjusting to your system. You should not need to edit any
# other file for machine specific things!
# The name of this file MUST be Makefile (note the uppercase 'M').
#{{{ config

VIEWNAME	= view

CC		= gcc
DEFS	= -DHAVE_CONFIG_H
#CFLAGS = -gdwarf-5 -gsplit-dwarf -Winline -Wfatal-errors -Werror=pointer-integer-compare \
#         -O0  -Werror=return-type -D_REENTRANT \
#         -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1 -fno-pie
#CFLAGS		= -O2  -D_REENTRANT  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
CPPFLAGS	= 
srcdir		= .

LDFLAGS		= -L/usr/local/lib -Wl,--as-needed -no-pie
LIBS		= -lm -ltinfo -lattr
TAGPRG		= ctags

CPP		= gcc -E
CPP_MM		= M
DEPEND_FLAGS_FILTER = | sed 's+-I */+-isystem /+g'
LINK_AS_NEEDED	= yes
X_FLAGS	=  
X_LIBS_DIR	=  
X_PRE_LIBS	=  -lSM -lICE -lXpm
X_EXTRA_LIBS	=  -lXdmcp -lSM -lICE
X_LIBS		= -lXt -lX11

WAYLAND_LIBS    = -lwayland-client
WAYLAND_SRC	=        libs/wayland/wlr-data-control-unstable-v1.c       libs/wayland/ext-data-control-v1.c \
        libs/wayland/xdg-shell.c       libs/wayland/primary-selection-unstable-v1.c       src/wayland.c
WAYLAND_OBJ	=        .b/wlr-data-control-unstable-v1.o .b/ext-data-control-v1.o \
        .b/xdg-shell.o .b/primary-selection-unstable-v1.o .b/wayland.o
WAYLAND_FLAGS    = 



CHANNEL_SRC	= channel.c
CHANNEL_OBJ	= .b/channel.o
TERM_TEST	= test_libvterm


AWK		= gawk

STRIP		= strip

EXEEXT		= 
CROSS_COMPILING = 

COMPILEDBY	= 

INSTALLVIMDIFF	= installvimdiff
INSTALLGVIMDIFF	= installgvimdiff
INSTALL_LANGS	= install-languages
INSTALL_TOOL_LANGS	= install-tool-languages

### Line break character as octal number for "tr"
NL		= "\\012"

### Top directory for everything
prefix		= /usr/local

### Top directory for the binary
exec_prefix	= ${prefix}

### Prefix for location of data files
BINDIR		= ${exec_prefix}/bin

### For autoconf 2.60 and later (avoid a warning)
datarootdir	= ${prefix}/share

### Prefix for location of data files
DATADIR		= ${datarootdir}

### Prefix for location of man pages
MANDIR		= ${datarootdir}/man



### Any OS dependent extra source and object file
OS_EXTRA_SRC	= 
OS_EXTRA_OBJ	= 

### If the *.po files are to be translated to *.mo files.
MAKEMO		= yes

MSGFMT		= msgfmt
MSGFMTCMD	= OLD_PO_FILE_INPUT=yes msgfmt --no-convert -v
MSGFMT_DESKTOP	= eegl.desktop

### set if $SOURCE_DATE_EPOCH was set when running configure
BUILD_DATE_MSG	= 


# Make sure that "make first" will run "make all" once configure has done its
# work.  This is needed when using the Makefile in the top directory. first: all

#}}}
#
# 1. Edit this Makefile  {{{1
#	The defaults for Eegl should work on most machines, but you may want to
#	uncomment some lines or make other changes below to tune it to your
#	system, compiler or preferences.  Uncommenting means that the '#' in
#	the first column of a line is removed.
#	- If you want a version of Eegl that is small and starts up quickly,
#	  you might want to disable the GUI, X11, Perl, Python and Tcl.
#	- Uncomment the line "CONF_OPT_X = --without-x" if you have X11 but
#	  want to disable using X11 libraries.	This speeds up starting Eegl,
#	  but the window title will not be set and the X11 selection can not
#	  be used.
#	- Uncomment the line "CONF_OPT_XSMP = --disable-xsmp" if you have the
#	  X11 Session Management Protocol (XSMP) library (libSM) but do not
#	  want to use it.
#	  This can speedup Eegl startup but Eegl loses the ability to catch the
#	  user logging out from session-managers like GNOME and work
#	  could be lost.
#	- Uncomment one of the lines with --with-features= to enable a set of
#	  features (but not the interfaces just mentioned).
#	- Uncomment the line with --disable-gpm to disable gpm support
#	  even though you have gpm libraries and includes.
#	- Uncomment the line with --disable-sysmouse to disable sysmouse
#	  support even though you have /dev/sysmouse and includes.
#	- Uncomment one of the lines with CFLAGS and/or CC if you have
#	  something very special or want to tune the optimizer.
#	- Search for the name of your system to see if it needs anything special.
#	- A few versions of make use '.include "file"' instead of 'include
#	  file'.  Adjust the include line below if yours does.
#
# 2. Edit feature.h  {{{1
#	Only if you do not agree with the default compile features, e.g.:
#	- you want Eegl to be as vi compatible as it can be
#	- you want to use Emacs tags files
#	- you want right-to-left editing (Hebrew)
#	- you want 'langmap' support (Greek)
#	- you want to remove features to make Eegl smaller
#
# 3. "make"  {{{1
#	Will first run ./configure with the options in this file. Then it will
#	start make again on this Makefile to do the compiling. You can also do
#	this in two steps with:
#		make config
#		make
#	The configure script is created with "make autoconf".  It can detect
#	different features of your system and act accordingly.  However, it is
#	not correct for all systems.  Check this:
#	- If you have X windows, but configure could not find it or reported
#	  another include/library directory then you wanted to use, you have
#	  to set CONF_OPT_X below.  You might also check the installation of
#	  xmkmf.
#	- If you have --enable-gui=motif and have Motif on your system, but
#	  configure reports "checking for location of gui... <not found>", you
#	  have to set GUI_INC_LOC and GUI_LIB_LOC below.
#	If you changed something, do this to run configure again:
#		make reconfig
#
#	- If you get error messages, find out what is wrong and try to correct
#	  it in this Makefile. You may need to do "make reconfig" when you
#	  change anything that configure uses (e.g. switching from an old C
#	  compiler to an ANSI C compiler). Only when auto/configure does
#	  something wrong you may need to change one of the other files. If
#	  you find a clean way to fix the problem, consider sending a note to
#	  the author of autoconf (bug-gnu-utils@prep.ai.mit.edu) or Eegl
#	  (vim-dev@vim.org). Don't bother to do that when you made a hack
#	  solution for a non-standard system.
#
# 4. "make test"  {{{1
#	This is optional.  This will run Eegl scripts on a number of test
#	files, and compare the produced output with the expected output.
#	If all is well, you will get the "ALL DONE" message in the end.  If a
#	test fails you get "TEST FAILURE".  See below (search for "/^test").
#
# 5. "make install"  {{{1
#	If the new Eegl seems to be working OK you can install it and the
#	documentation in the appropriate location. The default is
#	"/usr/local".  Change "prefix" below to change the location.
#	Note that any existing executable is removed or overwritten.  If you
#	want to keep it you will have to make a backup copy first.
#	The runtime files are in a different directory for each version.  You
#	might want to delete an older version.
#	If you don't want to install everything, there are other targets:
#		make installvim		only installs Eegl, not the tools
#		make installvimbin	only installs the Eegl executable
#		make installruntime	installs most of the runtime files
#		make installrtbase	only installs the Eegl help and runtime files
#		make installlinks	only installs the Eegl binary links
#		make installmanlinks	only installs the Eegl manpage links
#		make installmacros	only installs the Eegl macros
#		make installpack	only installs the packages
#		make installtutorbin	only installs the Eegl tutor program
#		make installtutor	only installs the Eegl tutor files
#		make installspell	only installs the spell files
#	If you install Eegl, not to install for real but to prepare a package
#	or RPM, set DESTDIR to the root of the tree.
#
# 6. Use Eegl until a new version comes out.  {{{1
#
# 7. "make uninstall_runtime"  {{{1
#	Will remove the runtime files for the current version.	This is safe
#	to use while another version is being used, only version-specific
#	files will be deleted.
#	To remove the runtime files of another version:
#		make uninstall_runtime VIMRTDIR=/vim54
#	If you want to delete all installed files, use:
#		make uninstall
#	Note that this will delete files that have the same name for any
#	version, thus you might need to do a "make install" soon after this.
#	Be careful not to remove a version of Eegl that is still being used!
#	To find out which files and directories will be deleted, use:
#		make -n uninstall
# }}}
#
### This Makefile has been successfully tested on many systems. {{{
### Only the ones that require special options are mentioned here.
### Check the (*) column for remarks, listed below.
### Later code changes may cause small problems, otherwise Eegl is supposed to
### compile and run without problems.

#system:	      configurations:		     version (*) tested by:
#-------------	      ------------------------	     -------  -  ----------
#AIX 3.2.5	      cc (not gcc)   -			4.5  (M) Will Fiveash
#AIX 4		      cc	     +X11 -GUI		3.27 (4) Axel Kielhorn
#AIX 4.1.4	      cc	     +X11 +GUI		4.5  (5) Nico Bakker
#AIX 4.2.1	      cc				5.2k (C) Will Fiveash
#AIX 4.3.3.12	      xic 3.6.6				5.6  (5) David R. Favor
#A/UX 3.1.1	      gcc	     +X11		4.0  (6) Jim Jagielski
#BSDI 2.1 (x86)       shlicc2 gcc-2.6.3 -X11 X11R6	4.5  (1) Jos Backus
#BSD/OS 3.0 (x86)     gcc gcc-2.7.2.1 -X11 X11R6	4.6c (1) Jos Backus
#CX/UX 6.2	      cc	     +X11 +GUI_Mofif	5.4  (V) Kipp E. Howard
#DG/UX 5.4*	      gcc 2.5.8      GUI		5.0e (H) Jonas Schlein
#DG/UX 5.4R4.20       gcc 2.7.2      GUI		5.0s (H) Rocky Olive
#HP-UX (most)	      c89 cc				5.1  (2) Bram Moolenaar
#HP-UX_9.04	      cc	     +X11 +Motif	5.0  (2) Carton Lao
#Linux 2.0	      gcc-2.7.2      Infomagic Motif	4.3  (3) Ronald Rietman
#NEC UP4800 UNIX_SV 4.2MP  cc	     +X11R6 Motif	4.6b (Q) Lennart Schultz
#NetBSD 1.0A	      gcc-2.4.5      -X11 -GUI		3.21 (X) Juergen Weigert
#QNX 4.2	      wcc386-10.6    -X11		4.2  (D) G.F. Desrochers
#QNX 4.23	      Watcom	     -X11		4.2  (F) John Oleynick
#SCO Unix v3.2.5      cc	     +X11 Motif		3.27 (C) M. Kuperblum
#SCO Open Server 5    gcc 2.7.2.3    +X11 +GUI Motif	5.3  (A) Glauber Ribeiro
#SINIX-N 5.43 RM400 R4000   cc	     +X11 +GUI		5.0l (I) Martin Furter
#SINIX-Z 5.42 i386    gcc 2.7.2.3    +X11 +GUI Motif	5.1  (I) Joachim Fehn
#SINIX-Y 5.43 RM600 R4000  gcc 2.7.2.3 +X11 +GUI Motif	5.1  (I) Joachim Fehn
#Reliant/SINIX 5.44   cc	     +X11 +GUI		5.5a (I) B. Pruemmer
#SNI Targon31 TOS 4.1.11 gcc-2.4.5   +X11 -GUI		4.6c (B) Paul Slootman
#Solaris 2.4 (Sparc)  cc	     +X11 +GUI		3.29 (9) Glauber
#Solaris 2.4/2.5      clcc	     +X11 -GUI openwin	3.20 (7) Robert Colon
#Solaris 2.5 (sun4m)  cc (SC4.0)     +X11R6 +GUI (CDE)	4.6b (E) Andrew Large
#Solaris 2.5	      gcc 2.5.6      +X11 Motif		5.0m (R) Ant. Colombo
#Solaris 2.6	      gcc 2.8.1      ncurses		5.3  (G) Larry W. Virden
#Solaris with -lthread					5.5  (W) K. Nagano
#Solaris	      gcc				     (b) Riccardo
#SunOS 4.1.x			     +X11 -GUI		5.1b (J) Bram Moolenaar
#SUPER-UX 6.2 (NEC SX-4) cc	     +X11R6 Motif	4.6b (P) Lennart Schultz
#Tandem/NSK						     (c) Matthew Woehlke
#Unisys 6035	      cc	     +X11 Motif		5.3  (8) Glauber Ribeiro
#ESIX V4.2	      cc	     +X11		6.0  (a) Reinhard Wobst
# }}}

# (*)  Remarks: {{{
#
# (1)  Uncomment line below for shlicc2
# (2)  HPUX with compile problems or wrong digraphs, uncomment line below
# (3)  Infomagic Motif needs GUI_LIB_LOC and GUI_INC_LOC set, see below.
#      And add "-lXpm" to MOTIF_LIBS2.
# (4)  For cc the optimizer must be disabled (use CFLAGS= after running
#      configure) (symptom: ":set termcap" output looks weird).
# (5)  Compiler may need extra argument, see below.
# (6)  See below for a few lines to uncomment
# (7)  See below for lines which enable the use of clcc
# (8)  Needs some EXTRA_LIBS, search for Unisys below
# (9)  Needs an extra compiler flag to compile gui_at_sb.c, see below.
# (A)  May need EXTRA_LIBS, see below
# (B)  Can't compile GUI because there is no waitpid()...  Disable GUI below.
# (C)  Force the use of curses instead of termcap, see below.
# (D)  Uncomment lines below for QNX
# (E)  You might want to use termlib instead of termcap, see below.
# (F)  See below for instructions.
# (G)  Using ncurses version 4.2 has reported to cause a crash.  Use the
#      Sun curses library instead.
# (H)  See line for EXTRA_LIBS below.
# (I)  SINIX-N 5.42 and 5.43 need some EXTRA_LIBS.  Also for Reliant-Unix.
# (J)  If you get undefined symbols, see below for a solution.
# (K)  See lines to uncomment below for machines with 64 bit pointers.
# (M)  gcc version cygnus-2.0.1 does NOT work (symptom: "dl" deletes two
#      characters instead of one).
# (N)  SCO with decmouse.
# (O)  LynxOS needs EXTRA_LIBS, see below.
# (P)  For SuperUX 6.2 on NEC SX-4 see a few lines below to uncomment.
# (Q)  For UNIXSVR 4.2MP on NEC UP4800 see below for lines to uncomment.
# (R)  For Solaris 2.5 (or 2.5.1) with gcc > 2.5.6, uncomment line below.
# (U)  Must uncomment CONF_OPT_PYTHON option below to disable Python
#      detection, since the configure script runs into an error when it
#      detects Python (probably because of the bash shell).
# (V)  See lines to uncomment below.
# (Y)  See line with c89 below
# (Z)  See lines with cc or c89 below
# (a)  See line with EXTRA_LIBS below.
# (b)  When using gcc with the Solaris linker, make sure you don't use GNU
#      strip, otherwise the binary may not run: "Cannot find ELF".
# (c)  Add -lfloss to EXTRA_LIBS, see below.
# (x)  When you get warnings for precompiled header files, run
#      "sudo fixPrecomps".  Also see CONF_OPT_DARWIN below.
# }}}


#DO NOT CHANGE the next line, we need it for configure to find the compiler
#instead of using the default from the "make" program.
#Use a line further down to change the value for CC.
CC=

# Argument for running ctags.
TAGS_FILES = *.c *.h

# Change and use these defines if configure cannot find your Motif stuff.

srcdir = src 
EEGLNAME = eegl
VIEWNAME = view

#{{{what used to be auto/config.mk

EXNAME		= eegl
VIEWNAME	= view

CC		= gcc
DEFS		= -DHAVE_CONFIG_H
CFLAGS	= --std=c17 -gdwarf-5 -gsplit-dwarf -Wall -Wextra -Wfatal-errors -O0 \
              -Wno-cpp -Werror=return-type -D_REENTRANT -Werror=pointer-compare \
              -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1 -fno-pie
        
INDICES_FLAGS	= --std=c17 -Wfatal-errors -g3 -O0 -Wno-cpp -Werror=return-type
#C_FLAGS		= -O2  -D_REENTRANT  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
CPPFLAGS	= 
srcdir		= .

LDFLAGS		= -L/usr/local/lib -Wl,--as-needed -no-pie
LIBS		= -lm -ltinfo -lattr
TAGPRG		= ctags

CPP		= gcc -E
CPP_MM	= M
DEPEND_FLAGS_FILTER = | sed 's;-I */;-isystem /;g'
LINK_AS_NEEDED	= yes
X_FLAGS	=  
X_LIBS_DIR	=  
X_PRE_LIBS	=  -lSM -lICE -lXpm
X_EXTRA_LIBS	=  -lXdmcp -lSM -lICE
X_LIBS		= -lXt -lX11

WAYLAND_LIBS    = -lwayland-client
WAYLAND_SRC	=        libs/wayland/wlr-data-control-unstable-v1.c       libs/wayland/ext-data-control-v1.c \
        libs/wayland/xdg-shell.c       libs/wayland/primary-selection-unstable-v1.c       src/wayland.c
WAYLAND_OBJ	=        .b/wlr-data-control-unstable-v1.o .b/ext-data-control-v1.o \
        .b/xdg-shell.o .b/primary-selection-unstable-v1.o .b/wayland.o
WAYLAND_FLAGS    = 

XDIFF_OBJS_USED	= $(XDIFF_OBJS)


CHANNEL_SRC	= channel.c
CHANNEL_OBJ	= .b/channel.o
TERM_SRC	= libvterm/src/encoding.c libvterm/src/keyboard.c libvterm/src/mouse.c libvterm/src/parser.c libvterm/src/pen.c libvterm/src/creen.c libvterm/src/state.c libvterm/src/unicode.c libvterm/src/vterm.c
TERM_OBJ	= .b/vterm_encoding.o .b/vterm_keyboard.o .b/vterm_mouse.o .b/vterm_parser.o .b/vterm_pen.o .b/vterm_screen.o .b/vterm_state.o .b/vterm_unicode.o .b/vterm_vterm.o
TERM_TEST	= test_libvterm


AWK		= gawk

STRIP		= strip

EXEEXT		= 
CROSS_COMPILING = 

COMPILEDBY	= 

INSTALLVIMDIFF	= installvimdiff
INSTALLGVIMDIFF	= installgvimdiff
INSTALL_LANGS	= install-languages
INSTALL_TOOL_LANGS	= install-tool-languages

### sed command to fix quotes while creating pathdef.c
QUOTESED        = sed -e 's/[\\"]/\\&/g' -e 's/\\"/"/' -e 's/\\";$$/";/' -e 's/  */ /g'

### Line break character as octal number for "tr"
NL		= "\\012"

### Top directory for everything
prefix		= /usr/local

### Top directory for the binary
exec_prefix	= ${prefix}

### Prefix for location of data files
BINDIR		= ${exec_prefix}/bin

### For autoconf 2.60 and later (avoid a warning)
datarootdir	= ${prefix}/share

### Prefix for location of data files
DATADIR		= ${datarootdir}

### Prefix for location of man pages
MANDIR		= ${datarootdir}/man



### Any OS dependent extra source and object file
OS_EXTRA_SRC	= 
OS_EXTRA_OBJ	= 

### If the *.po files are to be translated to *.mo files.
MAKEMO		= yes

MSGFMT		= msgfmt
MSGFMTCMD	= OLD_PO_FILE_INPUT=yes msgfmt --no-convert -v
MSGFMT_DESKTOP	= gvim.desktop vim.desktop

### set if $SOURCE_DATE_EPOCH was set when running configure
BUILD_DATE_MSG	= 


# Make sure that "make first" will run "make all" once configure has done its
# work.  This is needed when using the Makefile in the top directory.
first: all

#}}}

# Include the configuration choices first, so we can override everything
# below. As shipped, this file contains a target that causes to run
# configure. Once configure was run, this file contains a list of
# make variables with predefined values instead. Thus any second invocation
# of make, will build Eegl.

# CONFIGURE - configure arguments {{{1
# You can give a lot of options to configure.
# Change this to your desire and do 'make config' afterwards

# examples you can uncomment:
#CONF_ARGS1 = --exec-prefix=/usr
#CONF_ARGS2 = --with-vim-name=vim8 --with-ex-name=ex8 --with-view-name=view8
#CONF_ARGS3 = --with-global-runtime=/etc/vim,/usr/share/vim
#CONF_ARGS4 = --with-local-dir=/usr/share
#CONF_ARGS5 = --without-local-dir

# Use this one if you distribute a modified version of Eegl.
#CONF_ARGS6 = --with-modified-by="John Doe"

# Uncomment one of these lines if you have that GUI but don't want to use it.
# The automatic check will use another one that can be found.
# Gnome is disabled by default, because it may cause trouble.

# Uncomment one of these lines to select a specific GUI to use.
# When using "yes" or nothing, configure will use the first one found: GTK+,
# or Motif.
#
# GTK versions that are known not to work 100% are rejected.
# Use "--disable-gtktest" to accept them anyway.
# For GTK 1 use Eegl 7.2.
#
# GNOME means GTK with Gnome support.  If using GTK and --enable-gnome-check
# is used then GNOME will automatically be used if it is found.  If you have
# GNOME, but do not want to use it (e.g., want a GTK-only version), then use
# --enable-gui=gtk or leave out --enable-gnome-check.
#
# GNOME makes sense only for GTK+ 2.  Avoid use of --enable-gnome-check with
# GTK+ 3 build, as the functionality of GNOME is already incorporated into
# GTK+ 3.
#

# DARWIN - detecting Mac OS X
# Uncomment this line when you want to compile a Unix version of Eegl on
# Darwin.  None of the Mac specific options or files will be used.
#CONF_OPT_DARWIN = --disable-darwin

# Select the architecture supported.  Default is to build for the current
# platform.  Use "both" for a universal binary.  That probably doesn't work
# when including Perl, Python, etc.
# NOTE: ppc probably doesn't work anymore,
#CONF_OPT_DARWIN = --with-mac-arch=intel
#CONF_OPT_DARWIN = --with-mac-arch=ppc
#CONF_OPT_DARWIN = --with-mac-arch=both

# Uncomment the next line to fail if one of the requested language interfaces
# cannot be configured.  Without this Eegl will be build anyway, without
# the failing interfaces.
#CONF_OPT_FAIL = --enable-fail-if-missing

# CSCOPE
# Uncomment this when you want to include the Cscope interface.
#CONF_OPT_CSCOPE = --enable-cscope

# TERMINAL - Terminal emulator support, :terminal command.  Requires the
# channel feature. The default is enable for when using "huge" features.
# Uncomment the first line when you want terminal emulator support for
# not-huge builds.  Uncomment the second line when you don't want terminal
# emulator support in the huge build.
#CONF_OPT_TERMINAL = --enable-terminal
#CONF_OPT_TERMINAL = --disable-terminal

# MULTIBYTE - To edit multi-byte characters.
# This is now always enabled.

# When building with "huge" features, right-left and Arabic
# features are enabled.  Use this to disable them.
CONF_OPT_MULTIBYTE = --disable-rightleft --disable-arabic

# NLS - National Language Support
# Uncomment this when you do not want to support translated messages, even
# though configure can find support for it.
#CONF_OPT_NLS = --disable-nls

# XIM - X Input Method.  Special character input support for X11 (Chinese,
# Japanese, special symbols, etc).  Also needed for dead-key support.
# When omitted it's automatically enabled for the X-windows GUI.
#CONF_OPT_INPUT = --enable-xim
#CONF_OPT_INPUT = --disable-xim

# FONTSET - X fontset support for output of languages with many characters.
# Uncomment this when you want to output a multibyte language.
#CONF_OPT_OUTPUT = --enable-fontset

# gpm - For mouse support on Linux console via gpm
# Uncomment this when you do not want to include gpm support, even
# though you have gpm libraries and includes.
# For Debian/Ubuntu gpm support requires the libgpm-dev package.
#CONF_OPT_GPM = --disable-gpm
# Use this to enable dynamic loading of the GPM library.
#CONF_OPT_GPM = --enable-gpm=dynamic

# sysmouse - For mouse support on FreeBSD and DragonFly console via sysmouse
# Uncomment this when you do not want do include sysmouse support, even
# though you have /dev/sysmouse and includes.
#CONF_OPT_SYSMOUSE = --disable-sysmouse

# libcanberra - For sound support.  Default is on for huge features.
# Uncomment one of the two to chose otherwise.
# CONF_OPT_CANBERRA = --enable-canberra
# CONF_OPT_CANBERRA = --disable-canberra


# FEATURES - For creating Eegl with more or less features
# Uncomment one of these lines when you want to include few to many features.
# The default is "huge" for most systems.
#CONF_OPT_FEAT = --with-features=tiny
#CONF_OPT_FEAT = --with-features=normal
#CONF_OPT_FEAT = --with-features=huge

# COMPILED BY - For including a specific e-mail address for ":version".
#CONF_OPT_COMPBY = "--with-compiledby=John Doe <JohnDoe@yahoo.com>"

# X WINDOWS DISABLE - For creating a plain Eegl without any X11 related fancies
# (otherwise Eegl configure will try to include xterm titlebar access)
# Also disable the GUI above, otherwise it will be included anyway.
# When both GUI and X11 have been disabled this may save about 15% of the
# code and make Eegl startup quicker.
#CONF_OPT_X = --without-x

# X WINDOWS DIRECTORY - specify X directories
# If configure can't find you X stuff, or if you have multiple X11 derivatives
# installed, you may wish to specify which one to use.
# Select nothing to let configure choose.
# This here selects openwin (as found on sun).
#XROOT = /usr/openwin
#CONF_OPT_X = --x-include=$(XROOT)/include --x-libraries=$(XROOT)/lib

# X11 Session Management Protocol support
# Eegl will try to use XSMP to catch the user logging out if there are unsaved
# files.  Uncomment this line to disable that (it prevents vim trying to open
# communications with the session manager).
#CONF_OPT_XSMP = --disable-xsmp

# You may wish to include xsmp but use exclude xsmp-interact if the logout
# XSMP functionality does not work well with your session-manager (at time of
# writing, this would be early GNOME-1 gnome-session: it 'freezes' other
# applications after Eegl has cancelled a logout (until Eegl quits).  This
# *might* be the Eegl code, but is more likely a bug in early GNOME-1.
# This disables the dialog that asks you if you want to save files or not.
#CONF_OPT_XSMP = --disable-xsmp-interact

# If you want to always automatically add a servername, also in the terminal.
#CONF_OPT_AUTOSERVE = --enable-autoservername

# COMPILER - Name of the compiler {{{1
# The default from configure will mostly be fine, no need to change this, just
# an example. If a compiler is defined here, configure will use it rather than
# probing for one. It is dangerous to change this after configure was run.
# Make will use your choice then -- but beware: Many things may change with
# another compiler.  It is wise to run 'make reconfig' to start all over
# again.
#CC = cc
CC = gcc
#CC = clang

# COMPILER FLAGS - change as you please. Either before running {{{1
# configure or afterwards. For examples see below.
# When using -g with some older versions of Linux you might get a
# statically linked executable.
# When not defined, configure will try to use -O2 for gcc and -O for others.
#C_FLAGS = -g
#C_FLAGS = -O


# Often used for GCC: mixed optimizing, lot of optimizing, debugging
#C_FLAGS = -g -O2 -fno-strength-reduce -Wall -Wshadow -Wmissing-prototypes
#C_FLAGS = -g -O2 -fno-strength-reduce -Wall -Wmissing-prototypes
#C_FLAGS = -g -Wall -Wmissing-prototypes
#C_FLAGS = -O6 -fno-strength-reduce -Wall -Wshadow -Wmissing-prototypes
#C_FLAGS = -g -DDEBUG -Wall -Wshadow -Wmissing-prototypes
#C_FLAGS = -g -O2 '-DSTARTUPTIME="vimstartup"' -fno-strength-reduce -Wall -Wmissing-prototypes

# Use this with GCC to check for mistakes, unused arguments, etc.
# Note: If you use -Wextra and get warnings in GTK code about function
#       parameters, you can add -Wno-cast-function-type (but not with clang)
#C_FLAGS = -g -Wall -Wextra -Wshadow -Wmissing-prototypes -Wunreachable-code -Wno-cast-function-type -Wno-deprecated-declarations -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
#C_FLAGS = -g -Wall -Wextra -Wshadow -Wmissing-prototypes -Wunreachable-code -Wno-deprecated-declarations -D_REENTRANT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
# Add -Wpedantic to find // comments and other C99 constructs.
# Better disable Perl and Python to avoid a lot of warnings.
#C_FLAGS = -g -Wall -Wextra -Wshadow -Wmissing-prototypes -Wpedantic -Wunreachable-code -Wunused-result -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
#C_FLAGS = -g -O2 -Wall -Wextra -Wshadow -Wmissing-prototypes -Wpedantic -Wunreachable-code -Wno-cast-function-type -Wunused-result -Wno-deprecated-declarations -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1

# EFENCE - Electric-Fence malloc debugging: catches memory accesses beyond
# allocated memory (and makes every malloc()/free() very slow).
# Electric Fence is free (search ftp sites).
# You may want to set the EF_PROTECT_BELOW environment variable to check the
# other side of allocated memory.
# On FreeBSD you might need to enlarge the number of mmaps allowed.  Do this
# as root: sysctl -w vm.max_proc_mmap=30000
#EXTRA_LIBS = /usr/local/lib/libefence.a

# Autoconf binary.
AUTOCONF ?= autoconf

# PURIFY - remove the # to use the "purify" program (hoi Nia++!)
#PURIFY = purify

# VALGRIND - remove the # to use valgrind for memory leaks and access errors.
#	     Used for the unittest targets.
# VALGRIND = valgrind --tool=memcheck --leak-check=yes --num-callers=25 --log-file=valgrind.$@

# }}}

# LINT - for running lint
#  For standard Unix lint
LINT = lint
LINT_OPTIONS = -beprxzF
#  For splint
#  It doesn't work well, crashes on include files and non-ascii characters.
#LINT = splint
#LINT_OPTIONS = +unixlib -weak -macrovarprefixexclude -showfunc -linelen 9999

# PROFILING - Uncomment the next two lines to do profiling with gcc and gprof.
# Might not work with GUI or Perl.
# After running Eegl see the profile result with: gprof vim gmon.out | vim -
# Need to recompile everything after changing this: "make clean" "make".
#PROFILE_FLAGS = -pg -g -DWE_ARE_PROFILING
#PROFILE_LIBS = -pg

# GCC 5 and later need the -no-pie argument.
#PROFILE_LIBS = -pg -no-pie

# For unknown reasons adding "-lc" fixes a linking problem with some versions
# of GCC.  That's probably a bug in the "-pg" implementation.
#PROFILE_LIBS = -pg -lc


# TEST COVERAGE - Uncomment the two lines below the explanation to get code
# coverage information. (provided by Yegappan Lakshmanan)
# 1. make clean, run configure and build Eegl as usual.
# 2. Generate the baseline code coverage information:
#	$ lcov -c -i -b . -d objects -o .b/coverage_base.info
# 3. Run "make test" to run the unit tests.  The code coverage information will
#    be generated in the src/objects directory.
# 4. Generate the code coverage information from the tests:
#	$ lcov -c -b . -d .b/ -o .b/coverage_test.info
# 5. Combine the baseline and test code coverage data:
#	$ lcov -a .b/coverage_base.info -a objects/coverage_test.info -o objects/coverage_total.info
# 6. Process the test coverage data and generate a report in html:
#	$ genhtml .b/coverage_total.info -o objects
# 7. Open the .b/index.html file in a web browser to view the coverage
#    information.
#
# LDFLAGS=--coverage
# PROFILE_FLAGS=-g -O0 -fprofile-arcs -ftest-coverage -DWE_ARE_PROFILING -DUSE_GCOV_FLUSH
# Alternate flags
# PROFILE_FLAGS=-g -O0 --coverage -DWE_ARE_PROFILING -DUSE_GCOV_FLUSH


#Uncomment the next lines to compile Eegl on GCC with the address sanitizer (asan) and
#with the undefined sanitizer.
#You should also use -DEXITFREE to avoid false reports.
#May make Eegl twice as slow.  Errors are reported on stderr.
#More at: https://code.google.com/p/address-sanitizer/
#Useful environment variables:
# $ export ASAN_OPTIONS="print_stacktrace=1 log_path=asan"
# $ export LSAN_OPTIONS="suppressions=`pwd`/tests/lsan-suppress.txt"
# When running tests output can be found in tests/asan.*
#SANITIZER_FLAGS = -g -O0 -fsanitize-recover=all \
#		   -fsanitize=address -fsanitize=undefined \
#		   -fno-omit-frame-pointer

# Similarly when compiling with clang and using ubsan.
# $ export UBSAN_OPTIONS="print_stacktrace=1 log_path=ubsan"
# $ export LSAN_OPTIONS="suppressions=`pwd`/tests/lsan-suppress.txt"
# When running tests output can be found in tests/ubsan.*
#SANITIZER_FLAGS = -g -O0  -fsanitize-recover=all -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer

SANITIZER_LIBS = $(SANITIZER_FLAGS)

# MEMORY LEAK DETECTION
# Requires installing the ccmalloc library.
# Configuration is in the .ccmalloc or ~/.ccmalloc file.
# Doesn't work very well, since memory linked to from global variables
# (in libraries) is also marked as leaked memory.
#LEAK_FLAGS = -DEXITFREE
#LEAK_LIBS = -lccmalloc

# Uncomment this line to have Eegl call abort() when an internal error is
# detected.  Useful when using a tool to find errors.
#ABORT_FLAGS = -DABORT_ON_INTERNAL_ERROR


### Names of the programs and targets  {{{1
EEGLTARGET	= bin/$(EEGLNAME)$(EXEEXT)
VIEWTARGET	= $(VIEWNAME)$(LNKEXT)
EEGLDIFFNAME	= $(EEGLNAME)diff
VIMDIFFTARGET	= $(EEGLDIFFNAME)$(LNKEXT)

### Names of the tools that are also made  {{{1

### Installation directories.  The defaults come from configure. {{{1
#
### prefix	the top directory for the data (default "/usr/local")
#
# Uncomment the next line to install Eegl in your home directory.
#prefix = $(HOME)

### exec_prefix	is the top directory for the executable (default $(prefix))
#
# Uncomment the next line to install the Eegl executable in "/usr/machine/bin"
#exec_prefix = /usr/machine

### BINDIR	dir for the executable	 (default "$(exec_prefix)/bin")
### MANDIR	dir for the manual pages (default "$(prefix)/man")
### DATADIR	dir for the other files  (default "$(prefix)/lib" or
#						  "$(prefix)/share")
# They may be different when using different architectures for the
# executable and a common directory for the other files.
#
# Uncomment the next line to install Eegl in "/usr/bin"
#BINDIR   = /usr/bin
# Uncomment the next line to install Eegl manuals in "/usr/share/man/man1"
#MANDIR   = /usr/share/man
# Uncomment the next line to install Eegl help files in "/usr/share/vim"
#DATADIR  = /usr/share

### DESTDIR	root of the installation tree.  This is prepended to the other
#		directories.  This directory must exist.
#DESTDIR  = ~/pkg/vim

### Directory of the man pages
MAN1DIR = /man1

### Eegl version (adjusted by a script)
VIMMAJOR = 9
VIMMINOR = 1

### Location of Eegl files (should not need to be changed, and  {{{1
### some things might not work when they are changed!)
VIMDIR = /vim
VIMRTDIR = /vim$(VIMMAJOR)$(VIMMINOR)
HELPSUBDIR = /doc
COLSUBDIR = /colors
SYNSUBDIR = /syntax
INDSUBDIR = /indent
AUTOSUBDIR = /autoload
IMPORTSUBDIR = /import
PLUGSUBDIR = /plugin
FTPLUGSUBDIR = /ftplugin
LANGSUBDIR = /lang
COMPSUBDIR = /compiler
KMAPSUBDIR = /keymap
MACROSUBDIR = /macros
PACKSUBDIR = /pack
TOOLSSUBDIR = /tools
TUTORSUBDIR = /tutor
SPELLSUBDIR = /spell
PRINTSUBDIR = /print
PODIR = po

### VIMLOC	common root of the Eegl files (all versions)
### VIMRTLOC	common root of the runtime Eegl files (this version)
### VIMRCLOC	compiled-in location for global [g]vimrc files (all versions)
### VIMRUNTIMEDIR  compiled-in location for runtime files (optional)
### HELPSUBLOC	location for help files
### COLSUBLOC	location for colorscheme files
### SYNSUBLOC	location for syntax files
### INDSUBLOC	location for indent files
### AUTOSUBLOC	location for standard autoload files
### IMPORTSUBLOC location for standard import files
### PLUGSUBLOC	location for standard plugin files
### FTPLUGSUBLOC  location for ftplugin files
### LANGSUBLOC	location for language files
### COMPSUBLOC	location for compiler files
### KMAPSUBLOC	location for keymap files
### MACROSUBLOC	location for macro files
### PACKSUBLOC	location for packages
### TOOLSSUBLOC	location for tools files
### TUTORSUBLOC	location for tutor files
### SPELLSUBLOC	location for spell files
### PRINTSUBLOC	location for PostScript files (prolog, latin1, ..)
### SCRIPTLOC	location for script files (menu.vim, bugreport.vim, ..)
### You can override these if you want to install them somewhere else.
### Edit feature.h for compile-time settings.
VIMLOC		= $(DATADIR)$(VIMDIR)
VIMRTLOC	= $(DATADIR)$(VIMDIR)$(VIMRTDIR)
VIMRCLOC	= $(VIMLOC)
HELPSUBLOC	= $(VIMRTLOC)$(HELPSUBDIR)
COLSUBLOC	= $(VIMRTLOC)$(COLSUBDIR)
SYNSUBLOC	= $(VIMRTLOC)$(SYNSUBDIR)
INDSUBLOC	= $(VIMRTLOC)$(INDSUBDIR)
AUTOSUBLOC	= $(VIMRTLOC)$(AUTOSUBDIR)
IMPORTSUBLOC	= $(VIMRTLOC)$(IMPORTSUBDIR)
PLUGSUBLOC	= $(VIMRTLOC)$(PLUGSUBDIR)
FTPLUGSUBLOC	= $(VIMRTLOC)$(FTPLUGSUBDIR)
LANGSUBLOC	= $(VIMRTLOC)$(LANGSUBDIR)
COMPSUBLOC	= $(VIMRTLOC)$(COMPSUBDIR)
KMAPSUBLOC	= $(VIMRTLOC)$(KMAPSUBDIR)
MACROSUBLOC	= $(VIMRTLOC)$(MACROSUBDIR)
PACKSUBLOC	= $(VIMRTLOC)$(PACKSUBDIR)
TOOLSSUBLOC	= $(VIMRTLOC)$(TOOLSSUBDIR)
TUTORSUBLOC	= $(VIMRTLOC)$(TUTORSUBDIR)
SPELLSUBLOC	= $(VIMRTLOC)$(SPELLSUBDIR)
PRINTSUBLOC	= $(VIMRTLOC)$(PRINTSUBDIR)
SCRIPTLOC	= $(VIMRTLOC)

### Only set VIMRUNTIMEDIR when VIMRTLOC is set to a different location and
### the runtime directory is not below it.
#VIMRUNTIMEDIR = $(VIMRTLOC)

### Name of the defaults/evim/mswin file target.
VIM_DEFAULTS_FILE = $(DESTDIR)$(SCRIPTLOC)/defaults.vim
EVIM_FILE	= $(DESTDIR)$(SCRIPTLOC)/evim.vim
MSWIN_FILE	= $(DESTDIR)$(SCRIPTLOC)/mswin.vim

### Name of the menu file target.
SYS_MENU_FILE	= $(DESTDIR)$(SCRIPTLOC)/menu.vim
SYS_SYNMENU_FILE = $(DESTDIR)$(SCRIPTLOC)/synmenu.vim
SYS_DELMENU_FILE = $(DESTDIR)$(SCRIPTLOC)/delmenu.vim

### Name of the bugreport file target.
SYS_BUGR_FILE	= $(DESTDIR)$(SCRIPTLOC)/bugreport.vim

### Name of the file type detection file target.
SYS_FILETYPE_FILE = $(DESTDIR)$(SCRIPTLOC)/filetype.vim

### Name of the file type detection file target.
SYS_FTOFF_FILE	= $(DESTDIR)$(SCRIPTLOC)/ftoff.vim

### Name of the file type detection script file target.
SYS_SCRIPTS_FILE = $(DESTDIR)$(SCRIPTLOC)/scripts.vim

### Name of the ftplugin-on file target.
SYS_FTPLUGIN_FILE = $(DESTDIR)$(SCRIPTLOC)/ftplugin.vim

### Name of the ftplugin-off file target.
SYS_FTPLUGOF_FILE = $(DESTDIR)$(SCRIPTLOC)/ftplugof.vim

### Name of the indent-on file target.
SYS_INDENT_FILE = $(DESTDIR)$(SCRIPTLOC)/indent.vim

### Name of the indent-off file target.
SYS_INDOFF_FILE = $(DESTDIR)$(SCRIPTLOC)/indoff.vim

### Name of the option window script file target.
SYS_OPTWIN_FILE = $(DESTDIR)$(SCRIPTLOC)/optwin.vim

# Program to install the program in the target directory.  Could also be "mv".
INSTALL_PROG	= cp

### Permissions for binaries  {{{1
BINMOD = 755

### Permissions for man page
MANMOD = 644

### Permissions for help files
HELPMOD = 644

### Permissions for Perl and shell scripts
SCRIPTMOD = 755

### Permission for Eegl script files (menu.vim, bugreport.vim, ..)
VIMSCRIPTMOD = 644

### Permissions for all directories that are created
DIRMOD = 755

### Permissions for all other files that are created
FILEMOD = 644

# Where to copy the man and help files from
HELPSOURCE = ../runtime/doc

# Where to copy the script files from (menu, bugreport)
SCRIPTSOURCE = ../runtime

# Where to copy the colorscheme files from
COLSOURCE = ../runtime/colors

# Where to copy the syntax files from
SYNSOURCE = ../runtime/syntax

# Where to copy the indent files from
INDSOURCE = ../runtime/indent

# Where to copy the standard plugin files from
AUTOSOURCE = ../runtime/autoload

# Where to copy the standard import files from
IMPORTSOURCE = ../runtime/import

# Where to copy the standard plugin files from
PLUGSOURCE = ../runtime/plugin

# Where to copy the ftplugin files from
FTPLUGSOURCE = ../runtime/ftplugin

# Where to copy the macro files from
MACROSOURCE = ../runtime/macros

# Where to copy the package files from
PACKSOURCE = ../runtime/pack

# Where to copy the tools files from
TOOLSSOURCE = ../runtime/tools

# Where to copy the tutor files from
TUTORSOURCE = ../runtime/tutor

# Where to copy the spell files from
SPELLSOURCE = ../runtime/spell

# Where to look for language specific files
LANGSOURCE = ../runtime/lang

# Where to look for compiler files
COMPSOURCE = ../runtime/compiler

# Where to look for keymap files
KMAPSOURCE = ../runtime/keymap

# Where to look for print resource files
PRINTSOURCE = ../runtime/print

# Where to look translated README and LICENSE files
TRANSSOURCE = ../lang

# If you are using Linux, you might want to use this to make vim the
# default vi editor, it will create a link from vi to Eegl when doing
# "make install".  An existing file will be overwritten!
# When not using it, some make programs can't handle an undefined $(LINKIT).
#LINKIT = ln -f -s $(DEST_BIN)/$(EEGLTARGET) $(DESTDIR)/usr/bin/vi
LINKIT = @echo >/dev/null

###


### Command to create dependencies based on #include "..."
### prototype headers are ignored due to -DPROTO, system
### headers #include <...> are ignored if we use the -MM option, as
### e.g. provided by gcc-cpp.
### Need to change "-I /<path>" to "-isystem /<path>" for GCC 3.x.
CPP_DEPEND = $(CC) -I$(srcdir) -M$(CPP_MM) \
		`echo "$(DEPEND_FLAGS)" $(DEPEND_FLAGS_FILTER)`

# flags for cproto
#     This is for cproto 3 patchlevel 8 or below
#     __inline, __attribute__ and __extension__ are not recognized by cproto
#     G_IMPLEMENT_INLINES is to avoid functions defined in glib/gutils.h.
#NO_ATTR = -D__inline= -D__inline__= -DG_IMPLEMENT_INLINES \
#	  -D"__attribute__\\(x\\)=" -D"__asm__\\(x\\)=" \
#	  -D__extension__= -D__restrict="" \
#	  -D__gnuc_va_list=char -D__builtin_va_list=char
#
#     This is for cproto 3 patchlevel 9 or above (currently 4.6, 4.7g)
#     __inline and __attribute__ are now recognized by cproto
#     __attribute() is not recognized and used in X11/Intrinsic.h
#     -D"foo()=" is not supported by all compilers so do not use it
NO_ATTR = -D"__attribute\\(x\\)="
#
# Use this for cproto 3 patchlevel 6 or below (use "cproto -V" to check):
# PROTO_FLAGS = -f4 -d -E"$(CPP)" $(NO_ATTR)
#
# Use this for cproto 3 patchlevel 7 or above (use "cproto -V" to check):
PROTO_FLAGS = -d -E"$(CPP)" $(NO_ATTR)


################################################
##   no changes required below this line      ##
################################################

SHELL = /bin/sh

.SUFFIXES:
.SUFFIXES: .c .o .pro


PRE_DEFS = -Isrc/proto $(DEFS) $(CPPFLAGS) $(EXTRA_IPATHS)
POST_DEFS = $(X_FLAGS) $(EXTRA_DEFS)

ALL_FLAGS = $(PRE_DEFS) $(CFLAGS) $(PROFILE_FLAGS) $(SANITIZER_FLAGS) $(LEAK_FLAGS) \
   $(ABORT_FLAGS) $(POST_DEFS)


LINT_FLAGS = -DLINT -I. $(PRE_DEFS) $(POST_DEFS) \
	      -Dinline= -D__extension__= -Dalloca=alloca

LINT_EXTRA = -D"__attribute__(x)="

DEPEND_FLAGS = -DPROTO -DDEPEND $(LINT_FLAGS)

ALL_LIB_DIRS = $(X_LIBS_DIR)
ALL_LIBS = \
	   $(X_PRE_LIBS) \
	   $(X_LIBS) \
	   $(X_EXTRA_LIBS) \
	   $(LIBS) \
	   $(EXTRA_LIBS) \
	   $(PROFILE_LIBS) \
	   $(SANITIZER_LIBS) \
	   $(LEAK_LIBS) \
	   $(WAYLAND_LIBS)

# abbreviations
DEST_BIN = $(DESTDIR)$(BINDIR)
DEST_VIM = $(DESTDIR)$(VIMLOC)
DEST_RT = $(DESTDIR)$(VIMRTLOC)
DEST_HELP = $(DESTDIR)$(HELPSUBLOC)
DEST_COL = $(DESTDIR)$(COLSUBLOC)
DEST_SYN = $(DESTDIR)$(SYNSUBLOC)
DEST_IND = $(DESTDIR)$(INDSUBLOC)
DEST_AUTO = $(DESTDIR)$(AUTOSUBLOC)
DEST_IMPORT = $(DESTDIR)$(IMPORTSUBLOC)
DEST_PLUG = $(DESTDIR)$(PLUGSUBLOC)
DEST_FTP = $(DESTDIR)$(FTPLUGSUBLOC)
DEST_LANG = $(DESTDIR)$(LANGSUBLOC)
DEST_COMP = $(DESTDIR)$(COMPSUBLOC)
DEST_KMAP = $(DESTDIR)$(KMAPSUBLOC)
DEST_MACRO = $(DESTDIR)$(MACROSUBLOC)
DEST_PACK = $(DESTDIR)$(PACKSUBLOC)
DEST_TOOLS = $(DESTDIR)$(TOOLSSUBLOC)
DEST_TUTOR = $(DESTDIR)$(TUTORSUBLOC)
DEST_SPELL = $(DESTDIR)$(SPELLSUBLOC)
DEST_SCRIPT = $(DESTDIR)$(SCRIPTLOC)
DEST_PRINT = $(DESTDIR)$(PRINTSUBLOC)
DEST_MAN_TOP = $(DESTDIR)$(MANDIR)

# We assume that the ".../man/xx/man1/" directory is for latin1 manual pages.
# Some systems use UTF-8, but these should find the ".../man/xx.UTF-8/man1/"
# directory first.
# FreeBSD uses ".../man/xx.ISO8859-1/man1" for latin1, use that one too.
DEST_MAN = $(DEST_MAN_TOP)$(MAN1DIR)
DEST_MAN_DA = $(DEST_MAN_TOP)/da$(MAN1DIR)
DEST_MAN_DA_I = $(DEST_MAN_TOP)/da.ISO8859-1$(MAN1DIR)
DEST_MAN_DA_U = $(DEST_MAN_TOP)/da.UTF-8$(MAN1DIR)
DEST_MAN_DE = $(DEST_MAN_TOP)/de$(MAN1DIR)
DEST_MAN_DE_I = $(DEST_MAN_TOP)/de.ISO8859-1$(MAN1DIR)
DEST_MAN_DE_U = $(DEST_MAN_TOP)/de.UTF-8$(MAN1DIR)
DEST_MAN_FR = $(DEST_MAN_TOP)/fr$(MAN1DIR)
DEST_MAN_FR_I = $(DEST_MAN_TOP)/fr.ISO8859-1$(MAN1DIR)
DEST_MAN_FR_U = $(DEST_MAN_TOP)/fr.UTF-8$(MAN1DIR)
DEST_MAN_IT = $(DEST_MAN_TOP)/it$(MAN1DIR)
DEST_MAN_IT_I = $(DEST_MAN_TOP)/it.ISO8859-1$(MAN1DIR)
DEST_MAN_IT_U = $(DEST_MAN_TOP)/it.UTF-8$(MAN1DIR)
DEST_MAN_JA_U = $(DEST_MAN_TOP)/ja$(MAN1DIR)
DEST_MAN_PL = $(DEST_MAN_TOP)/pl$(MAN1DIR)
DEST_MAN_PL_I = $(DEST_MAN_TOP)/pl.ISO8859-2$(MAN1DIR)
DEST_MAN_PL_U = $(DEST_MAN_TOP)/pl.UTF-8$(MAN1DIR)
DEST_MAN_RU = $(DEST_MAN_TOP)/ru.KOI8-R$(MAN1DIR)
DEST_MAN_RU_U = $(DEST_MAN_TOP)/ru.UTF-8$(MAN1DIR)
DEST_MAN_TR = $(DEST_MAN_TOP)/tr$(MAN1DIR)
DEST_MAN_TR_I = $(DEST_MAN_TOP)/tr.ISO8859-9$(MAN1DIR)
DEST_MAN_TR_U = $(DEST_MAN_TOP)/tr.UTF-8$(MAN1DIR)


# get the list of tests
include noncode/tests/Make_all.mak

#	     BASIC_SRC: files that are always used
#	       GUI_SRC: extra GUI files for current configuration
#	   ALL_GUI_SRC: all GUI files for Unix
#
#		   SRC: files used for current configuration
#	       ALL_SRC: source files used for make depend and make lint

BASIC_SRC_NO_DIR = \
	book.c \
   channel.c \
	clipboard.c \
	data.c \
	diff.c \
	do.c \
	draw.c \
	eval.c \
	fileio.c \
	hilite.c \
	input.c \
	insert.c \
	juggle.c \
	location.c \
	main.c \
	mark.c \
	memory.c \
	message.c \
	normal.c \
	option.c \
	persist.c \
	portal.c \
	regexp.c \
	script.c \
	search.c \
	strings.c \
	tag.c \
	term.c \
	ui.c \
   window.c \
	$(OS_EXTRA_SRC)

BASIC_SRC = $(addprefix src/, $(BASIC_SRC_NO_DIR))


SRC =	$(BASIC_SRC) \
	#$(WAYLAND_SRC)

EXTRA_SRC = src/channel.c \
	    $(GRESOURCE_SRC)

$(WAYLAND_SRC):
	cd libs/wayland; $(MAKE)

# Needed for parallel jobs to work
libs/wayland/ext-data-control-v1.h: libs/wayland/ext-data-control-v1.c
libs/wayland/wlr-data-control-unstable-v1.h: libs/wayland/wlr-data-control-unstable-v1.c
libs/wayland/primary-selection-unstable-v1.h: libs/wayland/primary-selection-unstable-v1.c
libs/wayland/xdg-shell.h: libs/wayland/xdg-shell.c

# Unittest files
JSON_TEST_SRC = src/json_test.c
JSON_TEST_TARGET = src/json_test$(EXEEXT)
KWORD_TEST_SRC = src/kword_test.c
KWORD_TEST_TARGET = src/kword_test$(EXEEXT)
MEMFILE_TEST_SRC = src/memfile_test.c
MEMFILE_TEST_TARGET = src/memfile_test$(EXEEXT)
MESSAGE_TEST_SRC = src/message_test.c
MESSAGE_TEST_TARGET = src/message_test$(EXEEXT)

UNITTEST_SRC = $(JSON_TEST_SRC) $(KWORD_TEST_SRC) $(MEMFILE_TEST_SRC) $(MESSAGE_TEST_SRC)
UNITTEST_TARGETS = $(JSON_TEST_TARGET) $(KWORD_TEST_TARGET) $(MEMFILE_TEST_TARGET) $(MESSAGE_TEST_TARGET)
RUN_UNITTESTS = run_json_test run_kword_test run_memfile_test run_message_test

# All sources, also the ones that are not configured
ALL_LOCAL_SRC = $(BASIC_SRC) $(UNITTEST_SRC) $(EXTRA_SRC) $(WAYLAND_SRC)
ALL_SRC = $(ALL_LOCAL_SRC)

# Which files to check with lint.  Select one of these three lines.  ALL_SRC
# checks more, but may not work well for checking a GUI that wasn't configured.
# The perl sources also don't work well with lint.
LINT_SRC = $(BASIC_SRC) $(CHANNEL_SRC)
#LINT_SRC = $(SRC)
#LINT_SRC = $(ALL_SRC)
#LINT_SRC = $(BASIC_SRC)

OBJ_COMMON = \
	.b/book.o \
	.b/clipboard.o \
	.b/data.o \
	.b/diff.o \
	.b/do.o \
	.b/draw.o \
	.b/eval.o \
	.b/fileio.o \
	.b/hilite.o \
	.b/input.o \
	.b/insert.o \
	.b/juggle.o \
	.b/location.o \
	.b/mark.o \
	.b/normal.o \
	.b/option.o \
	.b/persist.o \
	.b/portal.o \
	.b/regexp.o \
	.b/script.o \
	.b/search.o \
	.b/tag.o \
	.b/term.o \
	.b/ui.o \
	.b/window.o \
	$(OS_EXTRA_OBJ) \
	$(CHANNEL_OBJ)

# The files included by tests are not in OBJ_COMMON.
OBJ_MAIN = \
	.b/strings.o \
	.b/main.o \
	.b/memory.o \
	.b/message.o

OBJ = $(OBJ_COMMON) $(OBJ_MAIN)

OBJ_JSON_TEST = \
	.b/strings.o \
	.b/memory.o \
	.b/message.o \
	.b/json_test.o

JSON_TEST_OBJ = $(OBJ_COMMON) $(OBJ_JSON_TEST)

OBJ_KWORD_TEST = \
	.b/strings.o \
	.b/memory.o \
	.b/message.o \
	.b/kword_test.o

KWORD_TEST_OBJ = $(OBJ_COMMON) $(OBJ_KWORD_TEST)

OBJ_MEMFILE_TEST = \
	.b/strings.o \
	.b/strings.o \
	.b/message.o \
	.b/memfile_test.o

MEMFILE_TEST_OBJ = $(OBJ_COMMON) $(OBJ_MEMFILE_TEST)

OBJ_MESSAGE_TEST = \
	.b/strings.o \
	.b/strings.o \
	.b/memory.o \
	.b/message_test.o

MESSAGE_TEST_OBJ = $(OBJ_COMMON) $(OBJ_MESSAGE_TEST)

ALL_OBJ = $(OBJ_COMMON) \
	  $(OBJ_MAIN) \
	  $(OBJ_JSON_TEST) \
	  $(OBJ_KWORD_TEST) \
	  $(OBJ_MEMFILE_TEST) \
	  $(OBJ_MESSAGE_TEST)


PRO_AUTO = \
	alloc.pro \
	book.pro \
	change.pro \
	channel.pro \
	clipboard.pro \
	dict.pro \
	diff.pro \
	do.pro \
	draw.pro \
	eval.pro \
	fileio.pro \
	hilite.pro \
	input.pro \
	insert.pro \
	juggle.pro \
	list.pro \
	location.pro \
	main.pro \
	mark.pro \
	memory.pro \
	message.pro \
	normal.pro \
	option.pro \
	unix.pro \
	persist.pro \
	portal.pro \
	regexp.pro \
	script.pro \
	search.pro \
	sound.pro \
	strings.pro \
	tag.pro \
	term.pro \
	ui.pro \
	window.pro

# Default target is making the executable and tools
all: $(EEGLTARGET) $(TOOLS) languages

tools: $(TOOLS)



# Run the script to generate the Command lookup table and the normal/visual mode command lookup 
# tables. This only needs to be run when command has been added or changed.
# If this fails because you don't have Eegl yet, first build and install Eegl without changes.
indices: src/commands.h src/actions.h
	$(CC) -I$(srcdir) $(INDICES_FLAGS) src/indices/indexGenerator.c -o .b/indexGenerator
	.b/indexGenerator actions
	.b/indexGenerator commands
	.b/indexGenerator options

# The normal command to compile a .c file to its .o file.
# Without or with ALL_FLAGS.
COMPILE = $(CC) -c -I$(srcdir) $(ALL_FLAGS)
CClink = $(CC)

# Link the target for normal use or debugging.
# A shell script is used to try linking without unnecessary libraries.
$(EEGLTARGET): $(OBJ)
	@$(BUILD_DATE_MSG)
	@LINK="$(PURIFY) $(SHRPENV) $(CClink) $(ALL_LIB_DIRS) $(LDFLAGS) \
		-o $(EEGLTARGET) $(OBJ) $(ALL_LIBS)" \
		MAKE="$(MAKE)" LINK_AS_NEEDED=$(LINK_AS_NEEDED) \
		PROG="$(EEGLNAME)" \
		sh $(srcdir)/link.sh
	@echo '                                 '
	@echo '                    .^^~-.       '
	@echo '                    / ,__`)      '
	@echo "                   |   \o/|'--.  "
	@echo "    BUILD SUCCESS!  \     /   ,\ "
	@echo "                     \    '---./ "
	@echo '                    /     \      '
	@echo '                   / ,  ,  \     '
	@echo "                   \`-'--'--'    "
	@echo "                                 "

# Build the language specific files if they were unpacked.
# Generate the converted .mo files separately, it's no problem if this fails.
languages:
	@if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
		cd $(PODIR); \
		  CC="$(CC)" $(MAKE) prefix=$(DESTDIR)$(prefix) originals; \
	fi
	-@if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
		cd $(PODIR); \
		  CC="$(CC)" $(MAKE) prefix=$(DESTDIR)$(prefix) converted; \
	fi

# Update the *.po files for changes in the sources.  Only run manually.
update-po:
	cd $(PODIR); CC="$(CC)" $(MAKE) prefix=$(DESTDIR)$(prefix) update-po

# Generate function prototypes.  This is not needed to compile Eegl, but if
# you want to use it, cproto is out there on the net somewhere -- Webb


# Filter out arguments that cproto doesn't support.
# Don't pass "-pthread", "-fwrapv" and similar arguments to cproto, it sees
# them as a list of individual flags.
# The -E"gcc -E" argument must be separate to avoid problems with shell
# quoting.
# Strip -O2, it may cause cproto to write stderr to the file "2".
CPROTO = cproto $(PROTO_FLAGS) -DPROTO \
	 `echo '$(LINT_FLAGS)' | sed -e 's/ -[a-z-]\+//g' -e 's/ -O[^ ]\+//g'`



PROTO_RESULTS := $(addprefix src/proto/,$(patsubst %.c,%.pro,$(BASIC_SRC_NO_DIR)))

src/proto/%.pro: src/%.c
	$(CPROTO) $< > $@
	echo "/* eegl: set ft=c : */" >> $@


proto: $(PROTO_RESULTS) $(addprefix src/proto/,$(PRO_MANUAL))

notags:
	-rm -f tags

# Note: tags is made for the currently configured version.
# You can ignore error messages for missing files.
tags TAGS: notags
	$(TAGPRG) $(TAGS_FILES)

# Build the cscope database.
# This may search more files than necessary.
.PHONY: cscope csclean all indices update-po

csclean:
	-rm -vf cscope.out
cscope.out:
	cscope -bv ./*.[ch] src/proto/*.pro
cscope: csclean cscope.out  ;

# Make a hilite file for types.  Requires Exuberant ctags and awk
types: types.vim
types.vim: $(TAGS_FILES)
	ctags --c-kinds=gstu -o- $(TAGS_FILES) |\
		awk 'BEGIN{printf("syntax keyword Type\t")}\
			{printf("%s ", $$1)}END{print ""}' > $@
	echo "syn keyword Constant OK FAIL TRUE FALSE MAYBE" >> $@

# TESTING
#
# Execute the test scripts and the unittests.
# Do the scripttests first, so that the summary shows last.
test check: unittests $(TERM_TEST) scripttests

# Execute the test scripts.  Run these after compiling Eegl, before installing.
# This doesn't depend on $(EEGLTARGET), because that won't work when configure
# wasn't run yet.  Restart make to build it instead.
#
# This will produce a lot of garbage on your screen, including a few error
# messages.  Don't worry about that.
# If everything is alright, the final message will be "ALL DONE".  If not you
# get "TEST FAILURE".
#
scripttests:
	$(MAKE) -f Makefile $(EEGLTARGET)
	if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
		cd $(PODIR); $(MAKE) -f Makefile check VIMPROG=../$(EEGLTARGET); \
	fi
	-if test $(EEGLTARGET) != eegl -a ! -r eegl; then \
		ln -s $(EEGLTARGET) eegl; \
	fi
	cd noncode/tests;\
   $(MAKE) -f Makefile VIMPROG=../$(EEGLTARGET) SCRIPTSOURCE=../$(SCRIPTSOURCE)

testtiny:
	cd tests; $(MAKE) -f Makefile tiny VIMPROG=../$(EEGLTARGET) SCRIPTSOURCE=../$(SCRIPTSOURCE)

# Run benchmarks.
benchmark:
	cd tests; \
		$(MAKE) -f Makefile benchmarkclean; \
		$(MAKE) -f Makefile benchmark VIMPROG=../$(EEGLTARGET) SCRIPTSOURCE=../$(SCRIPTSOURCE)

unittesttargets:
	$(MAKE) -f Makefile $(UNITTEST_TARGETS)

VIMTESTTARGET = $(EEGLTARGET)

# Execute the unittests one by one.
unittest unittests: $(RUN_UNITTESTS)

run_json_test: $(JSON_TEST_TARGET)
	$(VALGRIND) ./$(JSON_TEST_TARGET) || exit 1; echo $* passed;

run_kword_test: $(KWORD_TEST_TARGET)
	$(VALGRIND) ./$(KWORD_TEST_TARGET) || exit 1; echo $* passed;

run_memfile_test: $(MEMFILE_TEST_TARGET)
	$(VALGRIND) ./$(MEMFILE_TEST_TARGET) || exit 1; echo $* passed;

run_message_test: $(MESSAGE_TEST_TARGET)
	$(VALGRIND) ./$(MESSAGE_TEST_TARGET) || exit 1; echo $* passed;

# Run the libvterm tests.
# This works only on GNU make, not on BSD make.
# Libtool requires "gcc".
test_libvterm:
	@if $(MAKE) --version 2>/dev/null | grep -qs "GNU Make"; then \
	  if test -x "/usr/bin/gcc"; then \
	    cd libvterm; $(MAKE) -f Makefile test CC="$(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)"; \
	  fi \
	fi

# Run individual OLD style test.
# These do not depend on the executable, compile it when needed.
$(SCRIPTS_TINY):
	cd tests; rm -f $@.out; $(MAKE) -f Makefile $@.out VIMPROG=../$(VIMTESTTARGET) $(GUI_TESTARG) SCRIPTSOURCE=../$(SCRIPTSOURCE)

# Run individual NEW style test.
# These do not depend on the executable, compile it when needed.
# Set $TEST_FILTER to select what test function to invoke, e.g.:
#	export TEST_FILTER=Test_terminal_wipe_buffer
# A partial match also works:
#	export TEST_FILTER=wipe_buffer
$(NEW_TESTS) test_vim9:
	cd tests; $(MAKE) $@ VIMPROG=../$(VIMTESTTARGET) $(GUI_TESTARG) SCRIPTSOURCE=../$(SCRIPTSOURCE)

newtests:
	cd tests; rm -f $@.res test.log messages; $(MAKE) -f Makefile newtestssilent VIMPROG=../$(VIMTESTTARGET) $(GUI_TESTARG) SCRIPTSOURCE=../$(SCRIPTSOURCE)
	@if test -f tests/test.log; then \
		cat tests/test.log; \
	fi
	cat tests/messages

testclean:
	cd tests; $(MAKE) -f Makefile clean
	if test -d $(PODIR); then \
		cd $(PODIR); $(MAKE) checkclean; \
	fi

# Unittests
# It's build just like Eegl to satisfy all dependencies.
$(JSON_TEST_TARGET): $(JSON_TEST_OBJ)
	@LINK="$(PURIFY) $(SHRPENV) $(CClink) $(ALL_LIB_DIRS) $(LDFLAGS) \
		-o $(JSON_TEST_TARGET) $(JSON_TEST_OBJ) $(ALL_LIBS)" \
		MAKE="$(MAKE)" LINK_AS_NEEDED=$(LINK_AS_NEEDED) \
		PROG="json_test" \
		sh $(srcdir)/link.sh

$(KWORD_TEST_TARGET): $(KWORD_TEST_OBJ)
	@LINK="$(PURIFY) $(SHRPENV) $(CClink) $(ALL_LIB_DIRS) $(LDFLAGS) \
		-o $(KWORD_TEST_TARGET) $(KWORD_TEST_OBJ) $(ALL_LIBS)" \
		MAKE="$(MAKE)" LINK_AS_NEEDED=$(LINK_AS_NEEDED) \
		PROG="kword_test" \
		sh $(srcdir)/link.sh

$(MEMFILE_TEST_TARGET): $(MEMFILE_TEST_OBJ)
	@LINK="$(PURIFY) $(SHRPENV) $(CClink) $(ALL_LIB_DIRS) $(LDFLAGS) \
		-o $(MEMFILE_TEST_TARGET) $(MEMFILE_TEST_OBJ) $(ALL_LIBS)" \
		MAKE="$(MAKE)" LINK_AS_NEEDED=$(LINK_AS_NEEDED) \
		PROG="memfile_test" \
		sh $(srcdir)/link.sh

$(MESSAGE_TEST_TARGET): $(MESSAGE_TEST_OBJ)
	@LINK="$(PURIFY) $(SHRPENV) $(CClink) $(ALL_LIB_DIRS) $(LDFLAGS) \
		-o $(MESSAGE_TEST_TARGET) $(MESSAGE_TEST_OBJ) $(ALL_LIBS)" \
		MAKE="$(MAKE)" LINK_AS_NEEDED=$(LINK_AS_NEEDED) \
		PROG="message_test" \
		sh $(srcdir)/link.sh

# install targets

install: $(GUI_INSTALL)

install_normal: installvim installtools $(INSTALL_LANGS) install-icons

install_gui_extra: installgtutorbin

installvim: installvimbin installtutorbin \
		installruntime installlinks installmanlinks

#
# Avoid overwriting an existing executable, somebody might be running it and
# overwriting it could cause it to crash.  Deleting it is OK, it won't be
# really deleted until all running processes for it have exited.  It is
# renamed first, in case the deleting doesn't work.
#
# If you want to keep an older version, rename it before running "make
# install".
#
installvimbin: $(EEGLTARGET) $(DESTDIR)$(exec_prefix) $(DEST_BIN)
	-if test -f $(DEST_BIN)/$(EEGLTARGET); then \
	  mv -f $(DEST_BIN)/$(EEGLTARGET) $(DEST_BIN)/$(EEGLNAME).rm; \
	  rm -f $(DEST_BIN)/$(EEGLNAME).rm; \
	fi
	$(INSTALL_PROG) $(EEGLTARGET) $(DEST_BIN)
	strip $(DEST_BIN)/$(EEGLTARGET)
	chmod $(BINMOD) $(DEST_BIN)/$(EEGLTARGET)
# may create a link to the new executable from /usr/bin/vi
	-$(LINKIT)

# Long list of arguments for the shell script that installs the manual pages
# for one language.
INSTALLMANARGS = $(VIMLOC) $(SCRIPTLOC) $(VIMRCLOC) $(HELPSOURCE) $(MANMOD) \
		$(EEGLNAME) $(EEGLDIFFNAME) $(EEEGLNAME)

# Install most of the runtime files
installruntime: installrtbase installmacros installpack installtutor installspell

# Install the help files; first adjust the contents for the final location.
# Also install most of the other runtime files.
installrtbase: $(HELPSOURCE)/vim.1 $(DEST_VIM) $(EEGLTARGET) $(DEST_RT) \
		$(DEST_HELP) $(DEST_PRINT) $(DEST_COL) \
		$(DEST_SYN) $(DEST_SYN)/modula2 $(DEST_SYN)/modula2/opt $(DEST_SYN)/shared \
		$(DEST_IND) $(DEST_FTP) \
		$(DEST_AUTO) $(DEST_AUTO)/dist $(DEST_AUTO)/xml \
		$(DEST_AUTO)/rust $(DEST_AUTO)/cargo \
		$(DEST_IMPORT) $(DEST_IMPORT)/dist \
		$(DEST_PLUG) \
	       	$(DEST_TUTOR) $(DEST_TUTOR)/en $(DEST_TUTOR)/it $(DEST_TUTOR)/sr \
		$(DEST_TUTOR)/ru \
		$(DEST_SPELL) $(DEST_COMP)
	-$(SHELL) ./installman.sh install $(DEST_MAN) "" $(INSTALLMANARGS)
# Generate the help tags with ":helptags" to handle all languages.
# Move the distributed tags file aside and restore it, to avoid it being
# different from the repository.
	cd $(HELPSOURCE); if test -z "$(CROSS_COMPILING)" -a -f tags; then \
		mv -f tags tags.dist; fi
	@echo generating help tags
	-@BUILD_DIR=`pwd`; cd $(HELPSOURCE); if test -z "$(CROSS_COMPILING)"; then \
		$(MAKE) VIMPROG="$$BUILD_DIR/$(EEGLTARGET)" vimtags; fi
	cd $(HELPSOURCE); \
		files=`ls *.txt tags`; \
		files="$$files `ls *.??x tags-?? 2>/dev/null || true`"; \
		cp $$files  $(DEST_HELP); \
		cd $(DEST_HELP); \
		chmod $(HELPMOD) $$files
	cp  $(HELPSOURCE)/*.pl $(DEST_HELP)
	chmod $(SCRIPTMOD) $(DEST_HELP)/*.pl
	cd $(HELPSOURCE); if test -f tags.dist; then mv -f tags.dist tags; fi
# install the menu files
	cp $(SCRIPTSOURCE)/menu.vim $(SYS_MENU_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_MENU_FILE)
	cp $(SCRIPTSOURCE)/synmenu.vim $(SYS_SYNMENU_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_SYNMENU_FILE)
	cp $(SCRIPTSOURCE)/delmenu.vim $(SYS_DELMENU_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_DELMENU_FILE)
# install the defaults/evim/mswin file
	cp $(SCRIPTSOURCE)/defaults.vim $(VIM_DEFAULTS_FILE)
	chmod $(VIMSCRIPTMOD) $(VIM_DEFAULTS_FILE)
	cp $(SCRIPTSOURCE)/evim.vim $(EVIM_FILE)
	chmod $(VIMSCRIPTMOD) $(EVIM_FILE)
	cp $(SCRIPTSOURCE)/mswin.vim $(MSWIN_FILE)
	chmod $(VIMSCRIPTMOD) $(MSWIN_FILE)
# install the bugreport file
	cp $(SCRIPTSOURCE)/bugreport.vim $(SYS_BUGR_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_BUGR_FILE)
# install the example vimrc files
	cp $(SCRIPTSOURCE)/vimrc_example.vim $(DEST_SCRIPT)
	chmod $(VIMSCRIPTMOD) $(DEST_SCRIPT)/vimrc_example.vim
	cp $(SCRIPTSOURCE)/gvimrc_example.vim $(DEST_SCRIPT)
	chmod $(VIMSCRIPTMOD) $(DEST_SCRIPT)/gvimrc_example.vim
# install the file type detection files
	cp $(SCRIPTSOURCE)/filetype.vim $(SYS_FILETYPE_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_FILETYPE_FILE)
	cp $(SCRIPTSOURCE)/ftoff.vim $(SYS_FTOFF_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_FTOFF_FILE)
	cp $(SCRIPTSOURCE)/scripts.vim $(SYS_SCRIPTS_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_SCRIPTS_FILE)
	cp $(SCRIPTSOURCE)/ftplugin.vim $(SYS_FTPLUGIN_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_FTPLUGIN_FILE)
	cp $(SCRIPTSOURCE)/ftplugof.vim $(SYS_FTPLUGOF_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_FTPLUGOF_FILE)
	cp $(SCRIPTSOURCE)/indent.vim $(SYS_INDENT_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_INDENT_FILE)
	cp $(SCRIPTSOURCE)/indoff.vim $(SYS_INDOFF_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_INDOFF_FILE)
	cp $(SCRIPTSOURCE)/optwin.vim $(SYS_OPTWIN_FILE)
	chmod $(VIMSCRIPTMOD) $(SYS_OPTWIN_FILE)
# install README and LICENCE files
	cp ../README.txt $(DEST_RT)
	chmod $(HELPMOD) $(DEST_RT)/README.txt
	cp ../LICENSE $(DEST_RT)
	chmod $(HELPMOD) $(DEST_RT)/LICENSE
# install the print resource files
	cd $(PRINTSOURCE); cp *.ps $(DEST_PRINT)
	cd $(DEST_PRINT); chmod $(FILEMOD) *.ps
# install the colorscheme files
	cd $(COLSOURCE); cp -r *.vim lists tools README.txt $(DEST_COL)
	cd $(DEST_COL); chmod $(DIRMOD) lists tools
	cd $(DEST_COL); chmod $(HELPMOD) *.vim README.txt lists/*.vim tools/*.vim
# install the syntax files
	cd $(SYNSOURCE); cp *.vim README.txt $(DEST_SYN)
	cd $(DEST_SYN); chmod $(HELPMOD) *.vim README.txt
	cd $(SYNSOURCE)/shared; cp *.vim README.txt $(DEST_SYN)/shared
	cd $(DEST_SYN)/shared; chmod $(HELPMOD) *.vim README.txt
	cd $(SYNSOURCE)/modula2/opt; cp *.vim $(DEST_SYN)/modula2/opt
	cd $(DEST_SYN)/modula2/opt; chmod $(HELPMOD) *.vim
# install the indent files
	cd $(INDSOURCE); cp *.vim README.txt $(DEST_IND)
	cd $(DEST_IND); chmod $(HELPMOD) *.vim README.txt
# install the standard autoload files
	cd $(AUTOSOURCE); cp *.vim README.txt $(DEST_AUTO)
	cd $(DEST_AUTO); chmod $(HELPMOD) *.vim README.txt
	cd $(AUTOSOURCE)/dist; cp *.vim $(DEST_AUTO)/dist
	cd $(DEST_AUTO)/dist; chmod $(HELPMOD) *.vim
	cd $(AUTOSOURCE)/xml; cp *.vim $(DEST_AUTO)/xml
	cd $(DEST_AUTO)/xml; chmod $(HELPMOD) *.vim
	cd $(AUTOSOURCE)/cargo; cp *.vim $(DEST_AUTO)/cargo
	cd $(DEST_AUTO)/cargo; chmod $(HELPMOD) *.vim
	cd $(AUTOSOURCE)/rust; cp *.vim $(DEST_AUTO)/rust
	cd $(DEST_AUTO)/rust; chmod $(HELPMOD) *.vim
# install the standard import files
	cd $(IMPORTSOURCE)/dist; cp *.vim $(DEST_IMPORT)/dist
	cd $(DEST_IMPORT)/dist; chmod $(HELPMOD) *.vim
# install the standard plugin files
	cd $(PLUGSOURCE); cp *.vim README.txt $(DEST_PLUG)
	cd $(DEST_PLUG); chmod $(HELPMOD) *.vim README.txt
# install the ftplugin files
	cd $(FTPLUGSOURCE); cp *.vim README.txt logtalk.dict $(DEST_FTP)
	cd $(DEST_FTP); chmod $(HELPMOD) *.vim README.txt logtalk.dict
# install the compiler files
	cd $(COMPSOURCE); cp *.vim README.txt $(DEST_COMP)
	cd $(DEST_COMP); chmod $(HELPMOD) *.vim README.txt

installmacros: $(DEST_VIM) $(DEST_RT) $(DEST_MACRO)
	cp -r $(MACROSOURCE)/* $(DEST_MACRO)
	chmod $(DIRMOD) `find $(DEST_MACRO) -type d -print`
	chmod $(FILEMOD) `find $(DEST_MACRO) -type f -print`
	chmod $(SCRIPTMOD) $(DEST_MACRO)/less.sh
# When using CVS some CVS directories might have been copied.
# Also delete AAPDIR and *.info files.
	cvs=`find $(DEST_MACRO) \( -name CVS -o -name AAPDIR -o -name "*.info" \) -print`; \
	      if test -n "$$cvs"; then \
		 rm -rf $$cvs; \
	      fi

installpack: $(DEST_VIM) $(DEST_RT) $(DEST_PACK)
	cp -r $(PACKSOURCE)/* $(DEST_PACK)
	chmod $(DIRMOD) `find $(DEST_PACK) -type d -print`
	chmod $(FILEMOD) `find $(DEST_PACK) -type f -print`

# install the tutor files
installtutorbin: $(DEST_BIN)
	cp scripts/vimtutor $(DEST_BIN)/$(EEGLNAME)tutor
	chmod $(SCRIPTMOD) $(DEST_BIN)/$(EEGLNAME)tutor


installtutor: $(DEST_RT) $(DEST_TUTOR)/en $(DEST_TUTOR)/it $(DEST_TUTOR)/sr $(DEST_TUTOR)/ru
	-cp $(TUTORSOURCE)/README* $(TUTORSOURCE)/tutor* $(DEST_TUTOR)
	-cp $(TUTORSOURCE)/en/* $(DEST_TUTOR)/en/
	-cp $(TUTORSOURCE)/it/* $(DEST_TUTOR)/it/
	-cp $(TUTORSOURCE)/ru/* $(DEST_TUTOR)/ru/
	-cp $(TUTORSOURCE)/sr/* $(DEST_TUTOR)/sr/
	-rm -f $(DEST_TUTOR)/*.info
	chmod $(HELPMOD) $(DEST_TUTOR)/*
	chmod $(DIRMOD) $(DEST_TUTOR)/en
	chmod $(DIRMOD) $(DEST_TUTOR)/it
	chmod $(DIRMOD) $(DEST_TUTOR)/ru
	chmod $(DIRMOD) $(DEST_TUTOR)/sr

# Install the spell files, if they exist.  This assumes at least the English
# spell file is there.
installspell: $(DEST_VIM) $(DEST_RT) $(DEST_SPELL)
	if test -f $(SPELLSOURCE)/en.latin1.spl; then \
	  cp $(SPELLSOURCE)/*.spl $(SPELLSOURCE)/*.sug $(SPELLSOURCE)/*.vim $(DEST_SPELL); \
	  chmod $(HELPMOD) $(DEST_SPELL)/*.spl $(DEST_SPELL)/*.sug $(DEST_SPELL)/*.vim; \
	fi

# install the runtime tools
	cp -r $(TOOLSSOURCE)/* $(DEST_TOOLS)
# When using CVS some CVS directories might have been copied.
	cvs=`find $(DEST_TOOLS) \( -name CVS -o -name AAPDIR \) -print`; \
	      if test -n "$$cvs"; then \
		 rm -rf $$cvs; \
	      fi
	-chmod $(FILEMOD) $(DEST_TOOLS)/*
# replace the path in some tools
	awkpath=`which nawk` && sed -e "s+/usr/bin/nawk+$$awkpath+" $(TOOLSSOURCE)/mve.awk >$(DEST_TOOLS)/mve.awk; if test -z "$$awkpath"; then \
		awkpath=`which gawk` && sed -e "s+/usr/bin/nawk+$$awkpath+" $(TOOLSSOURCE)/mve.awk >$(DEST_TOOLS)/mve.awk; if test -z "$$awkpath"; then \
		awkpath=`which awk` && sed -e "s+/usr/bin/nawk+$$awkpath+" $(TOOLSSOURCE)/mve.awk >$(DEST_TOOLS)/mve.awk; fi; fi
	-chmod $(SCRIPTMOD) `grep -l "^#!" $(DEST_TOOLS)/*`


# install the language specific files, if they were unpacked
install-languages: languages $(DEST_LANG) $(DEST_KMAP) $(DEST_RT)
	-$(SHELL) ./installman.sh install $(DEST_MAN_DA) "-da" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_DA_I) "-da" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_DA_U) "-da.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_DE) "-de" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_DE_I) "-de" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_DE_U) "-de.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_FR) "-fr" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_FR_I) "-fr" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_FR_U) "-fr.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_IT) "-it" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_IT_I) "-it" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_IT_U) "-it.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_JA_U) "-ja.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_PL) "-pl" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_PL_I) "-pl" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_PL_U) "-pl.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_RU) "-ru" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_RU_U) "-ru.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_TR) "-tr" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_TR_I) "-tr" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh install $(DEST_MAN_TR_U) "-tr.UTF-8" $(INSTALLMANARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DA) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DA_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DA_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DE) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DE_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DE_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_FR) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_FR_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_FR_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_IT) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_IT_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_IT_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_JA_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_PL) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_PL_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_PL_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_RU) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_RU_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_TR) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_TR_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_TR_U) $(INSTALLMLARGS)
	if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
	   cd $(PODIR); $(MAKE) prefix=$(DESTDIR)$(prefix) LOCALEDIR=$(DEST_LANG) \
	   FILEMOD=$(FILEMOD) install; \
	fi
	if test -d $(LANGSOURCE); then \
	   cp $(LANGSOURCE)/README.txt $(LANGSOURCE)/*.vim $(DEST_LANG); \
	   chmod $(FILEMOD) $(DEST_LANG)/README.txt $(DEST_LANG)/*.vim; \
	fi
	if test -d $(KMAPSOURCE); then \
	   cp $(KMAPSOURCE)/README.txt $(KMAPSOURCE)/*.vim $(DEST_KMAP); \
	   chmod $(FILEMOD) $(DEST_KMAP)/README.txt $(DEST_KMAP)/*.vim; \
	fi
# Installing translated README and LICENSE files
	if test -d $(TRANSSOURCE) ; then \
	  if test -n "$(LANG)" ; then \
	    lngusr=$${LANG%%.*} ; \
	  elif test -n "$(LANGUAGE)" ; then \
	    lngusr=$${LANGUAGE%%:*} ; \
	  elif test -n "$(LC_MESSAGES)" ; then \
	    lngusr=$${LC_MESSAGES%%.*} ; \
	  fi; \
	  if test "$$lngusr" = "zh_TW" -o "$$lngusr" = "zh_CN" -o "$$lngusr" = "pt_BR" ; then \
	    lngusr=`echo $$lngusr | tr '[:upper:]' '[:lower:]'` ; \
	  elif test -n "$$lngusr" -a "$$lngusr" != "C" -a "$$lngusr" != "POSIX" ; then \
	    lngusr=$${lngusr%%_*} ; \
	  fi ; \
	  if test -f $(TRANSSOURCE)/README.$$lngusr.txt ; then \
	    cp $(TRANSSOURCE)/README.$$lngusr.txt $(DEST_RT) ; \
	    chmod $(HELPMOD) $(DEST_RT)/README.$$lngusr.txt ; \
	  fi ; \
	  if test -f $(TRANSSOURCE)/LICENSE.$$lngusr.txt ; then \
	    cp $(TRANSSOURCE)/LICENSE.$$lngusr.txt $(DEST_RT) ; \
	    chmod $(HELPMOD) $(DEST_RT)/LICENSE.$$lngusr.txt ; \
	  fi ; \
	fi

# Install the icons for KDE, if the directory exists and the icon doesn't.
# Always when $(DESTDIR) is not empty.
ICON48PATH = $(DESTDIR)$(DATADIR)/icons/hicolor/48x48/apps
ICON32PATH = $(DESTDIR)$(DATADIR)/icons/locolor/32x32/apps
ICON16PATH = $(DESTDIR)$(DATADIR)/icons/locolor/16x16/apps
ICONTHEMEPATH = $(DATADIR)/icons/hicolor
DESKTOPPATH = $(DESTDIR)$(DATADIR)/applications
KDEPATH = $(HOME)/.kde/share/icons
install-icons:
	if test -n "$(DESTDIR)$(DATADIR)"; then \
		mkdir -p $(ICON48PATH) $(ICON32PATH) \
		$(ICON16PATH) $(DESKTOPPATH); \
	fi

	if test -d $(ICON48PATH) -a -w $(ICON48PATH) \
		-a ! -f $(ICON48PATH)/gvim.png; then \
	   cp $(SCRIPTSOURCE)/vim48x48.png $(ICON48PATH)/gvim.png; \
	   if test -z "$(DESTDIR)" -a -x "$(GTK_UPDATE_ICON_CACHE)" \
		   -a -w $(ICONTHEMEPATH) \
		   -a -f $(ICONTHEMEPATH)/index.theme; then \
		$(GTK_UPDATE_ICON_CACHE) -q $(ICONTHEMEPATH); \
	   fi \
	fi
	if test -d $(ICON32PATH) -a -w $(ICON32PATH) \
		-a ! -f $(ICON32PATH)/gvim.png; then \
	   cp $(SCRIPTSOURCE)/vim32x32.png $(ICON32PATH)/gvim.png; \
	fi
	if test -d $(ICON16PATH) -a -w $(ICON16PATH) \
		-a ! -f $(ICON16PATH)/gvim.png; then \
	   cp $(SCRIPTSOURCE)/vim16x16.png $(ICON16PATH)/gvim.png; \
	fi
	if test -d $(DESKTOPPATH) -a -w $(DESKTOPPATH); then \
	   if test -f po/vim.desktop -a -f po/gvim.desktop; then \
		cp po/vim.desktop po/gvim.desktop \
			$(DESKTOPPATH); \
	   else \
		cp $(SCRIPTSOURCE)/vim.desktop \
			$(SCRIPTSOURCE)/gvim.desktop \
			$(DESKTOPPATH); \
	   fi; \
	   if test -z "$(DESTDIR)" -a -x "$(UPDATE_DESKTOP_DATABASE)"; then \
	      $(UPDATE_DESKTOP_DATABASE) -q $(DESKTOPPATH); \
	   fi \
	fi

$(HELPSOURCE)/vim.1 $(MACROSOURCE) $(TOOLSSOURCE):
	@echo Runtime files not found.
	@echo You need to unpack the runtime archive before running "make install".
	test -f error

$(DESTDIR)$(exec_prefix) $(DEST_BIN) \
		$(DEST_VIM) $(DEST_RT) $(DEST_HELP) \
		$(DEST_PRINT) $(DEST_COL) $(DEST_SYN) $(DEST_SYN)/shared \
		$(DEST_SYN)/modula2 $(DEST_SYN)/modula2/opt \
		$(DEST_IND) $(DEST_FTP) \
		$(DEST_LANG) $(DEST_KMAP) $(DEST_COMP) $(DEST_MACRO) \
		$(DEST_PACK) $(DEST_TOOLS) \
		$(DEST_TUTOR) $(DEST_TUTOR)/en $(DEST_TUTOR)/it \
		$(DEST_TUTOR)/sr $(DEST_TUTOR)/ru \
		$(DEST_SPELL) \
		$(DEST_AUTO) $(DEST_AUTO)/dist $(DEST_AUTO)/xml \
		$(DEST_AUTO)/cargo $(DEST_AUTO)/rust \
		$(DEST_IMPORT) $(DEST_IMPORT)/dist $(DEST_PLUG):
	mkdir -p $@
	-chmod $(DIRMOD) $@

# Create links from various names to vim.  This is only done when the links
# (or executables with the same name) don't exist yet.
installlinks: $(GUI_TARGETS) \
			$(DEST_BIN)/$(VIEWTARGET) \
			$(DEST_BIN)/$(REEGLTARGET) \
			$(DEST_BIN)/$(RVIEWTARGET) \
			$(INSTALLVIMDIFF)

installglinks: $(DEST_BIN)/$(GEEGLTARGET) \
			$(DEST_BIN)/$(GVIEWTARGET) \
			$(DEST_BIN)/$(RGEEGLTARGET) \
			$(DEST_BIN)/$(RGVIEWTARGET) \
			$(DEST_BIN)/$(EEEGLTARGET) \
			$(DEST_BIN)/$(EVIEWTARGET) \
			$(INSTALLGVIMDIFF)

installvimdiff: $(DEST_BIN)/$(VIMDIFFTARGET)

$(DEST_BIN)/$(VIEWTARGET): $(DEST_BIN)
	cd $(DEST_BIN); ln -s $(EEGLTARGET) $(VIEWTARGET)


$(DEST_BIN)/$(EEGLDIFFTARGET): $(DEST_BIN)
	cd $(DEST_BIN); ln -s $(EEGLTARGET) $(VIMDIFFTARGET)

# Create links for the manual pages with various names to Eegl. This is only
# done when the links (or manpages with the same name) don't exist yet.

INSTALLMLARGS = $(EEGLNAME) $(EEGLDIFFNAME) $(EEEGLNAME)

installmanlinks:
	-$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN) $(INSTALLMLARGS)

uninstall: uninstall_runtime
	-rm -f $(DEST_BIN)/$(EEGLTARGET)
	-rm -f $(DEST_BIN)/vimtutor
	-rm -f $(DEST_BIN)/$(REEGLTARGET) $(DEST_BIN)/$(RVIEWTARGET)
	-rm -f $(DEST_BIN)/$(RGEEGLTARGET) $(DEST_BIN)/$(RGVIEWTARGET)
	-rm -f $(DEST_BIN)/$(VIMDIFFTARGET) $(DEST_BIN)/$(GVIMDIFFTARGET)
	-rm -f $(DEST_BIN)/$(EEEGLTARGET) $(DEST_BIN)/$(EVIEWTARGET)

# Note: the "rmdir" will fail if any files were added after "make install"
uninstall_runtime:
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_DA) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_DA_I) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_DA_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_DE) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_DE_I) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_DE_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_FR) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_FR_I) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_FR_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_IT) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_IT_I) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_IT_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_JA_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_PL) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_PL_I) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_PL_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_RU) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_RU_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_TR) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_TR_I) "" $(INSTALLMANARGS)
	-$(SHELL) ./installman.sh uninstall $(DEST_MAN_TR_U) "" $(INSTALLMANARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DA) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DA_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DA_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DE) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DE_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_DE_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_FR) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_FR_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_FR_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_IT) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_IT_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_IT_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_JA_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_PL) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_PL_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_PL_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_RU) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_RU_U) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_TR) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_TR_I) $(INSTALLMLARGS)
	-$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
		$(DEST_MAN_TR_U) $(INSTALLMLARGS)
	-rm -f $(DEST_HELP)/*.txt $(DEST_HELP)/tags $(DEST_HELP)/*.pl
	-rm -f $(DEST_HELP)/*.??x $(DEST_HELP)/tags-??
	-rm -f $(SYS_MENU_FILE) $(SYS_SYNMENU_FILE) $(SYS_DELMENU_FILE)
	-rm -f $(SYS_BUGR_FILE) $(VIM_DEFAULTS_FILE) $(EVIM_FILE) $(MSWIN_FILE)
	-rm -f $(DEST_SCRIPT)/gvimrc_example.vim $(DEST_SCRIPT)/vimrc_example.vim
	-rm -f $(SYS_FILETYPE_FILE) $(SYS_FTOFF_FILE) $(SYS_SCRIPTS_FILE)
	-rm -f $(SYS_INDOFF_FILE) $(SYS_INDENT_FILE)
	-rm -f $(SYS_FTPLUGOF_FILE) $(SYS_FTPLUGIN_FILE)
	-rm -f $(SYS_OPTWIN_FILE)
	-rm -f $(DEST_COL)/*.vim $(DEST_COL)/README.txt
	-rm -rf $(DEST_COL)/tools
	-rm -f $(DESKTOPPATH)/vim.desktop $(DESKTOPPATH)/gvim.desktop
	-rm -f $(ICON16PATH)/gvim.png $(ICON32PATH)/gvim.png $(ICON48PATH)/gvim.png
	-rm -rf $(DEST_COL)/lists
	-rm -f $(DEST_SYN)/shared/*.vim $(DEST_SYN)/shared/README.txt
	-rm -f $(DEST_SYN)/modula2/opt/*.vim
	-rm -f $(DEST_SYN)/*.vim $(DEST_SYN)/README.txt
	-rm -f $(DEST_IND)/*.vim $(DEST_IND)/README.txt
	-rm -rf $(DEST_MACRO)
	-rm -rf $(DEST_PACK)
	-rm -rf $(DEST_TUTOR)/en
	-rm -rf $(DEST_TUTOR)/it
	-rm -rf $(DEST_TUTOR)/ru
	-rm -rf $(DEST_TUTOR)/sr
	-rm -rf $(DEST_TUTOR)
	-rm -rf $(DEST_SPELL)
	-rm -rf $(DEST_TOOLS)
	-rm -rf $(DEST_LANG)
	-rm -rf $(DEST_KMAP)
	-rm -rf $(DEST_COMP)
	-rm -f $(DEST_PRINT)/*.ps
	-rmdir $(DEST_HELP) $(DEST_PRINT) $(DEST_COL) $(DEST_SYN)/shared
	-rmdir $(DEST_SYN)/modula2/opt $(DEST_SYN)/modula2
	-rmdir $(DEST_SYN) $(DEST_IND)
	-rm -rf $(DEST_FTP)/*.vim $(DEST_FTP)/README.txt $(DEST_FTP)/logtalk.dict
	-rm -f $(DEST_AUTO)/*.vim $(DEST_AUTO)/README.txt
	-rm -f $(DEST_AUTO)/dist/*.vim $(DEST_AUTO)/xml/*.vim $(DEST_AUTO)/cargo/*.vim $(DEST_AUTO)/rust/*.vim
	-rm -f $(DEST_IMPORT)/dist/*.vim
	-rm -f $(DEST_PLUG)/*.vim $(DEST_PLUG)/README.txt
	-rmdir $(DEST_FTP) $(DEST_AUTO)/dist $(DEST_AUTO)/xml $(DEST_AUTO)/cargo $(DEST_AUTO)/rust $(DEST_AUTO)
	-rmdir $(DEST_IMPORT)/dist $(DEST_IMPORT)
	-rm -f $(DEST_RT)/README.??.txt
	-rm -f $(DEST_RT)/README.??_??.txt
	-rm -f $(DEST_RT)/LICENSE.??.txt
	-rm -f $(DEST_RT)/LICENSE.??_??.txt
	-rm -f $(DEST_RT)/README.txt $(DEST_RT)/LICENSE
	-rmdir $(DEST_PLUG) $(DEST_RT)
#	This will fail when other Eegl versions are installed, no worries.
	-rmdir $(DEST_VIM)

# Clean up all the files that have been produced, except configure's.
# We support common typing mistakes for Juergen! :-)
clean celan: testclean
	-rm -f *.o core $(EEGLTARGET).core $(EEGLTARGET) vim
	-rm .b/*
	-rm .b/os/*
	-rm -f conftest* *~ auto/link.sed
	-rm -f tests/opt_test.vim
	-rm -f $(UNITTEST_TARGETS)
	-rm -rf libs/libvterm/.libs libs/libvterm/src/.libs \
                libs/libvterm/t/.libs libs/libvterm/src/*.o \
                libs/libvterm/src/*.lo \
                libs/libvterm/t/*.o libvterm/t/*.lo libvterm/t/harness libvterm/libvterm.la
	if test -d $(PODIR); then \
		cd $(PODIR); $(MAKE) prefix=$(DESTDIR)$(prefix) clean; \
	fi
	cd libs/wayland; $(MAKE) clean

# Make a shadow directory for compilation on another system or with different
# features:
#  % make shadow
#  % cd shadow
#  edit configuration in src/shadow/Makefile
#  % make
#
# Alternatively use a link for the Makefile and run configure with flags in
# another way.  When new source files are added use "shadowupdate":
#  % cd shadow
#  % rm Makefile
#  % ln -s ../Makefile .
#  % ./configure {options}
#  % make
# And later:
#  % git pull
#  % make distclean shadowupdate
#  % ./configure {options}
#  % make
SHADOWDIR = shadow

LINKEDFILES = ../*.[chm] ../*.cc ../*.in ../*.sh ../*.xs ../*.xbm ../gui_gtk_res.xml ../toolcheck ../proto ../libvterm ../vimtutor ../install-sh ../Make_all.mak

shadow:	runtime
	mkdir -p $(SHADOWDIR)
	cd $(SHADOWDIR); ln -s $(LINKEDFILES) .
	mkdir $(SHADOWDIR)/auto
	mkdir $(SHADOWDIR)/libs/wayland
	cd $(SHADOWDIR)/libs/wayland; ln -s ../../../libs/wayland/* .
	mkdir -p $(SHADOWDIR)/po
	cd $(SHADOWDIR)/po; ln -s ../../po/*.po ../../po/*.mak ../../po/*.vim ../../po/*.in ../../po/Makefile ../../po/*.c .
	cd $(SHADOWDIR); rm -f auto/link.sed
	cp Makefile configure $(SHADOWDIR)
	mkdir -p $(SHADOWDIR)/tests
	cd $(SHADOWDIR)/tests; ln -s ../../tests/Makefile \
				 ../../tests/Make_all.mak \
				 ../../tests/README.txt \
				 ../../tests/*.in \
				 ../../tests/*.vim \
				 ../../tests/*.py \
				 ../../tests/python* \
				 ../../tests/pyxfile \
				 ../../tests/ru_RU \
				 ../../tests/sautest \
				 ../../tests/samples \
				 ../../tests/util \
				 ../../tests/dumps \
				 ../../tests/*.ok \
				 ../../tests/testluaplugin \
				 .

# After updating Eegl new files may have been created, use this to refresh the
# symbolic links in the shadow directory. This isn't guaranteed to catch all
# changes, running "make shadow" again might sometimes be needed.
shadowupdate:
	ln -sf $(LINKEDFILES) .

# Link needed for doing "make install" in a shadow directory.
runtime:
	-ln -s ../runtime .
	-ln -s ../README.txt .
	-ln -s ../LICENSE .

# Update the synmenu.vim file with the latest Syntax menu.
# This is only needed when runtime/makemenu.vim was changed.
menu: ./vim ../runtime/makemenu.vim
	./vim --clean -X --not-a-term -S ../runtime/makemenu.vim

# Start configure from scratch
scrub scratch:
	-rm -f auto/config.status auto/config.cache config.log auto/config.log
	touch auto/config.h

distclean: clean scratch
	-rm -f tags

dist: distclean
	@echo
	@echo Making the distribution has to be done in the top directory

mdepend:
	-@rm -f Makefile~
	cp Makefile Makefile~
	sed -e '/\#\#\# Dependencies/q' < Makefile > tmp_make
	@for i in $(ALL_SRC) ; do \
	  echo "$$i" ; \
	  echo `echo "$$i" | sed -e 's/[^ ]*\.c$$/.b\/\1.o/'`": $$i" `\
	    $(CPP) $$i |\
	    grep '^# .*"\./.*\.h"' |\
	    sort -t'"' -u +1 -2 |\
	    sed -e 's/.*"\.\/\(.*\)".*/\1/'\
	    ` >> tmp_make ; \
	done
	mv tmp_make Makefile

depend:
	-@rm -f Makefile.bak
	cp Makefile Makefile.bak
	sed -e '/\#\#\# Dependencies/q' < Makefile > tmp_make
	-for i in $(ALL_LOCAL_SRC); do echo $$i; \
		$(CPP_DEPEND) $$i | \
		sed -e 's+^\([^ ]*\.o\)+.b/\1+' >> tmp_make; done
	mv tmp_make Makefile

# Run lint.  Clean up the *.ln files that are sometimes left behind.
lint:
	$(LINT) $(LINT_OPTIONS) $(LINT_FLAGS) $(LINT_EXTRA) $(LINT_SRC)
	-rm -f *.ln

# Check dosinst.c with lint.
lintinstall:
	$(LINT) $(LINT_OPTIONS) -DWIN32 -DUNIX_LINT dosinst.c
	-rm -f dosinst.ln

###########################################################################

.c.o:
	$(COMPILE) $<

os/.c.o:
	$(COMPILE) $<



# Dependencies through eegl.h that most targets depend on.  Used by targets
# that are not taken care of by "make depend".
VIM_H_DEPENDENCIES = eegl.h termdefs.h commands.h

# All the object files are put in the ".b" directory.  Since not all make
# commands understand putting object files in another directory, it must be
# specified for each file separately.

.b: .b/.dirstamp

.b/.dirstamp:
	mkdir -p .b
	mkdir -p .b/os
	touch .b/.dirstamp

# All object files depend on the objects directory, so that parallel make
# works.  Can't depend on the directory itself, its timestamp changes all the time.
$(ALL_OBJ): .b/.dirstamp

.b/%.o: src/%.c
	$(COMPILE) -o $@ $<

.b/diff.o: src/diff.c
	$(COMPILE) -o $@ $<

.b/option.o: src/option.c
	$(COMPILE) -o $@ $<

.b/regexp.o: src/regexp.c
	$(COMPILE) -o $@ $<

.b/ui.o: src/ui.c
	$(COMPILE) -o $@ $<

.b/window.o: src/window.c
	$(COMPILE) $(WAYLAND_FLAGS) -o $@ $<

.b/wlr-data-control-unstable-v1.o: libs/wayland/wlr-data-control-unstable-v1.c
	$(COMPILE) $(WAYLAND_FLAGS) -o $@ $<

.b/ext-data-control-v1.o: libs/wayland/ext-data-control-v1.c
	$(COMPILE) $(WAYLAND_FLAGS) -o $@ $< 

.b/xdg-shell.o: libs/wayland/xdg-shell.c
	$(COMPILE) $(WAYLAND_FLAGS) -o $@ $<

.b/primary-selection-unstable-v1.o: libs/wayland/primary-selection-unstable-v1.c
	$(COMPILE) $(WAYLAND_FLAGS) -o $@ $<


Makefile:
	@echo 'The name of the makefile MUST be "Makefile" (with capital M)!'


###############################################################################
### (automatically generated by 'make depend')
### Dependencies:
.b/book.o: src/book.c src/eegl.h \
 src/generic.h src/commands.h
.b/channel.o: src/channel.c src/eegl.h \
 src/generic.h src/commands.h
.b/clipboard.o: src/clipboard.c src/eegl.h \
 src/generic.h src/commands.h
.b/data.o: src/data.c src/eegl.h \
 src/generic.h src/commands.h
.b/diff.o: src/diff.c src/eegl.h src/generic.h src/commands.h
.b/do.o: src/do.c src/eegl.h \
 src/generic.h src/commands.h
.b/draw.o: src/draw.c src/eegl.h \
 src/generic.h src/commands.h
.b/eval.o: src/eval.c src/eegl.h \
 src/generic.h src/commands.h
.b/fileio.o: src/fileio.c src/eegl.h \
 src/generic.h src/commands.h
.b/input.o: src/input.c src/eegl.h \
 src/generic.h src/commands.h
.b/hilite.o: src/hilite.c src/eegl.h \
 src/generic.h src/commands.h
.b/insert.o: src/insert.c src/eegl.h \
 src/generic.h src/commands.h
.b/juggle.o: src/juggle.c src/eegl.h \
 src/generic.h src/commands.h
.b/location.o: src/location.c src/eegl.h \
 src/generic.h src/commands.h
.b/main.o: src/main.c src/eegl.h \
 src/generic.h src/commands.h
.b/mark.o: src/mark.c src/eegl.h \
 src/generic.h src/commands.h
.b/memory.o: src/memory.c src/eegl.h \
 src/generic.h src/commands.h
.b/message.o: src/message.c src/eegl.h \
 src/generic.h src/commands.h
.b/normal.o: src/normal.c src/eegl.h \
 src/generic.h src/commands.h src/actions.h src/indices/actions.h
.b/option.o: src/option.c src/eegl.h \
 src/generic.h src/commands.h
.b/portal.o: src/portal.c src/eegl.h \
 src/generic.h src/commands.h
.b/regexp.o: src/regexp.c src/eegl.h \
 src/generic.h src/commands.h
.b/script.o: src/script.c src/eegl.h \
 src/generic.h src/commands.h
.b/search.o: src/search.c src/eegl.h \
 src/generic.h src/commands.h
.b/persist.o: src/persist.c src/eegl.h \
 src/generic.h src/commands.h
.b/strings.o: src/strings.c src/eegl.h \
 src/generic.h src/commands.h
.b/syntax.o: src/syntax.c src/eegl.h \
 src/generic.h src/commands.h
.b/tag.o: src/tag.c src/eegl.h \
 src/generic.h src/commands.h
.b/term.o: src/term.c src/eegl.h \
 src/generic.h src/commands.h
.b/ui.o: src/ui.c src/eegl.h \
 src/generic.h src/commands.h
.b/ui.o: src/ui.c src/eegl.h \
 src/generic.h src/commands.h
.b/json_test.o: src/json_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/strings.c
.b/kword_test.o: src/kword_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/strings.c
.b/memfile_test.o: src/memfile_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/memory.c
.b/message_test.o: src/message_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/message.c
.b/channel.o: src/channel.c src/eegl.h \
 src/generic.h src/commands.h
.b/wlr-data-control-unstable-v1.o: \
 libs/wayland/wlr-data-control-unstable-v1.c
.b/ext-data-control-v1.o: libs/wayland/ext-data-control-v1.c
.b/xdg-shell.o: libs/wayland/xdg-shell.c
.b/primary-selection-unstable-v1.o: \
 libs/wayland/primary-selection-unstable-v1.c
.b/window.o: src/window.c src/eegl.h \
 src/generic.h src/commands.h
 
