CC ?= gcc
INTERNAL_CFLAGS = --std=c17 -gdwarf-5 -pthread -Wp,-D_FORTIFY_SOURCE=2 -fno-plt \
      -fstack-clash-protection -fno-stack-protector -fno-semantic-interposition \
      -fdebug-prefix-map=$(shell pwd)=.

# The debug flags
CFLAGS ?=  $(INTERNAL_CFLAGS) -Wall -Wextra -Wfatal-errors -O0 \
              -Wno-cpp -Werror=return-type -Werror=pointer-compare \
# The release flags
RELEASE_CFLAGS = $(INTERNAL_CFLAGS) -O2

LDFLAGS ?= -L/usr/lib -Wl,-z,relro,-z,now 

LIBS	= -lm -ltinfo -lwayland-client


.RECIPEPREFIX = /

#
# Compiling Eegl, summary:
#
#	make
#	doas make install
#
# Compiling Eegl, details:
#
# Edit this file for adjusting to your system. You should not need to edit any
# other file for machine specific things!
#{{{ config


VIEWNAME	= view

srcdir = src

TAGPRG		= ctags

CPP		?= gcc -E
CPP_MM		= M
DEPEND_FLAGS_FILTER = | sed 's+-I */+-isystem /+g'

OBJDIR ?= ../.b/eegl

WAYLAND_SRC	= libs/wayland/ext-data-control-v1.c \
        libs/wayland/xdg-shell.c       libs/wayland/primary-selection-unstable-v1.c
WAYLAND_OBJ	= $(OBJDIR)/ext-data-control-v1.o \
   $(OBJDIR)/xdg-shell.o \
   $(OBJDIR)/primary-selection-unstable-v1.o
WAYLAND_FLAGS  = 



CHANNEL_OBJ	= $(OBJDIR)/channel.o

CROSS_COMPILING = 
COMPILEDBY	= 

INSTALLVIMDIFF	= installvimdiff
INSTALL_LANGS	= install-languages
INSTALL_TOOL_LANGS	= install-tool-languages

### Line break character as octal number for "tr"
NL		= "\\012"

### Top directory for everything
PREFIX ?= /usr

### Top directory for the binary
exec_prefix	= $(PREFIX)/bin

### Prefix for location of data files
BINDIR		= $(PREFIX)/share


### Prefix for location of data files
DATADIR		= ${datarootdir}

### Prefix for location of man pages
MANDIR		= ${datarootdir}/man




### If the *.po files are to be translated to *.mo files.
MAKEMO		= yes

MSGFMT		= msgfmt
MSGFMTCMD	= OLD_PO_FILE_INPUT=yes msgfmt --no-convert -v
MSGFMT_DESKTOP	= eegl.desktop
APP = eegl

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
#	- Edit the INTERLAL_CFLAGS and/or RELEASE_CFLAGS and/or CC if you have
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
# 4. "make test"  {{{1
# This is optional.  This will run Eegl scripts on a number of test files, and compare the 
# produced output with the expected output. If all is well, you will get the "ALL DONE" message 
# in the end. If a test fails, you get "TEST FAILURE".  See below (search for "/^test").
# 5. "make install"  {{{1
# If the new Eegl seems to be working OK you can install it and the documentation in the 
# appropriate location. The default is
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

# Argument for running ctags.
TAGS_FILES = *.c *.h


#{{{what used to be auto/config.mk


INDICES_FLAGS	= --std=c17 -Wfatal-errors -g3 -O0 -Wno-cpp -Werror=return-type

DEPEND_FLAGS_FILTER = | sed 's;-I */;-isystem /;g'


### Line break character as octal number for "tr"
NL		= "\\012"



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


# COMPILED BY - For including a specific e-mail address for ":version".
#CONF_OPT_COMPBY = "--with-compiledby=John Doe <JohnDoe@yahoo.com>"

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

#EFENCE - Electric-Fence malloc debugging: catches memory accesses beyond
#allocated memory (and makes every malloc()/free() very slow).
#Electric Fence is free (search ftp sites).
#You may want to set the EF_PROTECT_BELOW environment variable to check the
# other side of allocated memory.
#EXTRA_LIBS = /usr/local/lib/libefence.a


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
#	$ lcov -c -i -b . -d objects -o $(OBJDIR)/coverage_base.info
# 3. Run "make test" to run the unit tests.  The code coverage information will
#    be generated in the src/objects directory.
# 4. Generate the code coverage information from the tests:
#	$ lcov -c -b . -d $(OBJDIR)/ -o $(OBJDIR)/coverage_test.info
# 5. Combine the baseline and test code coverage data:
#	$ lcov -a $(OBJDIR)/coverage_base.info -a objects/coverage_test.info -o objects/coverage_total.info
# 6. Process the test coverage data and generate a report in html:
#	$ genhtml $(OBJDIR)/coverage_total.info -o objects
# 7. Open the $(OBJDIR)/index.html file in a web browser to view the coverage
#    information.
#
# LDFLAGS=--coverage
# PROFILE_FLAGS=-g -O0 -fprofile-arcs -ftest-coverage -DWE_ARE_PROFILING -DUSE_GCOV_FLUSH
# Alternate flags
# PROFILE_FLAGS=-g -O0 --coverage -DWE_ARE_PROFILING -DUSE_GCOV_FLUSH


#Uncomment the next lines to compile Eegl on GCC with the address sanitizer (asan) and with the 
#undefined sanitizer.
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
EEGLTARGET	= ../bin/$(APP)
VIEWTARGET	= $(VIEWNAME)$(LNKEXT)
EEGLDIFFNAME	= eegldiff
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

### DESTDIR	root of the installation tree.  This is prepended to the other
#		directories.  This directory must exist.
DESTDIR  ?= .

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

###

CPROTO_FLAGS = -DPROTO -d -E"$(CPP)" -I./src #$(NO_ATTR) # -D"__typeof__\\(x\\)=x"


################################################
##   no changes required below this line      ##
################################################

SHELL = /usr/bin/bash

PRE_DEFS = -iquote=src/proto

ALL_FLAGS = $(PRE_DEFS) $(CFLAGS) $(PROFILE_FLAGS) $(SANITIZER_FLAGS) $(LEAK_FLAGS) \
   $(ABORT_FLAGS)


LINT_FLAGS = -DLINT -I. $(PRE_DEFS) -Dinline= -D__extension__= -Dalloca=alloca
LINT_FLAGS_CPROTO = -DLINT -Isrc -Isrc/proto #-Dinline= -D__extension__= -Dalloca=alloca

LINT_EXTRA = -D"__attribute__(x)="

DEPEND_FLAGS = -DPROTO -DDEPEND $(LINT_FLAGS)

ALL_LIBS = \
/    $(LIBS) \
/    $(EXTRA_LIBS) \
/    $(PROFILE_LIBS) \
/    $(SANITIZER_LIBS) \
/    $(LEAK_LIBS)

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
DEST_MAN_RU = $(DEST_MAN_TOP)/ru.KOI8-R$(MAN1DIR)
DEST_MAN_RU_U = $(DEST_MAN_TOP)/ru.UTF-8$(MAN1DIR)


# get the list of tests
include tests/Make_all.mak

#	     BASIC_SRC: files that are always used
#	       GUI_SRC: extra GUI files for current configuration
#	   ALL_GUI_SRC: all GUI files for Unix
#
#		   SRC: files used for current configuration
#	       ALL_SRC: source files used for make depend and make lint

BASIC_SRC_NO_DIR = \
/ book.c \
   channel.c \
/ data.c \
/ diff.c \
/ do.c \
/ draw.c \
/ eval.c \
/ fileio.c \
/ hilite.c \
/ input.c \
/ insert.c \
/ juggle.c \
/ location.c \
/ memory.c \
/ message.c \
/ motor.c \
/ normal.c \
/ option.c \
/ persist.c \
/ portal.c \
/ regexp.c \
/ script.c \
/ search.c \
/ strings.c \
/ tag.c \
/ term.c \
/ ui.c \
   window.c

BASIC_SRC = $(addprefix src/, $(BASIC_SRC_NO_DIR))


SRC =	$(BASIC_SRC) $(WAYLAND_SRC)

EXTRA_SRC = src/channel.c \
/     $(GRESOURCE_SRC)

$(WAYLAND_SRC):
/ cd libs/wayland; $(MAKE)

# Needed for parallel jobs to work
libs/wayland/ext-data-control-v1.h: libs/wayland/ext-data-control-v1.c
libs/wayland/wlr-data-control-unstable-v1.h: libs/wayland/wlr-data-control-unstable-v1.c
libs/wayland/primary-selection-unstable-v1.h: libs/wayland/primary-selection-unstable-v1.c
libs/wayland/xdg-shell.h: libs/wayland/xdg-shell.c

# Unittest files
JSON_TEST_SRC = src/json_test.c
JSON_TEST_TARGET = src/json_test
KWORD_TEST_SRC = src/kword_test.c
KWORD_TEST_TARGET = src/kword_test
MEMFILE_TEST_SRC = src/memfile_test.c
MEMFILE_TEST_TARGET = src/memfile_test
MESSAGE_TEST_SRC = src/message_test.c
MESSAGE_TEST_TARGET = src/message_test

UNITTEST_SRC = $(JSON_TEST_SRC) $(KWORD_TEST_SRC) $(MEMFILE_TEST_SRC) $(MESSAGE_TEST_SRC)
UNITTEST_TARGETS = $(JSON_TEST_TARGET) $(KWORD_TEST_TARGET) $(MEMFILE_TEST_TARGET) $(MESSAGE_TEST_TARGET)
RUN_UNITTESTS = run_json_test run_kword_test run_memfile_test run_message_test

# All sources, also the ones that are not configured
ALL_LOCAL_SRC = $(BASIC_SRC) $(UNITTEST_SRC) $(EXTRA_SRC) $(WAYLAND_SRC)
ALL_SRC = $(ALL_LOCAL_SRC)

# Which files to check with lint.  Select one of these three lines. 
LINT_SRC = $(ALL_SRC)

OBJ_COMMON = \
 $(OBJDIR)/book.o \
 $(OBJDIR)/data.o \
 $(OBJDIR)/diff.o \
 $(OBJDIR)/do.o \
 $(OBJDIR)/draw.o \
 $(OBJDIR)/eval.o \
 $(OBJDIR)/fileio.o \
 $(OBJDIR)/hilite.o \
 $(OBJDIR)/input.o \
 $(OBJDIR)/insert.o \
 $(OBJDIR)/juggle.o \
 $(OBJDIR)/location.o \
 $(OBJDIR)/motor.o \
 $(OBJDIR)/normal.o \
 $(OBJDIR)/option.o \
 $(OBJDIR)/persist.o \
 $(OBJDIR)/portal.o \
 $(OBJDIR)/regexp.o \
 $(OBJDIR)/script.o \
 $(OBJDIR)/search.o \
 $(OBJDIR)/tag.o \
 $(OBJDIR)/term.o \
 $(OBJDIR)/ui.o \
 $(OBJDIR)/window.o \
 $(WAYLAND_OBJ) \
 $(CHANNEL_OBJ)

# The files included by tests are not in OBJ_COMMON.
OBJ_MAIN = \
/ $(OBJDIR)/strings.o \
/ $(OBJDIR)/main.o \
/ $(OBJDIR)/memory.o \
/ $(OBJDIR)/message.o

OBJ = $(OBJ_COMMON) $(OBJ_MAIN)

OBJ_JSON_TEST = \
/ $(OBJDIR)/strings.o \
/ $(OBJDIR)/memory.o \
/ $(OBJDIR)/message.o \
/ $(OBJDIR)/json_test.o

JSON_TEST_OBJ = $(OBJ_COMMON) $(OBJ_JSON_TEST)

OBJ_KWORD_TEST = \
/ $(OBJDIR)/strings.o \
/ $(OBJDIR)/memory.o \
/ $(OBJDIR)/message.o \
/ $(OBJDIR)/kword_test.o

KWORD_TEST_OBJ = $(OBJ_COMMON) $(OBJ_KWORD_TEST)

OBJ_MEMFILE_TEST = \
/ $(OBJDIR)/strings.o \
/ $(OBJDIR)/strings.o \
/ $(OBJDIR)/message.o \
/ $(OBJDIR)/memfile_test.o

MEMFILE_TEST_OBJ = $(OBJ_COMMON) $(OBJ_MEMFILE_TEST)

OBJ_MESSAGE_TEST = \
/ $(OBJDIR)/strings.o \
/ $(OBJDIR)/strings.o \
/ $(OBJDIR)/memory.o \
/ $(OBJDIR)/message_test.o

MESSAGE_TEST_OBJ = $(OBJ_COMMON) $(OBJ_MESSAGE_TEST)

ALL_OBJ = $(OBJ_COMMON) \
/   $(OBJ_MAIN) \
/   $(OBJ_JSON_TEST) \
/   $(OBJ_KWORD_TEST) \
/   $(OBJ_MEMFILE_TEST) \
/   $(OBJ_MESSAGE_TEST)


PRO_AUTO = \
/ alloc.h \
/ book.h \
/ change.h \
/ channel.h \
/ dict.h \
/ diff.h \
/ do.h \
/ draw.h \
/ eval.h \
/ fileio.h \
/ hilite.h \
/ input.h \
/ insert.h \
/ juggle.h \
/ list.h \
/ location.h \
/ mark.h \
/ memory.h \
/ message.h \
/ motor.h \
/ normal.h \
/ option.h \
/ unix.h \
/ persist.h \
/ portal.h \
/ regexp.h \
/ script.h \
/ search.h \
/ sound.h \
/ strings.h \
/ tag.h \
/ term.h \
/ ui.h \
/ window.h

# Default target is making the executable and tools
all: $(EEGLTARGET) $(TOOLS) languages

tools: $(TOOLS)



# Run the script to generate the Command lookup table and the normal/visual mode command lookup 
# tables. This only needs to be run when command has been added or changed.
# If this fails because you don't have Eegl yet, first build and install Eegl without changes.
indices: src/commands.h src/actions.h
/ $(CC) -iquote src $(INDICES_FLAGS) dev/indexGenerator.c -o $(OBJDIR)/indexGenerator
/ $(OBJDIR)/indexGenerator actions
/ $(OBJDIR)/indexGenerator commands
/ $(OBJDIR)/indexGenerator options


better: ##Better C: codegen for headers & generics
/ $(CC) dev/betterc.c -o $(OBJDIR)/betterc
/ $(OBJDIR)/betterc -d proto src/book.c

# The normal command to compile a .c file to its .o file.
# Without or with ALL_FLAGS.
COMPILE = $(CC) -c -iquote $(srcdir) $(ALL_FLAGS)
CClink = $(CC)

# MAIN. LINK the target for normal use or debugging.
# A shell script is used to try linking without unnecessary libraries.
$(EEGLTARGET): $(OBJ)
/ $(CClink) $(LDFLAGS) -o $(EEGLTARGET) $(OBJ) $(ALL_LIBS)
/ @echo "                               "
/ @echo "         .^^~-.                "
/ @echo '         / ,__`)               '
/ @echo "        |   \o/|'--.           "
/ @echo "  BUILD  \     /___ \ SUCCESS! "
/ @echo "         |     '---\/          "
/ @echo "         /     \               "
/ @echo "        / ,  ,  \              "
/ @echo "        \`-'--'--'             "
/ @echo "                               "

# Build the language specific files if they were unpacked.
# Generate the converted .mo files separately, it's no problem if this fails.
languages:
/ @if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
/ 	cd $(PODIR); \
/ 	  CC="$(CC)" make prefix=$(DESTDIR)$(prefix) originals; \
/ fi
/ -@if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
/ 	cd $(PODIR); \
/ 	  CC="$(CC)" make prefix=$(DESTDIR)$(prefix) converted; \
/ fi

# Update the *.po files for changes in the sources.  Only run manually.
update-po:
/ cd $(PODIR); CC="$(CC)" $(MAKE) prefix=$(DESTDIR)$(prefix) update-po

# Generate function prototypes.  This is not needed to compile Eegl, but if
# you want to use it, cproto is out there on the net somewhere -- Webb


# Filter out arguments that cproto doesn't support.
# Don't pass "-pthread", "-fwrapv" and similar arguments to cproto, it sees
# them as a list of individual flags.
# The -E"gcc -E" argument must be separate to avoid problems with shell
# quoting.
# Strip -O2, it may cause cproto to write stderr to the file "2".
CPROTO = cproto $(CPROTO_FLAGS) $(LINT_FLAGS_CPROTO)



PROTO_RESULTS := $(addprefix src/proto/,$(patsubst %.c,%.h,$(BASIC_SRC_NO_DIR)))

src/proto/%.h: src/%.c
/ $(CPROTO) $< > $@

proto: $(PROTO_RESULTS) $(addprefix src/proto/,$(PRO_MANUAL))

notags:
/ -rm -f tags

# Note: tags is made for the currently configured version.
# You can ignore error messages for missing files.
tags TAGS: notags
/ $(TAGPRG) $(TAGS_FILES)

# Build the cscope database.
# This may search more files than necessary.
.PHONY: cscope csclean all indices better update-po

csclean:
/ -rm -vf cscope.out
cscope.out:
/ cscope -bv ./*.[ch] src/proto/*.h
cscope: csclean cscope.out  ;

# Make a hilite file for types.  Requires Exuberant ctags and awk
types: types.vim
types.vim: $(TAGS_FILES)
/ ctags --c-kinds=gstu -o- $(TAGS_FILES) |\
/ 	awk 'BEGIN{printf("syntax keyword Type\t")}\
/ 		{printf("%s ", $$1)}END{print ""}' > $@
/ echo "syn keyword Constant OK FAIL TRUE FALSE MAYBE" >> $@

# TESTING
#
# Execute the test scripts and the unittests.
# Do the scripttests first, so that the summary shows last.
test check: unittests scripttests

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
/ $(MAKE) -f Makefile $(EEGLTARGET)
/ if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
/ 	cd $(PODIR); $(MAKE) -f Makefile check VIMPROG=../$(EEGLTARGET); \
/ fi
/ -if test $(EEGLTARGET) != eegl -a ! -r eegl; then \
/ 	ln -s $(EEGLTARGET) eegl; \
/ fi
/ cd tests;\
   $(MAKE) -f Makefile VIMPROG=../$(EEGLTARGET) SCRIPTSOURCE=../$(SCRIPTSOURCE)

testtiny:
/ cd tests; $(MAKE) -f Makefile tiny VIMPROG=../$(EEGLTARGET) SCRIPTSOURCE=../$(SCRIPTSOURCE)

# Run benchmarks.
benchmark:
/ cd tests; \
/ 	$(MAKE) -f Makefile benchmarkclean; \
/ 	$(MAKE) -f Makefile benchmark VIMPROG=../$(EEGLTARGET) SCRIPTSOURCE=../$(SCRIPTSOURCE)

unittesttargets:
/ $(MAKE) -f Makefile $(UNITTEST_TARGETS)

VIMTESTTARGET = $(EEGLTARGET)

# Execute the unittests one by one.
unittest unittests: $(RUN_UNITTESTS)

run_json_test: $(JSON_TEST_TARGET)
/ $(VALGRIND) ./$(JSON_TEST_TARGET) || exit 1; echo $* passed;

run_kword_test: $(KWORD_TEST_TARGET)
/ $(VALGRIND) ./$(KWORD_TEST_TARGET) || exit 1; echo $* passed;

run_memfile_test: $(MEMFILE_TEST_TARGET)
/ $(VALGRIND) ./$(MEMFILE_TEST_TARGET) || exit 1; echo $* passed;

run_message_test: $(MESSAGE_TEST_TARGET)
/ $(VALGRIND) ./$(MESSAGE_TEST_TARGET) || exit 1; echo $* passed;

# Run individual OLD style test.
# These do not depend on the executable, compile it when needed.
$(SCRIPTS_TINY):
/ cd tests; rm -f $@.out; $(MAKE) -f Makefile $@.out VIMPROG=../$(VIMTESTTARGET) $(GUI_TESTARG) SCRIPTSOURCE=../$(SCRIPTSOURCE)

# Run individual NEW style test.
# These do not depend on the executable, compile it when needed.
# Set $TEST_FILTER to select what test function to invoke, e.g.:
#	export TEST_FILTER=Test_terminal_wipe_buffer
# A partial match also works:
#	export TEST_FILTER=wipe_buffer
$(NEW_TESTS) test_vim9:
/ cd tests; $(MAKE) $@ VIMPROG=../$(VIMTESTTARGET) $(GUI_TESTARG) SCRIPTSOURCE=../$(SCRIPTSOURCE)

newtests:
/ cd tests; rm -f $@.res test.log messages; $(MAKE) -f Makefile newtestssilent VIMPROG=../$(VIMTESTTARGET) $(GUI_TESTARG) SCRIPTSOURCE=../$(SCRIPTSOURCE)
/ @if test -f tests/test.log; then \
/ 	cat tests/test.log; \
/ fi
/ cat tests/messages

testclean:
/ cd tests; $(MAKE) -f Makefile clean
/ if test -d $(PODIR); then \
/ 	cd $(PODIR); $(MAKE) checkclean; \
/ fi

# Unittests
# It's build just like Eegl to satisfy all dependencies.
$(JSON_TEST_TARGET): $(JSON_TEST_OBJ)
/ @LINK="$(CClink) $(LDFLAGS) \
/ 	-o $(JSON_TEST_TARGET) $(JSON_TEST_OBJ) $(ALL_LIBS)" \
/ 	MAKE="$(MAKE)" LINK_AS_NEEDED=yes \
/ 	PROG="json_test" \
/ 	sh $(srcdir)/link.sh

$(KWORD_TEST_TARGET): $(KWORD_TEST_OBJ)
/ @LINK="$(CClink) $(LDFLAGS) \
/ 	-o $(KWORD_TEST_TARGET) $(KWORD_TEST_OBJ) $(ALL_LIBS)" \
/ 	MAKE="$(MAKE)" LINK_AS_NEEDED=yes \
/ 	PROG="kword_test" \
/ 	sh $(srcdir)/link.sh

$(MEMFILE_TEST_TARGET): $(MEMFILE_TEST_OBJ)
/ @LINK="$(CClink) $(LDFLAGS) \
/ 	-o $(MEMFILE_TEST_TARGET) $(MEMFILE_TEST_OBJ) $(ALL_LIBS)" \
/ 	MAKE="$(MAKE)" LINK_AS_NEEDED=yes \
/ 	PROG="memfile_test" \
/ 	sh $(srcdir)/link.sh

$(MESSAGE_TEST_TARGET): $(MESSAGE_TEST_OBJ)
/ @LINK="$(CClink) $(LDFLAGS) \
/ 	-o $(MESSAGE_TEST_TARGET) $(MESSAGE_TEST_OBJ) $(ALL_LIBS)" \
/ 	MAKE="$(MAKE)" LINK_AS_NEEDED=yes \
/ 	PROG="message_test" \
/ 	sh $(srcdir)/link.sh

# install targets

install: $(GUI_INSTALL)

install_normal: installvim installtools $(INSTALL_LANGS) install-icons

installvim: installvimbin installtutorbin \
/ 	installruntime installlinks installmanlinks

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
/ -if test -f $(DEST_BIN)/$(EEGLTARGET); then \
/   mv -f $(DEST_BIN)/$(EEGLTARGET) $(DEST_BIN)/eegl.rm; \
/   rm -f $(DEST_BIN)/eegl.rm; \
/ fi
/ $(INSTALL_PROG) $(EEGLTARGET) $(DEST_BIN)
/ strip $(DEST_BIN)/$(EEGLTARGET)
/ chmod $(BINMOD) $(DEST_BIN)/$(EEGLTARGET)
# may create a link to the new executable from /usr/bin/vi
/ -$(LINKIT)

# Long list of arguments for the shell script that installs the manual pages
# for one language.
INSTALLMANARGS = $(VIMLOC) $(SCRIPTLOC) $(VIMRCLOC) $(HELPSOURCE) $(MANMOD) \
/ 	eegl $(EEGLDIFFNAME) $(EEEGLNAME)

# Install most of the runtime files
installruntime: installrtbase installmacros installpack installtutor installspell

# Install the help files; first adjust the contents for the final location.
# Also install most of the other runtime files.
installrtbase: $(HELPSOURCE)/vim.1 $(DEST_VIM) $(EEGLTARGET) $(DEST_RT) \
/ 	$(DEST_HELP) $(DEST_PRINT) $(DEST_COL) \
/ 	$(DEST_SYN) $(DEST_SYN)/modula2 $(DEST_SYN)/modula2/opt $(DEST_SYN)/shared \
/ 	$(DEST_IND) $(DEST_FTP) \
/ 	$(DEST_AUTO) $(DEST_AUTO)/dist $(DEST_AUTO)/xml \
/ 	$(DEST_AUTO)/rust $(DEST_AUTO)/cargo \
/ 	$(DEST_IMPORT) $(DEST_IMPORT)/dist \
/ 	$(DEST_PLUG) \
/        	$(DEST_TUTOR) $(DEST_TUTOR)/en $(DEST_TUTOR)/it $(DEST_TUTOR)/sr \
/ 	$(DEST_TUTOR)/ru \
/ 	$(DEST_SPELL) $(DEST_COMP)
/ -$(SHELL) ./installman.sh install $(DEST_MAN) "" $(INSTALLMANARGS)
# Generate the help tags with ":helptags" to handle all languages.
# Move the distributed tags file aside and restore it, to avoid it being
# different from the repository.
/ cd $(HELPSOURCE); if test -z "$(CROSS_COMPILING)" -a -f tags; then \
/ 	mv -f tags tags.dist; fi
/ @echo generating help tags
/ -@BUILD_DIR=`pwd`; cd $(HELPSOURCE); if test -z "$(CROSS_COMPILING)"; then \
/ 	$(MAKE) VIMPROG="$$BUILD_DIR/$(EEGLTARGET)" vimtags; fi
/ cd $(HELPSOURCE); \
/ 	files=`ls *.txt tags`; \
/ 	files="$$files `ls *.??x tags-?? 2>/dev/null || true`"; \
/ 	cp $$files  $(DEST_HELP); \
/ 	cd $(DEST_HELP); \
/ 	chmod $(HELPMOD) $$files
/ cp  $(HELPSOURCE)/*.pl $(DEST_HELP)
/ chmod $(SCRIPTMOD) $(DEST_HELP)/*.pl
/ cd $(HELPSOURCE); if test -f tags.dist; then mv -f tags.dist tags; fi
# install the menu files
/ cp $(SCRIPTSOURCE)/menu.vim $(SYS_MENU_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_MENU_FILE)
/ cp $(SCRIPTSOURCE)/synmenu.vim $(SYS_SYNMENU_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_SYNMENU_FILE)
/ cp $(SCRIPTSOURCE)/delmenu.vim $(SYS_DELMENU_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_DELMENU_FILE)
# install the defaults/evim/mswin file
/ cp $(SCRIPTSOURCE)/defaults.vim $(VIM_DEFAULTS_FILE)
/ chmod $(VIMSCRIPTMOD) $(VIM_DEFAULTS_FILE)
/ cp $(SCRIPTSOURCE)/evim.vim $(EVIM_FILE)
/ chmod $(VIMSCRIPTMOD) $(EVIM_FILE)
/ cp $(SCRIPTSOURCE)/mswin.vim $(MSWIN_FILE)
/ chmod $(VIMSCRIPTMOD) $(MSWIN_FILE)
# install the bugreport file
/ cp $(SCRIPTSOURCE)/bugreport.vim $(SYS_BUGR_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_BUGR_FILE)
# install the example vimrc files
/ cp $(SCRIPTSOURCE)/vimrc_example.vim $(DEST_SCRIPT)
/ chmod $(VIMSCRIPTMOD) $(DEST_SCRIPT)/vimrc_example.vim
/ cp $(SCRIPTSOURCE)/gvimrc_example.vim $(DEST_SCRIPT)
/ chmod $(VIMSCRIPTMOD) $(DEST_SCRIPT)/gvimrc_example.vim
# install the file type detection files
/ cp $(SCRIPTSOURCE)/filetype.vim $(SYS_FILETYPE_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_FILETYPE_FILE)
/ cp $(SCRIPTSOURCE)/ftoff.vim $(SYS_FTOFF_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_FTOFF_FILE)
/ cp $(SCRIPTSOURCE)/scripts.vim $(SYS_SCRIPTS_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_SCRIPTS_FILE)
/ cp $(SCRIPTSOURCE)/ftplugin.vim $(SYS_FTPLUGIN_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_FTPLUGIN_FILE)
/ cp $(SCRIPTSOURCE)/ftplugof.vim $(SYS_FTPLUGOF_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_FTPLUGOF_FILE)
/ cp $(SCRIPTSOURCE)/indent.vim $(SYS_INDENT_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_INDENT_FILE)
/ cp $(SCRIPTSOURCE)/indoff.vim $(SYS_INDOFF_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_INDOFF_FILE)
/ cp $(SCRIPTSOURCE)/optwin.vim $(SYS_OPTWIN_FILE)
/ chmod $(VIMSCRIPTMOD) $(SYS_OPTWIN_FILE)
# install README and LICENCE files
/ cp ../README.txt $(DEST_RT)
/ chmod $(HELPMOD) $(DEST_RT)/README.txt
/ cp ../LICENSE $(DEST_RT)
/ chmod $(HELPMOD) $(DEST_RT)/LICENSE
# install the print resource files
/ cd $(PRINTSOURCE); cp *.ps $(DEST_PRINT)
/ cd $(DEST_PRINT); chmod $(FILEMOD) *.ps
# install the colorscheme files
/ cd $(COLSOURCE); cp -r *.vim lists tools README.txt $(DEST_COL)
/ cd $(DEST_COL); chmod $(DIRMOD) lists tools
/ cd $(DEST_COL); chmod $(HELPMOD) *.vim README.txt lists/*.vim tools/*.vim
# install the syntax files
/ cd $(SYNSOURCE); cp *.vim README.txt $(DEST_SYN)
/ cd $(DEST_SYN); chmod $(HELPMOD) *.vim README.txt
/ cd $(SYNSOURCE)/shared; cp *.vim README.txt $(DEST_SYN)/shared
/ cd $(DEST_SYN)/shared; chmod $(HELPMOD) *.vim README.txt
/ cd $(SYNSOURCE)/modula2/opt; cp *.vim $(DEST_SYN)/modula2/opt
/ cd $(DEST_SYN)/modula2/opt; chmod $(HELPMOD) *.vim
# install the indent files
/ cd $(INDSOURCE); cp *.vim README.txt $(DEST_IND)
/ cd $(DEST_IND); chmod $(HELPMOD) *.vim README.txt
# install the standard autoload files
/ cd $(AUTOSOURCE); cp *.vim README.txt $(DEST_AUTO)
/ cd $(DEST_AUTO); chmod $(HELPMOD) *.vim README.txt
/ cd $(AUTOSOURCE)/dist; cp *.vim $(DEST_AUTO)/dist
/ cd $(DEST_AUTO)/dist; chmod $(HELPMOD) *.vim
/ cd $(AUTOSOURCE)/xml; cp *.vim $(DEST_AUTO)/xml
/ cd $(DEST_AUTO)/xml; chmod $(HELPMOD) *.vim
/ cd $(AUTOSOURCE)/cargo; cp *.vim $(DEST_AUTO)/cargo
/ cd $(DEST_AUTO)/cargo; chmod $(HELPMOD) *.vim
/ cd $(AUTOSOURCE)/rust; cp *.vim $(DEST_AUTO)/rust
/ cd $(DEST_AUTO)/rust; chmod $(HELPMOD) *.vim
# install the standard import files
/ cd $(IMPORTSOURCE)/dist; cp *.vim $(DEST_IMPORT)/dist
/ cd $(DEST_IMPORT)/dist; chmod $(HELPMOD) *.vim
# install the standard plugin files
/ cd $(PLUGSOURCE); cp *.vim README.txt $(DEST_PLUG)
/ cd $(DEST_PLUG); chmod $(HELPMOD) *.vim README.txt
# install the ftplugin files
/ cd $(FTPLUGSOURCE); cp *.vim README.txt logtalk.dict $(DEST_FTP)
/ cd $(DEST_FTP); chmod $(HELPMOD) *.vim README.txt logtalk.dict
# install the compiler files
/ cd $(COMPSOURCE); cp *.vim README.txt $(DEST_COMP)
/ cd $(DEST_COMP); chmod $(HELPMOD) *.vim README.txt

installmacros: $(DEST_VIM) $(DEST_RT) $(DEST_MACRO)
/ cp -r $(MACROSOURCE)/* $(DEST_MACRO)
/ chmod $(DIRMOD) `find $(DEST_MACRO) -type d -print`
/ chmod $(FILEMOD) `find $(DEST_MACRO) -type f -print`
/ chmod $(SCRIPTMOD) $(DEST_MACRO)/less.sh
# When using CVS some CVS directories might have been copied.
# Also delete AAPDIR and *.info files.
/ cvs=`find $(DEST_MACRO) \( -name CVS -o -name AAPDIR -o -name "*.info" \) -print`; \
/       if test -n "$$cvs"; then \
/ 	 rm -rf $$cvs; \
/       fi

installpack: $(DEST_VIM) $(DEST_RT) $(DEST_PACK)
/ cp -r $(PACKSOURCE)/* $(DEST_PACK)
/ chmod $(DIRMOD) `find $(DEST_PACK) -type d -print`
/ chmod $(FILEMOD) `find $(DEST_PACK) -type f -print`

# install the tutor files
installtutorbin: $(DEST_BIN)
/ cp scripts/vimtutor $(DEST_BIN)/eegltutor
/ chmod $(SCRIPTMOD) $(DEST_BIN)/eegltutor


installtutor: $(DEST_RT) $(DEST_TUTOR)/en $(DEST_TUTOR)/it $(DEST_TUTOR)/sr $(DEST_TUTOR)/ru
/ -cp $(TUTORSOURCE)/README* $(TUTORSOURCE)/tutor* $(DEST_TUTOR)
/ -cp $(TUTORSOURCE)/en/* $(DEST_TUTOR)/en/
/ -cp $(TUTORSOURCE)/it/* $(DEST_TUTOR)/it/
/ -cp $(TUTORSOURCE)/ru/* $(DEST_TUTOR)/ru/
/ -cp $(TUTORSOURCE)/sr/* $(DEST_TUTOR)/sr/
/ -rm -f $(DEST_TUTOR)/*.info
/ chmod $(HELPMOD) $(DEST_TUTOR)/*
/ chmod $(DIRMOD) $(DEST_TUTOR)/en
/ chmod $(DIRMOD) $(DEST_TUTOR)/it
/ chmod $(DIRMOD) $(DEST_TUTOR)/ru
/ chmod $(DIRMOD) $(DEST_TUTOR)/sr

# install the runtime tools
/ cp -r $(TOOLSSOURCE)/* $(DEST_TOOLS)
# When using CVS some CVS directories might have been copied.
/ cvs=`find $(DEST_TOOLS) \( -name CVS -o -name AAPDIR \) -print`; \
/       if test -n "$$cvs"; then \
/ 	 rm -rf $$cvs; \
/       fi
/ -chmod $(FILEMOD) $(DEST_TOOLS)/*
# replace the path in some tools
/ awkpath=`which nawk` && sed -e "s+/usr/bin/nawk+$$awkpath+" $(TOOLSSOURCE)/mve.awk >$(DEST_TOOLS)/mve.awk; if test -z "$$awkpath"; then \
/ 	awkpath=`which gawk` && sed -e "s+/usr/bin/nawk+$$awkpath+" $(TOOLSSOURCE)/mve.awk >$(DEST_TOOLS)/mve.awk; if test -z "$$awkpath"; then \
/ 	awkpath=`which awk` && sed -e "s+/usr/bin/nawk+$$awkpath+" $(TOOLSSOURCE)/mve.awk >$(DEST_TOOLS)/mve.awk; fi; fi
/ -chmod $(SCRIPTMOD) `grep -l "^#!" $(DEST_TOOLS)/*`


# install the language specific files, if they were unpacked
install-languages: languages $(DEST_LANG) $(DEST_KMAP) $(DEST_RT)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_DA) "-da" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_DA_I) "-da" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_DA_U) "-da.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_DE) "-de" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_DE_I) "-de" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_DE_U) "-de.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_FR) "-fr" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_FR_I) "-fr" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_FR_U) "-fr.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_IT) "-it" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_IT_I) "-it" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_IT_U) "-it.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_JA_U) "-ja.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_PL) "-pl" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_PL_I) "-pl" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_PL_U) "-pl.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_RU) "-ru" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_RU_U) "-ru.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_TR) "-tr" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_TR_I) "-tr" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh install $(DEST_MAN_TR_U) "-tr.UTF-8" $(INSTALLMANARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DA) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DA_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DA_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DE) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DE_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DE_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_FR) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_FR_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_FR_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_IT) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_IT_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_IT_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_JA_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_PL) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_PL_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_PL_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_RU) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_RU_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_TR) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_TR_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_TR_U) $(INSTALLMLARGS)
/ if test -n "$(MAKEMO)" -a -f $(PODIR)/Makefile; then \
/    cd $(PODIR); $(MAKE) prefix=$(DESTDIR)$(prefix) LOCALEDIR=$(DEST_LANG) \
/    FILEMOD=$(FILEMOD) install; \
/ fi
/ if test -d $(LANGSOURCE); then \
/    cp $(LANGSOURCE)/README.txt $(LANGSOURCE)/*.vim $(DEST_LANG); \
/    chmod $(FILEMOD) $(DEST_LANG)/README.txt $(DEST_LANG)/*.vim; \
/ fi
/ if test -d $(KMAPSOURCE); then \
/    cp $(KMAPSOURCE)/README.txt $(KMAPSOURCE)/*.vim $(DEST_KMAP); \
/    chmod $(FILEMOD) $(DEST_KMAP)/README.txt $(DEST_KMAP)/*.vim; \
/ fi
# Installing translated README and LICENSE files
/ if test -d $(TRANSSOURCE) ; then \
/   if test -n "$(LANG)" ; then \
/     lngusr=$${LANG%%.*} ; \
/   elif test -n "$(LANGUAGE)" ; then \
/     lngusr=$${LANGUAGE%%:*} ; \
/   elif test -n "$(LC_MESSAGES)" ; then \
/     lngusr=$${LC_MESSAGES%%.*} ; \
/   fi; \
/   if test "$$lngusr" = "zh_TW" -o "$$lngusr" = "zh_CN" -o "$$lngusr" = "pt_BR" ; then \
/     lngusr=`echo $$lngusr | tr '[:upper:]' '[:lower:]'` ; \
/   elif test -n "$$lngusr" -a "$$lngusr" != "C" -a "$$lngusr" != "POSIX" ; then \
/     lngusr=$${lngusr%%_*} ; \
/   fi ; \
/   if test -f $(TRANSSOURCE)/README.$$lngusr.txt ; then \
/     cp $(TRANSSOURCE)/README.$$lngusr.txt $(DEST_RT) ; \
/     chmod $(HELPMOD) $(DEST_RT)/README.$$lngusr.txt ; \
/   fi ; \
/   if test -f $(TRANSSOURCE)/LICENSE.$$lngusr.txt ; then \
/     cp $(TRANSSOURCE)/LICENSE.$$lngusr.txt $(DEST_RT) ; \
/     chmod $(HELPMOD) $(DEST_RT)/LICENSE.$$lngusr.txt ; \
/   fi ; \
/ fi

$(HELPSOURCE)/eegl.1 $(MACROSOURCE) $(TOOLSSOURCE):
/ @echo Runtime files not found.
/ @echo You need to unpack the runtime archive before running "make install".
/ test -f error

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
/ mkdir -p $@
/ -chmod $(DIRMOD) $@

# Create links from various names to vim.  This is only done when the links
# (or executables with the same name) don't exist yet.
installlinks: $(DEST_BIN)/$(VIEWTARGET) \
/ 		$(DEST_BIN)/$(REEGLTARGET) \
/ 		$(DEST_BIN)/$(RVIEWTARGET) \
/ 		$(INSTALLVIMDIFF)

installvimdiff: $(DEST_BIN)/$(VIMDIFFTARGET)

$(DEST_BIN)/$(VIEWTARGET): $(DEST_BIN)
/ cd $(DEST_BIN); ln -s $(EEGLTARGET) $(VIEWTARGET)


$(DEST_BIN)/$(EEGLDIFFTARGET): $(DEST_BIN)
/ cd $(DEST_BIN); ln -s $(EEGLTARGET) $(VIMDIFFTARGET)

# Create links for the manual pages with various names to Eegl. This is only
# done when the links (or manpages with the same name) don't exist yet.

INSTALLMLARGS = eegl $(EEGLDIFFNAME) $(EEEGLNAME)

installmanlinks:
/ -$(SHELL) ./installml.sh install "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN) $(INSTALLMLARGS)

uninstall: uninstall_runtime
/ -rm -f $(DEST_BIN)/$(EEGLTARGET)
/ -rm -f $(DEST_BIN)/vimtutor
/ -rm -f $(DEST_BIN)/$(REEGLTARGET) $(DEST_BIN)/$(RVIEWTARGET)
/ -rm -f $(DEST_BIN)/$(RGEEGLTARGET) $(DEST_BIN)/$(RGVIEWTARGET)
/ -rm -f $(DEST_BIN)/$(VIMDIFFTARGET) $(DEST_BIN)/$(GVIMDIFFTARGET)
/ -rm -f $(DEST_BIN)/$(EEEGLTARGET) $(DEST_BIN)/$(EVIEWTARGET)

# Note: the "rmdir" will fail if any files were added after "make install"
uninstall_runtime:
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_DA) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_DA_I) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_DA_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_DE) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_DE_I) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_DE_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_FR) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_FR_I) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_FR_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_IT) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_IT_I) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_IT_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_JA_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_PL) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_PL_I) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_PL_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_RU) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_RU_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_TR) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_TR_I) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installman.sh uninstall $(DEST_MAN_TR_U) "" $(INSTALLMANARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DA) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DA_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DA_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DE) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DE_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_DE_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_FR) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_FR_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_FR_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_IT) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_IT_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_IT_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_JA_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_PL) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_PL_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_PL_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_RU) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_RU_U) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_TR) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_TR_I) $(INSTALLMLARGS)
/ -$(SHELL) ./installml.sh uninstall "$(GUI_MAN_TARGETS)" \
/ 	$(DEST_MAN_TR_U) $(INSTALLMLARGS)
/ -rm -f $(DEST_HELP)/*.txt $(DEST_HELP)/tags $(DEST_HELP)/*.pl
/ -rm -f $(DEST_HELP)/*.??x $(DEST_HELP)/tags-??
/ -rm -f $(SYS_MENU_FILE) $(SYS_SYNMENU_FILE) $(SYS_DELMENU_FILE)
/ -rm -f $(SYS_BUGR_FILE) $(VIM_DEFAULTS_FILE) $(EVIM_FILE) $(MSWIN_FILE)
/ -rm -f $(DEST_SCRIPT)/gvimrc_example.vim $(DEST_SCRIPT)/vimrc_example.vim
/ -rm -f $(SYS_FILETYPE_FILE) $(SYS_FTOFF_FILE) $(SYS_SCRIPTS_FILE)
/ -rm -f $(SYS_INDOFF_FILE) $(SYS_INDENT_FILE)
/ -rm -f $(SYS_FTPLUGOF_FILE) $(SYS_FTPLUGIN_FILE)
/ -rm -f $(SYS_OPTWIN_FILE)
/ -rm -f $(DEST_COL)/*.vim $(DEST_COL)/README.txt
/ -rm -rf $(DEST_COL)/tools
/ -rm -f $(DESKTOPPATH)/vim.desktop $(DESKTOPPATH)/gvim.desktop
/ -rm -f $(ICON16PATH)/gvim.png $(ICON32PATH)/gvim.png $(ICON48PATH)/gvim.png
/ -rm -rf $(DEST_COL)/lists
/ -rm -f $(DEST_SYN)/shared/*.vim $(DEST_SYN)/shared/README.txt
/ -rm -f $(DEST_SYN)/modula2/opt/*.vim
/ -rm -f $(DEST_SYN)/*.vim $(DEST_SYN)/README.txt
/ -rm -f $(DEST_IND)/*.vim $(DEST_IND)/README.txt
/ -rm -rf $(DEST_MACRO)
/ -rm -rf $(DEST_PACK)
/ -rm -rf $(DEST_TUTOR)/en
/ -rm -rf $(DEST_TUTOR)/it
/ -rm -rf $(DEST_TUTOR)/ru
/ -rm -rf $(DEST_TUTOR)/sr
/ -rm -rf $(DEST_TUTOR)
/ -rm -rf $(DEST_SPELL)
/ -rm -rf $(DEST_TOOLS)
/ -rm -rf $(DEST_LANG)
/ -rm -rf $(DEST_KMAP)
/ -rm -rf $(DEST_COMP)
/ -rm -f $(DEST_PRINT)/*.ps
/ -rmdir $(DEST_HELP) $(DEST_PRINT) $(DEST_COL) $(DEST_SYN)/shared
/ -rmdir $(DEST_SYN)/modula2/opt $(DEST_SYN)/modula2
/ -rmdir $(DEST_SYN) $(DEST_IND)
/ -rm -rf $(DEST_FTP)/*.vim $(DEST_FTP)/README.txt $(DEST_FTP)/logtalk.dict
/ -rm -f $(DEST_AUTO)/*.vim $(DEST_AUTO)/README.txt
/ -rm -f $(DEST_AUTO)/dist/*.vim $(DEST_AUTO)/xml/*.vim $(DEST_AUTO)/cargo/*.vim $(DEST_AUTO)/rust/*.vim
/ -rm -f $(DEST_IMPORT)/dist/*.vim
/ -rm -f $(DEST_PLUG)/*.vim $(DEST_PLUG)/README.txt
/ -rmdir $(DEST_FTP) $(DEST_AUTO)/dist $(DEST_AUTO)/xml $(DEST_AUTO)/cargo $(DEST_AUTO)/rust $(DEST_AUTO)
/ -rmdir $(DEST_IMPORT)/dist $(DEST_IMPORT)
/ -rm -f $(DEST_RT)/README.??.txt
/ -rm -f $(DEST_RT)/README.??_??.txt
/ -rm -f $(DEST_RT)/LICENSE.??.txt
/ -rm -f $(DEST_RT)/LICENSE.??_??.txt
/ -rm -f $(DEST_RT)/README.txt $(DEST_RT)/LICENSE
/ -rmdir $(DEST_PLUG) $(DEST_RT)
#	This will fail when other Eegl versions are installed, no worries.
/ -rmdir $(DEST_VIM)

#Clean up all the files that have been produced, except configure's.
clean: testclean
/ -rm -f *.o core $(EEGLTARGET).core $(EEGLTARGET) vim
/ -rm $(OBJDIR)/*
/ -rm $(OBJDIR)/os/*
/ -rm -f conftest* *~ auto/link.sed
/ -rm -f tests/opt_test.vim
/ -rm -f $(UNITTEST_TARGETS)
/ -rm -rf libs/libvterm/.libs libs/libvterm/src/.libs \
                libs/libvterm/t/.libs libs/libvterm/src/*.o \
                libs/libvterm/src/*.lo \
                libs/libvterm/t/*.o libvterm/t/*.lo libvterm/t/harness libvterm/libvterm.la
/ if test -d $(PODIR); then \
/ 	cd $(PODIR); $(MAKE) prefix=$(DESTDIR)$(prefix) clean; \
/ fi
/ cd libs/wayland; $(MAKE) clean

LINKEDFILES = ../*.[chm] ../*.cc ../*.in ../*.sh ../*.xs ../*.xbm ../gui_gtk_res.xml ../toolcheck \
   ../proto ../libvterm ../vimtutor ../install-sh ../Make_all.mak


distclean: clean scratch
/ -rm -f tags

dist: distclean
/ @echo
/ @echo Making the distribution has to be done in the top directory


# Run lint.  Clean up the *.ln files that are sometimes left behind.
lint:
/ $(LINT) $(LINT_OPTIONS) $(LINT_FLAGS) $(LINT_EXTRA) $(LINT_SRC)
/ -rm -f *.ln

# Check dosinst.c with lint.
lintinstall:
/ $(LINT) $(LINT_OPTIONS) -DWIN32 -DUNIX_LINT dosinst.c
/ -rm -f dosinst.ln

###########################################################################

.c.o:
/ $(COMPILE) $<


# All the object files are put in the "$(OBJDIR)" directory.  Since not all make
# commands understand putting object files in another directory, it must be
# specified for each file separately.

$(OBJDIR): $(OBJDIR)/.dirstamp

$(OBJDIR)/.dirstamp:
/ mkdir -p $(OBJDIR)
/ touch $(OBJDIR)/.dirstamp

# All object files depend on the objects directory, so that parallel make
# works.  Can't depend on the directory itself, its timestamp changes all the time.
$(ALL_OBJ): $(OBJDIR)/.dirstamp

$(OBJDIR)/%.o: src/%.c
/ $(COMPILE) -o $@ $<

$(OBJDIR)/diff.o: src/diff.c
/ $(COMPILE) -o $@ $<

$(OBJDIR)/option.o: src/option.c
/ $(COMPILE) -o $@ $<

$(OBJDIR)/regexp.o: src/regexp.c
/ $(COMPILE) -o $@ $<

$(OBJDIR)/ui.o: src/ui.c
/ $(COMPILE) -o $@ $<

$(OBJDIR)/window.o: src/window.c
/ $(COMPILE) $(WAYLAND_FLAGS) -o $@ $<

$(OBJDIR)/main.o: main.c
/ $(COMPILE) -o $@ $<

$(OBJDIR)/ext-data-control-v1.o: libs/wayland/ext-data-control-v1.c
/ $(COMPILE) $(WAYLAND_FLAGS) -o $@ $< 

$(OBJDIR)/xdg-shell.o: libs/wayland/xdg-shell.c
/ $(COMPILE) $(WAYLAND_FLAGS) -o $@ $<

$(OBJDIR)/primary-selection-unstable-v1.o: libs/wayland/primary-selection-unstable-v1.c
/ $(COMPILE) $(WAYLAND_FLAGS) -o $@ $<


###############################################################################
### (automatically generated by 'make depend')
### Dependencies:
$(OBJDIR)/book.o: src/book.c src/eegl.h src/generic.h
$(OBJDIR)/channel.o: src/channel.c src/eegl.h src/generic.h
$(OBJDIR)/data.o: src/data.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/diff.o: src/diff.c src/eegl.h src/generic.h src/commands.h
$(OBJDIR)/do.o: src/do.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/draw.o: src/draw.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/eval.o: src/eval.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/fileio.o: src/fileio.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/input.o: src/input.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/hilite.o: src/hilite.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/insert.o: src/insert.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/juggle.o: src/juggle.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/location.o: src/location.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/motor.o: src/motor.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/memory.o: src/memory.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/message.o: src/message.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/normal.o: src/normal.c src/eegl.h \
 src/generic.h src/commands.h src/actions.h src/indices/actions.h
$(OBJDIR)/option.o: src/option.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/portal.o: src/portal.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/regexp.o: src/regexp.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/script.o: src/script.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/search.o: src/search.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/persist.o: src/persist.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/strings.o: src/strings.c src/eegl.h \
 src/generic.h src/commands.h src/base.h
$(OBJDIR)/syntax.o: src/syntax.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/tag.o: src/tag.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/term.o: src/term.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/ui.o: src/ui.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/ui.o: src/ui.c src/eegl.h \
 src/generic.h src/commands.h
$(OBJDIR)/json_test.o: src/json_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/strings.c
$(OBJDIR)/kword_test.o: src/kword_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/strings.c
$(OBJDIR)/memfile_test.o: src/memfile_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/memory.c
$(OBJDIR)/message_test.o: src/message_test.c src/main.c src/eegl.h \
 src/generic.h src/commands.h src/message.c
$(OBJDIR)/channel.o: src/channel.c src/eegl.h src/generic.h 
$(OBJDIR)/window.o: src/window.c src/eegl.h src/generic.h src/commands.h
$(OBJDIR)/main.o: main.c src/eegl.h
 
$(OBJDIR)/ext-data-control-v1.o: libs/wayland/ext-data-control-v1.c
$(OBJDIR)/xdg-shell.o: libs/wayland/xdg-shell.c
$(OBJDIR)/primary-selection-unstable-v1.o: libs/wayland/primary-selection-unstable-v1.c

#}}}

package: ##Create a package for Arch Linux by building a specific version
/ package/package.sh $(APP) $(OBJDIR) $(VERSION)
