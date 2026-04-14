// Vim support file to detect file types
//
// Maintainer:		The Vim Project <https://github.com/vim/vim>
// Last Change:		2025 Aug 10
// Former Maintainer:	Bram Moolenaar <Bram@vim.org>

// Listen very carefully, I will say this only once
if exists("g:didLoadFiletypes")
  finish
endif
let g:didLoadFiletypes = 1


augroup filetypedetect

// Ignored extensions
if exists("*fnameescape")
autocmd BufNewFile,BufRead ?\+.orig,?\+.bak,?\+.old,?\+.new,?\+.dpkg-dist,?\+.dpkg-old,?\+.dpkg-new,?\+.dpkg-bak,?\+.rpmsave,?\+.rpmnew,?\+.pacsave,?\+.pacnew
	\ exe "doautocmd filetypedetect BufRead " .. fnameescape(expand("<afile>:r"))
autocmd BufNewFile,BufRead *~
	\ let s:name = expand("<afile>") |
	\ let s:short = substitute(s:name, '\~\+$', '', '') |
	\ if s:name != s:short && s:short != "" |
	\   exe "doautocmd filetypedetect BufRead " .. fnameescape(s:short) |
	\ endif |
	\ unlet! s:name s:short
autocmd BufNewFile,BufRead ?\+.in
	\ if expand("<afile>:t") != "configure.in" |
	\   exe "doautocmd filetypedetect BufRead " .. fnameescape(expand("<afile>:r")) |
	\ endif
elseif &verbose > 0
  echomsg "Warning: some filetypes will not be recognized because this version of Vim does not have fnameescape()"
endif

// Pattern used to match file names which should not be inspected.
// Currently finds compressed files.
if !exists("g:ft_ignore_pat")
  let g:ft_ignore_pat = '\.\(Z\|gz\|zstd\|bz2\|zip\|tgz\)$'
endif

// Function used for patterns that end in a star: don't set the filetype if the
// file name matches ft_ignore_pat.
// When using this, the entry should probably be further down below with the
// other StarSetf() calls.
func s:StarSetf(ft)
  if expand("<amatch>") !~ g:ft_ignore_pat
    exe 'setf ' .. a:ft
  endif
endfunc

// Vim help file, set ft explicitly, because 'modeline' might be off
autocmd BufNewFile,BufRead */doc/*.txt
	\   setf help

// Abaqus or Trasys
autocmd BufNewFile,BufRead *.inp			call dist#ft#Check_inp()

// 8th (Firth-derivative)
autocmd BufNewFile,BufRead *.8th			setf 8th

// A-A-P recipe
autocmd BufNewFile,BufRead *.aap			setf aap

// A2ps printing utility
autocmd BufNewFile,BufRead */etc/a2ps.cfg,*/etc/a2ps/*.cfg,a2psrc,.a2psrc setf a2ps

// ABAB/4
autocmd BufNewFile,BufRead *.abap			setf abap

// ABC music notation
autocmd BufNewFile,BufRead *.abc			setf abc

// ABEL
autocmd BufNewFile,BufRead *.abl			setf abel

// ABNF
autocmd BufNewFile,BufRead *.abnf			setf abnf

// AceDB
autocmd BufNewFile,BufRead *.wrm			setf acedb

// Ada (83, 9X, 95)
autocmd BufNewFile,BufRead *.adb,*.ads,*.ada		setf ada
autocmd BufNewFile,BufRead *.gpr			setf ada

// AHDL
autocmd BufNewFile,BufRead *.tdf			setf ahdl

// AIDL
autocmd BufNewFile,BufRead *.aidl			setf aidl

// AMPL
autocmd BufNewFile,BufRead *.run			setf ampl

// Ant
autocmd BufNewFile,BufRead build.xml			setf ant

// ANTLR / PCCTS
autocmd BufNewFile,BufRead *.g			setf pccts

// ANTLR 4
autocmd BufNewFile,BufRead *.g4			setf antlr4

// Arduino
autocmd BufNewFile,BufRead *.ino,*.pde		setf arduino

// Ash of busybox
autocmd BufNewFile,BufRead .ash_history		setf sh

// Asymptote
autocmd BufNewFile,BufRead *.asy		setf asy

// Apache config file
autocmd BufNewFile,BufRead .htaccess,*/etc/httpd/*.conf		setf apache
autocmd BufNewFile,BufRead */etc/apache2/sites-*/*.com		setf apache

// XA65 MOS6510 cross assembler
autocmd BufNewFile,BufRead *.a65			setf a65

// Applescript
autocmd BufNewFile,BufRead *.scpt			setf applescript

// Automake (must be before the *.am pattern)
autocmd BufNewFile,BufRead [mM]akefile.am,GNUmakefile.am	setf automake

// Applix ELF
autocmd BufNewFile,BufRead *.am			setf elf

// ALSA configuration
autocmd BufNewFile,BufRead .asoundrc,*/usr/share/alsa/alsa.conf,*/etc/asound.conf setf alsaconf

// Arc Macro Language
autocmd BufNewFile,BufRead *.aml			setf aml

// APT config file
autocmd BufNewFile,BufRead apt.conf		       setf aptconf
autocmd BufNewFile,BufRead */.aptitude/config       setf aptconf
// more generic pattern far down

// Arch Inventory file
autocmd BufNewFile,BufRead .arch-inventory,=tagging-method	setf arch

// ART*Enterprise (formerly ART-IM)
autocmd BufNewFile,BufRead *.art			setf art

// AsciiDoc
autocmd BufNewFile,BufRead *.asciidoc,*.adoc		setf asciidoc

// ASN.1
autocmd BufNewFile,BufRead *.asn,*.asn1		setf asn

// Active Server Pages (with Visual Basic Script)
autocmd BufNewFile,BufRead *.asa
	\ if exists("g:filetype_asa") |
	\   exe "setf " .. g:filetype_asa |
	\ else |
	\   setf aspvbs |
	\ endif

// Active Server Pages (with Perl or Visual Basic Script)
autocmd BufNewFile,BufRead *.asp
	\ if exists("g:filetype_asp") |
	\   exe "setf " .. g:filetype_asp |
	\ elseif getline(1) .. getline(2) .. getline(3) =~? "perlscript" |
	\   setf aspperl |
	\ else |
	\   setf aspvbs |
	\ endif

// Grub (must be before pattern *.lst)
autocmd BufNewFile,BufRead */boot/grub/menu.lst,*/boot/grub/grub.conf,*/etc/grub.conf setf grub

// Maxima, see:
// https://maxima.sourceforge.io/docs/manual/maxima_71.html#file_005ftype_005fmaxima
// Must be before the pattern *.mac.
// *.dem omitted - also used by gnuplot demos
// *.mc omitted - used by dist#ft#McSetf()
autocmd BufNewFile,BufRead *.demo,*.dm{1,2,3,t},*.wxm,maxima-init.mac setf maxima

// Assembly (all kinds)
// *.lst is not pure assembly, it has two extra columns (address, byte codes)
// *.[sS], *.[aA] usually Assembly - GNU
autocmd BufNewFile,BufRead *.asm,*.[sS],*.[aA],*.mac,*.lst	call dist#ft#FTasm()

// Assembly - Netwide
autocmd BufNewFile,BufRead *.nasm			setf nasm

// Assembly - Microsoft
autocmd BufNewFile,BufRead *.masm			setf masm

// Assembly - Macro (VAX)
autocmd BufNewFile,BufRead *.mar			setf vmasm

// Astro
autocmd BufNewFile,BufRead *.astro			setf astro

// Atlas
autocmd BufNewFile,BufRead *.atl,*.as		setf atlas

// Atom is based on XML
autocmd BufNewFile,BufRead *.atom			setf xml

// Authzed
autocmd BufNewFile,BufRead *.zed			setf authzed

// Autoit v3
autocmd BufNewFile,BufRead *.au3			setf autoit

// Autohotkey
autocmd BufNewFile,BufRead *.ahk			setf autohotkey

// Autotest .at files are actually m4
autocmd BufNewFile,BufRead *.at			setf m4

// Avenue
autocmd BufNewFile,BufRead *.ave			setf ave

// Awk
autocmd BufNewFile,BufRead *.awk,*.gawk		setf awk

// B
autocmd BufNewFile,BufRead *.mch,*.ref,*.imp		setf b

// BASIC or Visual Basic
autocmd BufNewFile,BufRead *.bas			call dist#ft#FTbas()
autocmd BufNewFile,BufRead *.bi,*.bm			call dist#ft#FTbas()

// Bass
autocmd BufNewFile,BufRead *.bass			setf bass

// IBasic file (similar to QBasic)
autocmd BufNewFile,BufRead *.iba,*.ibi		setf ibasic

// FreeBasic file (similar to QBasic)
autocmd BufNewFile,BufRead *.fb			setf freebasic

// Batch file for MSDOS. See dist#ft#FTsys for *.sys
autocmd BufNewFile,BufRead *.bat			setf dosbatch

// *.cmd is close to a Batch file, but on OS/2 Rexx files and TI linker command files also use *.cmd.
// lnk: `/* comment */`, `// comment`, and `--linker-option=value`
// rexx: `/* comment */`, `-- comment`
autocmd BufNewFile,BufRead *.cmd
	\  if join(getline(1, 20), "\n") =~ 'MEMORY\|SECTIONS\|\%(^\|\n\)--\S\|\%(^\|\n\)//'
	\|   setf lnk
	\| elseif getline(1) =~ '^/\*'
	\|   setf rexx
	\| else
	\|   setf dosbatch
	\| endif
// ABB RAPID or Batch file for MSDOS.
autocmd BufNewFile,BufRead *.sys			call dist#ft#FTsys()
autocmd BufNewFile,BufRead *.Sys,*.SYS		call dist#ft#FTsys()
autocmd BufNewFile,BufRead *.sysx			setf rapid
autocmd BufNewFile,BufRead *.sysX,*.Sysx,*.SysX,*.SYSX,*.SYSx	setf rapid

// Batch file for 4DOS
autocmd BufNewFile,BufRead *.btm			call dist#ft#FTbtm()

// BC calculator
autocmd BufNewFile,BufRead *.bc			setf bc

// BDF font
autocmd BufNewFile,BufRead *.bdf			setf bdf

// Beancount
autocmd BufNewFile,BufRead *.beancount		setf beancount

// BibTeX bibliography database file
autocmd BufNewFile,BufRead *.bib			setf bib

// BibTeX Bibliography Style
autocmd BufNewFile,BufRead *.bst			setf bst

// Bicep
autocmd BufNewFile,BufRead *.bicep,*.bicepparam			setf bicep

// BIND configuration
// sudoedit uses namedXXXX.conf
autocmd BufNewFile,BufRead named*.conf,rndc*.conf,rndc*.key	setf named

// BIND zone
autocmd BufNewFile,BufRead named.root		setf bindzone
autocmd BufNewFile,BufRead *.zone			setf bindzone
autocmd BufNewFile,BufRead *.db			call dist#ft#BindzoneCheck('')

// Blade
autocmd BufNewFile,BufRead *.blade.php		setf blade

// Blank
autocmd BufNewFile,BufRead *.bl			setf blank

// Bitbake
autocmd BufNewFile,BufRead *.bb,*.bbappend,*.bbclass,*/build/conf/*.conf,*/meta{-*,}/conf/*.conf,*/project-spec/configs/*.conf	setf bitbake

// Blkid cache file
autocmd BufNewFile,BufRead */etc/blkid.tab,*/etc/blkid.tab.old   setf xml

// Brighterscript
autocmd BufNewFile,BufRead *.bs			setf brighterscript

// Brightscript
autocmd BufNewFile,BufRead *.brs			setf brightscript

// BSDL
autocmd BufNewFile,BufRead *.bsd,*.bsdl			setf bsdl

// Bazel (https://bazel.build) and Buck2 (https://buck2.build/)
autocmd BufRead,BufNewFile *.bzl,*.bazel,WORKSPACE,WORKSPACE.bzlmod	setf bzl
// There is another check for BUILD and BUCK further below.
autocmd BufRead,BufNewFile *.BUILD,BUILD,BUCK		setf bzl

// Busted (Lua unit testing framework - configuration files)
autocmd BufNewFile,BufRead .busted			setf lua

// Bun history
autocmd BufNewFile,BufRead .bun_repl_history		setf javascript

// Bundle config
autocmd BufNewFile,BufRead */.bundle/config			setf yaml

// C or lpc
autocmd BufNewFile,BufRead *.c,*.h			call dist#ft#FTlpc()
autocmd BufNewFile,BufRead *.lpc,*.ulpc		setf lpc

// C3
autocmd BufNewFile,BufRead *.c3,*.c3i,*.c3t		setf c3

// Cairo
autocmd BufNewFile,BufRead *.cairo			setf cairo

// Calendar
autocmd BufNewFile,BufRead calendar			setf calendar

// Cap'n Proto
autocmd BufNewFile,BufRead *.capnp			setf capnp

// Cgdb config file
autocmd BufNewFile,BufRead cgdbrc			setf cgdbrc

// m17n database files. */m17n/* matches installed files, */.m17n.d/* matches
// per-user config files, */m17n-db/* matches the git repo. (must be before
// *.cs)
autocmd BufNewFile,BufRead */{m17n,.m17n.d,m17n-db}/*.{ali,cs,dir,flt,fst,lnm,mic,mim,tbl} setf m17ndb

// C#
autocmd BufNewFile,BufRead *.cs,*.csx,*.cake		setf cs

// CSDL
autocmd BufNewFile,BufRead *.csdl			setf csdl

// Ctags
autocmd BufNewFile,BufRead *.ctags			setf conf

// Cabal
autocmd BufNewFile,BufRead *.cabal			setf cabal

// Cdrdao TOC or LaTeX \tableofcontents files
autocmd BufNewFile,BufRead *.toc
	\ if getline(1) =~# '\\contentsline' |setf tex|else|setf cdrtoc|endif

// Cdrdao config
autocmd BufNewFile,BufRead */etc/cdrdao.conf,*/etc/defaults/cdrdao,*/etc/default/cdrdao,.cdrdao	setf cdrdaoconf

// Cedar
autocmd BufNewFile,BufRead *.cedar			setf cedar

// Cfengine
autocmd BufNewFile,BufRead cfengine.conf		setf cfengine

// ChaiScript
autocmd BufRead,BufNewFile *.chai			setf chaiscript

// Chatito
autocmd BufNewFile,BufRead *.chatito			setf chatito

// Chktex
autocmd BufRead,BufNewFile .chktexrc			setf conf

// Chuck
autocmd BufNewFile,BufRead *.ck			setf chuck

// Comshare Dimension Definition Language
autocmd BufNewFile,BufRead *.cdl			setf cdl

// Conary Recipe
autocmd BufNewFile,BufRead *.recipe			setf conaryrecipe

// Containers config files
autocmd BufNewFile,BufRead */containers/containers.conf{,.d/*.conf}		setf toml
autocmd BufNewFile,BufRead */containers/containers.conf.modules/*.conf	setf toml
autocmd BufNewFile,BufRead */containers/registries.conf{,.d/*.conf}		setf toml
autocmd BufNewFile,BufRead */containers/storage.conf				setf toml

// Corn config file
autocmd BufNewFile,BufRead *.corn			setf corn

// ChainPack Object Notation (CPON)
autocmd BufNewFile,BufRead *.cpon			setf cpon

// Controllable Regex Mutilator
autocmd BufNewFile,BufRead *.crm			setf crm

// Cyn++
autocmd BufNewFile,BufRead *.cyn			setf cynpp

// Cynlib
// .cc and .cpp files can be C++ or Cynlib.
autocmd BufNewFile,BufRead *.cc
	\ if exists("cynlib_syntax_for_cc")|setf cynlib|else|setf cpp|endif
autocmd BufNewFile,BufRead *.cpp
	\ if exists("cynlib_syntax_for_cpp")|setf cynlib|else|setf cpp|endif

// Cypher query language
autocmd BufNewFile,BufRead *.cypher			setf cypher

// C++
autocmd BufNewFile,BufRead *.cxx,*.c++,*.hh,*.hxx,*.hpp,*.ipp,*.moc,*.tcc,*.inl setf cpp

// MS files (ixx: C++ module interface file, Microsoft Project file)
autocmd BufNewFile,BufRead *.ixx,*.mpp setf cpp

// C++ 20 modules (clang)
// https://clang.llvm.org/docs/StandardCPlusPlusModules.html#file-name-requirement
autocmd BufNewFile,BufRead *.cppm,*.ccm,*.cxxm,*.c++m setf cpp


// Ch (CHscript)
autocmd BufNewFile,BufRead *.chf			setf ch

// TLH files are C++ headers generated by Visual C++'s #import from typelibs
autocmd BufNewFile,BufRead *.tlh			setf cpp

// Cascading Style Sheets
autocmd BufNewFile,BufRead *.css			setf css

// Century Term Command Scripts (*.cmd too)
autocmd BufNewFile,BufRead *.con			setf cterm

// Changelog
autocmd BufNewFile,BufRead changelog.Debian,changelog.dch,NEWS.Debian,NEWS.dch,*/debian/changelog
					\	setf debchangelog

autocmd BufNewFile,BufRead [cC]hange[lL]og
	\  if getline(1) =~ '; urgency='
	\|   setf debchangelog
	\| else
	\|   setf changelog
	\| endif

autocmd BufNewFile,BufRead NEWS
	\  if getline(1) =~ '; urgency='
	\|   setf debchangelog
	\| endif

// CHILL
autocmd BufNewFile,BufRead *..ch			setf chill

// Changes for WEB and CWEB or CHILL
autocmd BufNewFile,BufRead *.ch			call dist#ft#FTchange()

// ChordPro
autocmd BufNewFile,BufRead *.chopro,*.crd,*.cho,*.crdpro,*.chordpro	setf chordpro

// Clangd
autocmd BufNewFile,BufRead .clangd			setf yaml

// Clang-format
autocmd BufNewFile,BufRead .clang-format		setf yaml

// Clang-tidy
autocmd BufNewFile,BufRead .clang-tidy		setf yaml

// Conda configuration file
autocmd BufNewFile,BufRead .condarc,condarc		setf yaml

// Matplotlib
autocmd BufNewFile,BufRead *.mplstyle,matplotlibrc	setf yaml

// Clean
autocmd BufNewFile,BufRead *.dcl,*.icl		setf clean

// Clever
autocmd BufNewFile,BufRead *.eni			setf cl

// Clever or dtd
autocmd BufNewFile,BufRead *.ent			call dist#ft#FTent()

// Cling
autocmd BufNewFile,BufRead .cling_history		setf cpp

// Clipper, FoxPro, ABB RAPID or eviews
autocmd BufNewFile,BufRead *.prg			call dist#ft#FTprg()

// Clojure
autocmd BufNewFile,BufRead *.clj,*.cljs,*.cljx,*.cljc		setf clojure

// Cmake
autocmd BufNewFile,BufRead CMakeLists.txt,*.cmake,*.cmake.in		setf cmake

// CmakeCache
autocmd BufRead,BufNewFile CMakeCache.txt			setf cmakecache

// Cmusrc
autocmd BufNewFile,BufRead */.cmus/{autosave,rc,command-history,*.theme} setf cmusrc
autocmd BufNewFile,BufRead */cmus/{rc,*.theme}			setf cmusrc

// Cobol
autocmd BufNewFile,BufRead *.cbl,*.cob	setf cobol
//   cobol or zope form controller python script? (heuristic)
autocmd BufNewFile,BufRead *.cpy
	\ if getline(1) =~ '^##' |
	\   setf python |
	\ else |
	\   setf cobol |
	\ endif

// Coco/R
autocmd BufNewFile,BufRead *.atg			setf coco

// Cold Fusion
autocmd BufNewFile,BufRead *.cfm,*.cfi,*.cfc		setf cf

// Configure scripts
autocmd BufNewFile,BufRead configure.in,configure.ac setf config

// Cooklang
autocmd BufNewFile,BufRead *.cook			setf cook

// Clinical Quality Language (CQL)
// .cql is also mentioned as the 'XDCC Catcher queue list' file extension.
// If support for XDCC Catcher is needed in the future, the contents of the file
// needs to be inspected.
autocmd BufNewFile,BufRead *.cql			setf cqlang

// Crystal
autocmd BufNewFile,BufRead *.cr			setf crystal

// CSV Files
autocmd BufNewFile,BufRead *.csv			setf csv

// CUDA Compute Unified Device Architecture
autocmd BufNewFile,BufRead *.cu,*.cuh		setf cuda

// Cue
autocmd BufNewFile,BufRead *.cue			setf cue

// DAX
autocmd BufNewFile,BufRead *.dax			setf dax

// Debian devscripts
autocmd BufNewFile,BufRead devscripts.conf,.devscripts	setf sh

// Dockerfile; Podman uses the same syntax with name Containerfile
// Also see Dockerfile.* below.
autocmd BufNewFile,BufRead Containerfile,Dockerfile,dockerfile,*.[dD]ockerfile	setf dockerfile

// WildPackets EtherPeek Decoder
autocmd BufNewFile,BufRead *.dcd			setf dcd

// Enlightenment configuration files
autocmd BufNewFile,BufRead *enlightenment/*.cfg	setf c

// Eterm
autocmd BufNewFile,BufRead *Eterm/*.cfg		setf eterm

// Elixir or Euphoria
autocmd BufNewFile,BufRead *.ex call dist#ft#ExCheck()

// Elixir
autocmd BufRead,BufNewFile mix.lock,*.exs setf elixir
autocmd BufRead,BufNewFile *.eex,*.leex setf eelixir

// Elvish
autocmd BufRead,BufNewFile *.elv setf elvish

// Euphoria 3 or 4
autocmd BufNewFile,BufRead *.eu,*.ew,*.exu,*.exw  call dist#ft#EuphoriaCheck()

// Execline (s6) scripts
autocmd BufNewFile,BufRead *s6*/\(up\|down\|run\|finish\)    setf execline

// Fontconfig config files
autocmd BufNewFile,BufRead fonts.conf			setf xml

// Faust
autocmd BufNewFile,BufRead *.lib				setf faust
autocmd BufNewFile,BufRead *.dsp				call dist#ft#FTdsp()

// Libreoffice config files
autocmd BufNewFile,BufRead *.xcu,*.xlb,*.xlc,*.xba		setf xml
autocmd BufNewFile,BufRead psprint.conf,sofficerc		setf dosini

// Libtool files
autocmd BufNewFile,BufRead *.lo,*.la,*.lai		setf sh

// Lynx config files
autocmd BufNewFile,BufRead lynx.cfg			setf lynx

// LyRiCs
autocmd BufNewFile,BufRead *.lrc			setf lyrics

// MLIR
autocmd BufNewFile,BufRead *.mlir			setf mlir

// Modula-3 configuration language (must be before *.cfg and *makefile)
autocmd BufNewFile,BufRead *.quake,cm3.cfg		setf m3quake
autocmd BufNewFile,BufRead m3makefile,m3overrides	setf m3build

// XDG mimeapps.list
autocmd BufNewFile,BufRead mimeapps.list	setf dosini

// Many tools written in Python use dosini as their config
// like setuptools, pudb, coverage, pypi, gitlint, oelint-adv, pylint, bpython, mypy
// (must be before *.cfg)
autocmd BufNewFile,BufRead pip.conf,setup.cfg,pudb.cfg,.coveragerc,.pypirc,.gitlint,.oelint.cfg	setf dosini
autocmd BufNewFile,BufRead {.,}pylintrc,*/bpython/config,*/mypy/config			setf dosini

// Many tools written in Python use toml as their config, like black
autocmd BufNewFile,BufRead .black	setf toml
autocmd BufNewFile,BufRead black
	\  if getline(1) =~ 'tool.back'
	\|   setf toml
	\| endif

// LXQt's programs use dosini as their config
autocmd BufNewFile,BufRead */{lxqt,screengrab}/*.conf	setf dosini

// Quake
autocmd BufNewFile,BufRead *baseq[2-3]/*.cfg,*id1/*.cfg	setf quake
autocmd BufNewFile,BufRead *quake[1-3]/*.cfg			setf quake

// Quake C
autocmd BufNewFile,BufRead *.qc			setf c

// LaTeX packages use LaTeX as their configuration, such as:
// ~/.texlive/texmf-config/tex/latex/hyperref/hyperref.cfg
// ~/.texlive/texmf-config/tex/latex/docstrip/docstrip.cfg
autocmd BufNewFile,BufRead */tex/latex/**.cfg		setf tex

// Wakatime config
autocmd BufNewFile,BufRead .wakatime.cfg		setf dosini

// Configure files
autocmd BufNewFile,BufRead *.cfg			call dist#ft#FTcfg()
if has("fname_case")
  autocmd BufNewFile,BufRead *.Cfg,*.CFG			call dist#ft#FTcfg()
endif

// Cucumber
autocmd BufNewFile,BufRead *.feature			setf cucumber

// Communicating Sequential Processes
autocmd BufNewFile,BufRead *.csp,*.fdr		setf csp

// CUPL logic description and simulation
autocmd BufNewFile,BufRead *.pld			setf cupl
autocmd BufNewFile,BufRead *.si			setf cuplsim

// Dafny
autocmd BufNewFile,BufRead *.dfy			setf dafny

// Dart
autocmd BufRead,BufNewfile *.dart,*.drt		setf dart

// Debian autopkgtest
autocmd BufNewFile,BufRead */debian/tests/control	setf autopkgtest

// Debian Control
autocmd BufNewFile,BufRead */{debian,DEBIAN}/control		setf debcontrol
autocmd BufNewFile,BufRead control
	\  if getline(1) =~ '^Source:\|^Package:'
	\|   setf debcontrol
	\| elseif getline(1) =~ '^Tests:\|^Test-Command:'
	\|   setf autopkgtest
	\| endif

// Debian Copyright
autocmd BufNewFile,BufRead */debian/copyright	setf debcopyright
autocmd BufNewFile,BufRead copyright
	\  if getline(1) =~ '^Format:'
	\|   setf debcopyright
	\| endif

// Debian Sources.list
autocmd BufNewFile,BufRead */etc/apt/sources.list		setf debsources
autocmd BufNewFile,BufRead */etc/apt/sources.list.d/*.list	setf debsources
autocmd BufNewFile,BufRead */etc/apt/sources.list.d/*.sources	setf deb822sources

// Deno history
autocmd BufNewFile,BufRead deno_history.txt		setf javascript

// Deny hosts
autocmd BufNewFile,BufRead denyhosts.conf		setf denyhosts

// Dhall
autocmd BufNewFile,BufRead *.dhall			setf dhall

// dnsmasq(8) configuration files
autocmd BufNewFile,BufRead */etc/dnsmasq.conf	setf dnsmasq

// ROCKLinux package description
autocmd BufNewFile,BufRead *.desc			setf desc

// the D language or dtrace
autocmd BufNewFile,BufRead */dtrace/*.d		setf dtrace
autocmd BufNewFile,BufRead *.d			call dist#ft#DtraceCheck()

// Desktop files
autocmd BufNewFile,BufRead *.desktop,*.directory	setf desktop

// Dict config
autocmd BufNewFile,BufRead dict.conf,.dictrc		setf dictconf

// Dictd config
autocmd BufNewFile,BufRead dictd*.conf		setf dictdconf

// DEP3 formatted patch files
autocmd BufNewFile,BufRead */debian/patches/*	call dist#ft#Dep3patch()

// Diff files
autocmd BufNewFile,BufRead *.diff,*.rej		setf diff
autocmd BufNewFile,BufRead *.patch
	\ if getline(1) =~# '^From [0-9a-f]\{40,\} Mon Sep 17 00:00:00 2001$' |
	\   setf gitsendemail |
	\ else |
	\   setf diff |
	\ endif

// Dircolors
autocmd BufNewFile,BufRead .dir_colors,.dircolors,*/etc/DIR_COLORS	setf dircolors

// Diva (with Skill) or InstallShield
autocmd BufNewFile,BufRead *.rul
	\ if getline(1).getline(2).getline(3).getline(4).getline(5).getline(6) =~? 'InstallShield' |
	\   setf ishd |
	\ else |
	\   setf diva |
	\ endif

// DCL (Digital Command Language - vms) or DNS zone file
autocmd BufNewFile,BufRead *.com			call dist#ft#BindzoneCheck('dcl')

// DOT
autocmd BufNewFile,BufRead *.dot,*.gv		setf dot

// Dune
autocmd BufNewFile,BufRead jbuild,dune,dune-project,dune-workspace,dune-file setf dune

// Dylan - lid files
autocmd BufNewFile,BufRead *.lid			setf dylanlid

// Dylan - intr files (melange)
autocmd BufNewFile,BufRead *.intr			setf dylanintr

// Dylan
autocmd BufNewFile,BufRead *.dylan			setf dylan

// Microsoft Module Definition or Modula-2
autocmd BufNewFile,BufRead *.def			call dist#ft#FTdef()

// Dracula
autocmd BufNewFile,BufRead *.drac,*.drc,*.lvs,*.lpe	setf dracula

// Datascript
autocmd BufNewFile,BufRead *.ds			setf datascript

// dsl: DSSSL or Structurizr
autocmd BufNewFile,BufRead *.dsl
	\ if getline(1) =~ '^\s*<\!' |
	\   setf dsl |
	\ else |
	\   setf structurizr |
	\ endif

// DTD (Document Type Definition for XML)
autocmd BufNewFile,BufRead *.dtd			setf dtd

// Devicetree (.its for U-Boot Flattened Image Trees, .keymap for ZMK keymap, and
// .overlay for Zephyr overlay)
autocmd BufNewFile,BufRead *.dts,*.dtsi,*.dtso	setf dts
autocmd BufNewFile,BufRead *.its			setf dts
autocmd BufNewFile,BufRead *.keymap			setf dts
autocmd BufNewFile,BufRead *.overlay			setf dts

// Earthfile
autocmd BufNewFile,BufRead Earthfile			setf earthfile

// EDIF (*.edf,*.edif,*.edn,*.edo) or edn
autocmd BufNewFile,BufRead *.ed\(f\|if\|o\)		setf edif
autocmd BufNewFile,BufRead *.edn
	\ if getline(1) =~ '^\s*(\s*edif\>' |
	\   setf edif |
	\ else |
	\   setf clojure |
	\ endif

// EditorConfig
autocmd BufNewFile,BufRead .editorconfig		setf editorconfig

// Embedix Component Description
autocmd BufNewFile,BufRead *.ecd			setf ecd

// Eiffel or Specman or Euphoria
autocmd BufNewFile,BufRead *.e,*.E			call dist#ft#FTe()

// Elinks configuration
autocmd BufNewFile,BufRead elinks.conf		setf elinks

// ERicsson LANGuage; Yaws is erlang too
autocmd BufNewFile,BufRead *.erl,*.hrl,*.yaws	setf erlang

// Elm
autocmd BufNewFile,BufRead *.elm			setf elm

// Elm Filter Rules file
autocmd BufNewFile,BufRead filter-rules		setf elmfilt

// Elsa - https://github.com/ucsd-progsys/elsa
autocmd BufNewFile,BufRead *.lc			setf elsa

// EdgeDB Schema Definition Language
autocmd BufNewFile,BufRead *.esdl			setf esdl

// ESMTP rc file
autocmd BufNewFile,BufRead *esmtprc			setf esmtprc

// ESQL-C
autocmd BufNewFile,BufRead *.ec,*.EC			setf esqlc

// Esterel
autocmd BufNewFile,BufRead *.strl			setf esterel

// Essbase script
autocmd BufNewFile,BufRead *.csc			setf csc

// Exim
autocmd BufNewFile,BufRead exim.conf			setf exim

// Expect
autocmd BufNewFile,BufRead *.exp			setf expect

// Exports
autocmd BufNewFile,BufRead exports			setf exports

// Falcon
autocmd BufNewFile,BufRead *.fal			setf falcon

// Fantom
autocmd BufNewFile,BufRead *.fan,*.fwt		setf fan

// Factor
autocmd BufNewFile,BufRead *.factor			setf factor

// Fennel
autocmd BufRead,BufNewFile *.fnl,{,.}fennelrc	setf fennel

// Fetchmail RC file
autocmd BufNewFile,BufRead .fetchmailrc		setf fetchmail

// FGA
autocmd BufNewFile,BufRead *.fga			setf fga

// FIRRTL - Flexible Internal Representation for RTL
autocmd BufNewFile,BufRead *.fir			setf firrtl

// Fish shell
autocmd BufNewFile,BufRead *.fish			setf fish

// Flatpak config
autocmd BufNewFile,BufRead */flatpak/repo/config	setf dosini

// Flix
autocmd BufNewFile,BufRead *.flix			setf flix

// Focus Executable
autocmd BufNewFile,BufRead *.fex,*.focexec		setf focexec

// Focus Master file (but not for auto.master)
autocmd BufNewFile,BufRead auto.master		setf conf
autocmd BufNewFile,BufRead *.mas,*.master		setf master

// Forth
autocmd BufNewFile,BufRead *.ft,*.fth,*.4th		setf forth

// Reva Forth
autocmd BufNewFile,BufRead *.frt			setf reva

// Fortran
autocmd BufNewFile,BufRead *.for,*.fortran,*.fpp,*.ftn,*.f77,*.f90,*.f95,*.f03,*.f08	setf fortran

// Fortran or Forth
autocmd BufNewFile,BufRead *.f			call dist#ft#FTf()

// Framescript
autocmd BufNewFile,BufRead *.fsl			setf framescript

// FStab
autocmd BufNewFile,BufRead fstab,mtab		setf fstab

// Func
autocmd BufNewFile,BufRead *.fc			setf func

// Fusion
autocmd BufRead,BufNewFile *.fusion			setf fusion

// F# or Forth
autocmd BufNewFile,BufRead *.fs			call dist#ft#FTfs()

// FHIR Shorthand (FSH)
autocmd BufNewFile,BufRead *.fsh			setf fsh

// F#
autocmd BufNewFile,BufRead *.fsi,*.fsx		setf fsharp

// GDB command files
autocmd BufNewFile,BufRead .gdbinit,gdbinit,.cuda-gdbinit,cuda-gdbinit,.gdbearlyinit,gdbearlyinit,*.gdb		setf gdb

// GDMO
autocmd BufNewFile,BufRead *.mo,*.gdmo		setf gdmo

// GDscript
autocmd BufNewFile,BufRead *.gd			setf gdscript

// Godot resource
autocmd BufRead,BufNewFile *.tscn,*.tres		setf gdresource

// Godot shader
autocmd BufRead,BufNewFile *.gdshader,*.shader	setf gdshader

// Gedcom
autocmd BufNewFile,BufRead *.ged,lltxxxxx.txt	setf gedcom

// Gemtext
autocmd BufNewFile,BufRead *.gmi,*.gemini		setf gemtext

// Gift (Moodle)
autocmd BufRead,BufNewFile *.gift		setf gift

// Git
autocmd BufNewFile,BufRead COMMIT_EDITMSG,MERGE_MSG,TAG_EDITMSG	setf gitcommit
autocmd BufNewFile,BufRead NOTES_EDITMSG,EDIT_DESCRIPTION		setf gitcommit
autocmd BufNewFile,BufRead *.git/config,.gitconfig,*/etc/gitconfig	setf gitconfig
autocmd BufNewFile,BufRead */.config/git/config			setf gitconfig
autocmd BufNewFile,BufRead *.git/config.worktree			setf gitconfig
autocmd BufNewFile,BufRead *.git/worktrees/*/config.worktree		setf gitconfig
autocmd BufNewFile,BufRead .gitmodules,*.git/modules/*/config	setf gitconfig
if exists('$XDG_CONFIG_HOME')
  autocmd BufNewFile,BufRead $XDG_CONFIG_HOME/git/config		setf gitconfig
  autocmd BufNewFile,BufRead $XDG_CONFIG_HOME/git/attributes		setf gitattributes
  autocmd BufNewFile,BufRead $XDG_CONFIG_HOME/git/ignore		setf gitignore
endif
autocmd BufNewFile,BufRead .gitattributes,*.git/info/attributes	setf gitattributes
autocmd BufNewFile,BufRead */.config/git/attributes			setf gitattributes
autocmd BufNewFile,BufRead */etc/gitattributes			setf gitattributes
autocmd BufNewFile,BufRead .gitignore,*.git/info/exclude		setf gitignore
autocmd BufNewFile,BufRead */.config/git/ignore,*.prettierignore	setf gitignore
autocmd BufNewFile,BufRead */.config/fd/ignore,.fdignore,.ignore	setf gitignore
autocmd BufNewFile,BufRead .rgignore,.dockerignore,.containerignore	setf gitignore
autocmd BufNewFile,BufRead .npmignore,.vscodeignore			setf gitignore
autocmd BufNewFile,BufRead git-rebase-todo				setf gitrebase
autocmd BufRead,BufNewFile .gitsendemail.msg.??????			setf gitsendemail
autocmd BufNewFile,BufRead *.git/*
      \ if getline(1) =~# '^\x\{40,\}\>\|^ref: ' |
      \   setf git |
      \ endif

// Gkrellmrc
autocmd BufNewFile,BufRead gkrellmrc,gkrellmrc_?	setf gkrellmrc

// Gleam
autocmd BufNewFile,BufRead *.gleam			setf gleam

// GLSL
// Extensions supported by Khronos reference compiler (with one exception, ".glsl")
// https://github.com/KhronosGroup/glslang
autocmd BufNewFile,BufRead *.vert,*.tesc,*.tese,*.glsl,*.geom,*.frag,*.comp,*.rgen,*.rmiss,*.rchit,*.rahit,*.rint,*.rcall	setf glsl

// GN (generate ninja) files
autocmd BufNewFile,BufRead *.gn,*.gni		setf gn

// GP scripts (2.0 and onward)
autocmd BufNewFile,BufRead *.gp,.gprc		setf gp

// GPG
autocmd BufNewFile,BufRead */.gnupg/options		setf gpg
autocmd BufNewFile,BufRead */.gnupg/gpg.conf		setf gpg
autocmd BufNewFile,BufRead */usr/*/gnupg/options.skel setf gpg
if !empty($GNUPGHOME)
  autocmd BufNewFile,BufRead $GNUPGHOME/options	setf gpg
  autocmd BufNewFile,BufRead $GNUPGHOME/gpg.conf	setf gpg
endif

// gnash(1) configuration files
autocmd BufNewFile,BufRead gnashrc,.gnashrc,gnashpluginrc,.gnashpluginrc setf gnash

// Gitolite
autocmd BufNewFile,BufRead gitolite.conf		setf gitolite
autocmd BufNewFile,BufRead {,.}gitolite.rc,example.gitolite.rc	setf perl

// Glimmer-flavored TypeScript and JavaScript
autocmd BufNewFile,BufRead *.gts			setf typescript.glimmer
autocmd BufNewFile,BufRead *.gjs			setf javascript.glimmer

// Gnuplot scripts
autocmd BufNewFile,BufRead *.gpi,*.gnuplot,.gnuplot_history	setf gnuplot

// GNU Radio Companion files
autocmd BufNewFile,BufRead *.grc
	\ if getline(1) =~# '<?xml' |
	\   setf xml |
	\ else |
	\   setf yaml |
	\ endif

// Go (Google)
autocmd BufNewFile,BufRead *.go			setf go
autocmd BufNewFile,BufRead Gopkg.lock		setf toml
autocmd BufRead,BufNewFile go.work			setf gowork

// GoAccess configuration
autocmd BufNewFile,BufRead goaccess.conf		setf goaccess

// GrADS scripts
autocmd BufNewFile,BufRead *.gs			setf grads

// GraphQL
autocmd BufNewFile,BufRead *.graphql,*.graphqls,*.gql			setf graphql

// Gretl
autocmd BufNewFile,BufRead *.gretl			setf gretl

// Groovy
autocmd BufNewFile,BufRead *.gradle,*.groovy,Jenkinsfile		setf groovy

// GNU Server Pages
autocmd BufNewFile,BufRead *.gsp			setf gsp

// Group file
autocmd BufNewFile,BufRead */etc/group,*/etc/group-,*/etc/group.edit,*/etc/gshadow,*/etc/gshadow-,*/etc/gshadow.edit,*/var/backups/group.bak,*/var/backups/gshadow.bak  setf group

// GTK RC
autocmd BufNewFile,BufRead .gtkrc,gtkrc		setf gtkrc

// GYP
autocmd BufNewFile,BufRead *.gyp,*.gypi		setf gyp

// Hack
autocmd BufRead,BufNewFile *.hack,*.hackpartial			setf hack

// Haml
autocmd BufNewFile,BufRead *.haml			setf haml

// Hamster Classic | Playground files
autocmd BufNewFile,BufRead *.hsm			setf hamster

// Handlebars
autocmd BufNewFile,BufRead *.hbs			setf handlebars

// Hare
autocmd BufNewFile,BufRead *.ha			setf hare
autocmd BufNewFile,BufRead README			call dist#ft#FTharedoc()

// Haskell
autocmd BufNewFile,BufRead *.hs,*.hsc,*.hs-boot,*.hsig setf haskell
autocmd BufNewFile,BufRead *.lhs			setf lhaskell
autocmd BufNewFile,BufRead *.chs			setf chaskell
autocmd BufNewFile,BufRead cabal.project		setf cabalproject
autocmd BufNewFile,BufRead */{.,}cabal/config	setf cabalconfig
autocmd BufNewFile,BufRead cabal.config		setf cabalconfig
autocmd BufNewFile,BufRead *.persistentmodels	setf haskellpersistent

// Haste
autocmd BufNewFile,BufRead *.ht			setf haste
autocmd BufNewFile,BufRead *.htpp			setf hastepreproc

// Haxe
autocmd BufNewFile,BufRead *.hx			setf haxe

// HCL
autocmd BufRead,BufNewFile *.hcl			setf hcl

// Go checksum file (must be before *.sum Hercules)
autocmd BufNewFile,BufRead go.sum,go.work.sum	setf gosum

// Hercules
autocmd BufNewFile,BufRead *.vc,*.ev,*.sum,*.errsum	setf hercules

// HEEx
autocmd BufRead,BufNewFile *.heex			setf heex

// HEX (Intel)
autocmd BufNewFile,BufRead *.hex,*.ihex,*.int,*.ihe,*.ihx,*.mcs,*.h32,*.h80,*.h86,*.a43,*.a90	setf hex

// Hjson
autocmd BufNewFile,BufRead *.hjson			setf hjson

// HLS Playlist (or another form of playlist)
autocmd BufNewFile,BufRead *.m3u,*.m3u8		setf hlsplaylist

// Hollywood
autocmd BufRead,BufNewFile *.hws			setf hollywood

// Hoon
autocmd BufRead,BufNewFile *.hoon			setf hoon

// TI Code Composer Studio General Extension Language
autocmd BufNewFile,BufRead *.gel			setf gel

// Tilde (must be before HTML)
autocmd BufNewFile,BufRead *.t.html			setf tilde

// Translate shell
autocmd BufNewFile,BufRead init.trans,*/etc/translate-shell,.trans	setf clojure

// HTML (.stm for server side, .shtml is server-side or superhtml)
autocmd BufNewFile,BufRead *.html,*.htm,*.shtml,*.stm  call dist#ft#FThtml()
autocmd BufNewFile,BufRead *.cshtml			setf html

// HTTP request files
autocmd BufNewFile,BufRead *.http			setf http

// HTML with Ruby - eRuby
autocmd BufNewFile,BufRead *.erb,*.rhtml		setf eruby

// HTML with M4
autocmd BufNewFile,BufRead *.html.m4			setf htmlm4

// Some template.  Used to be HTML Cheetah.
autocmd BufNewFile,BufRead *.tmpl			setf template

// Host config
autocmd BufNewFile,BufRead */etc/host.conf		setf hostconf

// Hosts access
autocmd BufNewFile,BufRead */etc/hosts.allow,*/etc/hosts.deny  setf hostsaccess

// Hurl
autocmd BufRead,BufNewFile *.hurl			setf hurl

// Hy
autocmd BufRead,BufNewFile *.hy,.hy-history		setf hy

// Hyper Builder
autocmd BufNewFile,BufRead *.hb			setf hb

// Hyprland Configuration language
autocmd BufNewFile,BufRead */hypr/*.conf,hypr\(land\|paper\|idle\|lock\).conf setf hyprlang

// Httest
autocmd BufNewFile,BufRead *.htt,*.htb		setf httest

// i3
autocmd BufNewFile,BufRead */i3/config		setf i3config
autocmd BufNewFile,BufRead */.i3/config		setf i3config

// sway
autocmd BufNewFile,BufRead */sway/config		setf swayconfig
autocmd BufNewFile,BufRead */.sway/config		setf swayconfig

// Icon
autocmd BufNewFile,BufRead *.icn			setf icon

// IDL (Interface Description Language)
autocmd BufNewFile,BufRead *.idl			call dist#ft#FTidl()

// Microsoft IDL (Interface Description Language)  Also *.idl
// MOF = WMI (Windows Management Instrumentation) Managed Object Format
autocmd BufNewFile,BufRead *.odl,*.mof		setf msidl

// Icewm menu
autocmd BufNewFile,BufRead */.icewm/menu		setf icemenu

// Indent profile (must come before IDL *.pro!)
autocmd BufNewFile,BufRead .indent.pro		setf indent
autocmd BufNewFile,BufRead indent.pro		call dist#ft#ProtoCheck('indent')

// IDL (Interactive Data Language), Prolog, Cproto or zsh module C
autocmd BufNewFile,BufRead *.pro			call dist#ft#ProtoCheck('idlang')

// Idris2
autocmd BufNewFile,BufRead *.idr			setf idris2
autocmd BufNewFile,BufRead *.lidr			setf lidris2

// Indent RC
autocmd BufNewFile,BufRead indentrc			setf indent

// Inform
autocmd BufNewFile,BufRead *.inf,*.INF		setf inform

// Initng
autocmd BufNewFile,BufRead */etc/initng/*/*.i,*.ii	setf initng

// Innovation Data Processing
autocmd BufRead,BufNewFile upstream.dat\c,upstream.*.dat\c,*.upstream.dat\c	setf upstreamdat
autocmd BufRead,BufNewFile fdrupstream.log,upstream.log\c,upstream.*.log\c,*.upstream.log\c,UPSTREAM-*.log\c	setf upstreamlog
autocmd BufRead,BufNewFile upstreaminstall.log\c,upstreaminstall.*.log\c,*.upstreaminstall.log\c setf upstreaminstalllog
autocmd BufRead,BufNewFile usserver.log\c,usserver.*.log\c,*.usserver.log\c	setf usserverlog
autocmd BufRead,BufNewFile usw2kagt.log\c,usw2kagt.*.log\c,*.usw2kagt.log\c	setf usw2kagtlog

// Ipfilter
autocmd BufNewFile,BufRead ipf.conf,ipf6.conf,ipf.rules	setf ipfilter

// Ipkg for Idris 2 language
autocmd BufNewFile,BufRead *.ipkg			setf ipkg

// Informix 4GL (source - canonical, include file, I4GL+M4 preproc.)
autocmd BufNewFile,BufRead *.4gl,*.4gh,*.m4gl	setf fgl

// .INI file for MSDOS
autocmd BufNewFile,BufRead *.ini,*.INI		setf dosini

// SysV Inittab
autocmd BufNewFile,BufRead inittab			setf inittab

// Inko
autocmd BufNewFile,BufRead *.inko			setf inko

// Inno Setup
autocmd BufNewFile,BufRead *.iss			setf iss

// J
autocmd BufNewFile,BufRead *.ijs			setf j

// JAL
autocmd BufNewFile,BufRead *.jal,*.JAL		setf jal

// Jam
autocmd BufNewFile,BufRead *.jpl,*.jpr		setf jam

// Janet
autocmd BufNewFile,BufRead *.janet			setf janet

// Java
autocmd BufNewFile,BufRead *.java,*.jav,*.jsh	setf java

// JavaCC
autocmd BufNewFile,BufRead *.jj,*.jjt		setf javacc

// JavaScript, ECMAScript, ES module script, CommonJS script
autocmd BufNewFile,BufRead *.js,*.jsm,*.javascript,*.es,*.mjs,*.cjs   setf javascript
autocmd BufNewFile,BufRead .node_repl_history	setf javascript

// JavaScript with React
autocmd BufNewFile,BufRead *.jsx			setf javascriptreact

// Java Server Pages
autocmd BufNewFile,BufRead *.jsp			setf jsp

// Java Properties resource file (note: doesn't catch font.properties.pl)
autocmd BufNewFile,BufRead *.properties,*.properties_??,*.properties_??_??	setf jproperties
// Eclipse preference files use Java Properties syntax
autocmd BufNewFile,BufRead org.eclipse.*.prefs	setf jproperties

// Jess
autocmd BufNewFile,BufRead *.clp			setf jess

// Jgraph
autocmd BufNewFile,BufRead *.jgr			setf jgraph

// Jinja
autocmd BufNewFile,BufRead *.jinja			setf jinja

// Jujutsu
autocmd BufNewFile,BufRead *.jjdescription		setf jjdescription

// Jovial
autocmd BufNewFile,BufRead *.jov,*.j73,*.jovial	setf jovial

// Jq
autocmd BufNewFile,BufRead *.jq			setf jq

// JSON5
autocmd BufNewFile,BufRead *.json5			setf json5

// JSON Patch (RFC 6902)
autocmd BufNewFile,BufRead *.json-patch		setf json

// Geojson is also json
autocmd BufNewFile,BufRead *.geojson			setf json

// Jupyter Notebook and jupyterlab config is also json
autocmd BufNewFile,BufRead *.ipynb,*.jupyterlab-settings	setf json

// Sublime config
autocmd BufNewFile,BufRead *.sublime-project,*.sublime-settings,*.sublime-workspace	setf json

// Other files that look like json
autocmd BufNewFile,BufRead .prettierrc,.firebaserc,.stylelintrc,.lintstagedrc,flake.lock,deno.lock,.swcrc,composer.lock,symfony.lock	setf json

// JSONC (JSON with comments)
autocmd BufNewFile,BufRead *.jsonc,.babelrc,.eslintrc,.jsfmtrc,bun.lock	setf jsonc
autocmd BufNewFile,BufRead .jshintrc,.jscsrc,.vsconfig,.hintrc,.swrc,[jt]sconfig*.json	setf jsonc
// Visual Studio Code settings
autocmd BufRead,BufNewFile ~/*/{Code,VSCodium}/User/*.json setf jsonc

// JSON
autocmd BufNewFile,BufRead *.json,*.jsonp,*.webmanifest	setf json

// JSON Lines
autocmd BufNewFile,BufRead *.jsonl			setf jsonl

// Jsonnet
autocmd BufNewFile,BufRead *.jsonnet,*.libsonnet	setf jsonnet

// Julia
autocmd BufNewFile,BufRead *.jl			setf julia

// Just
autocmd BufNewFile,BufRead \c{,*.}justfile,\c*.just setf just

// KAREL
autocmd BufNewFile,BufRead *.kl setf karel

// KDL
autocmd BufNewFile,BufRead *.kdl			setf kdl

// Kixtart
autocmd BufNewFile,BufRead *.kix			setf kix

// Kuka Robot Language
autocmd BufNewFile,BufRead *.src			call dist#ft#FTsrc()
autocmd BufNewFile,BufRead *.dat			call dist#ft#FTdat()
autocmd BufNewFile,BufRead *.sub			setf krl

// Kimwitu[++]
autocmd BufNewFile,BufRead *.k			setf kwt

// Kivy
autocmd BufNewFile,BufRead *.kv			setf kivy

// Kotlin
autocmd BufNewFile,BufRead *.kt,*.ktm,*.kts		setf kotlin

// KDE script
autocmd BufNewFile,BufRead *.ks			setf kscript

// Kconfig
autocmd BufNewFile,BufRead Kconfig,Kconfig.debug,Config.in	setf kconfig

// Lace (ISE)
autocmd BufNewFile,BufRead *.ace,*.ACE		setf lace

// Lalrpop
autocmd BufNewFile,Bufread *.lalrpop			setf lalrpop

// Larch Shared Language
autocmd BufNewFile,BufRead .lsl			call dist#ft#FTlsl()

// Latexmkrc
autocmd BufNewFile,BufRead .latexmkrc,latexmkrc	setf perl

// Latte
autocmd BufNewFile,BufRead *.latte,*.lte		setf latte

// Limits
autocmd BufNewFile,BufRead */etc/limits,*/etc/*limits.conf,*/etc/*limits.d/*.conf	setf limits

// LambdaProlog or SML (see dist#ft#FTmod for *.mod)
autocmd BufNewFile,BufRead *.sig			call dist#ft#FTsig()

// LDAP configuration
autocmd BufNewFile,BufRead ldaprc,.ldaprc,ldap.conf	setf ldapconf

// LDAP LDIF
autocmd BufNewFile,BufRead *.ldif			setf ldif

// Luadoc, Ldoc (must be before *.ld)
autocmd BufNewFile,BufRead config.ld			setf lua

// Ld loader
autocmd BufNewFile,BufRead *.ld,*/ldscripts/*	setf ld

// Lean
autocmd BufNewFile,BufRead *.lean			setf lean

// Ledger
autocmd BufRead,BufNewFile *.ldg,*.ledger,*.journal			setf ledger

// lf configuration (lfrc)
autocmd BufNewFile,BufRead lfrc			setf lf

// Leo
autocmd BufNewFile,BufRead *.leo			setf leo

// Less
autocmd BufNewFile,BufRead *.less			setf less

// Lex
autocmd BufNewFile,BufRead *.lex,*.l,*.lxx,*.l++	setf lex

// Libao
autocmd BufNewFile,BufRead */etc/libao.conf,*/.libao	setf libao

// Libsensors
autocmd BufNewFile,BufRead */etc/sensors.conf,*/etc/sensors3.conf	setf sensors

// LFTP
autocmd BufNewFile,BufRead lftp.conf,.lftprc,*lftp/rc	setf lftp

// Lifelines, LLVM, or Lex for C++
autocmd BufNewFile,BufRead *.ll			call dist#ft#FTll()

// Lilo: Linux loader
autocmd BufNewFile,BufRead lilo.conf			setf lilo

// Lilypond
autocmd BufNewFile,BufRead *.ly,*.ily		setf lilypond

// Lisp (*.el = ELisp)
// *.jl was removed, it's also used for Julia, better skip than guess wrong.
autocmd BufNewFile,BufRead *.lsp,*.lisp,*.asd,*.el,.emacs,.sawfishrc setf lisp

// *.cl = Common Lisp or OpenCL
autocmd BufNewFile,BufRead *.cl call dist#ft#FTcl()

// SBCL implementation of Common Lisp
autocmd BufNewFile,BufRead sbclrc,.sbclrc		setf lisp

// Liquidsoap
autocmd BufNewFile,BufRead *.liq			setf liquidsoap

// Liquid
autocmd BufNewFile,BufRead *.liquid			setf liquid

// Lite
autocmd BufNewFile,BufRead *.lite,*.lt		setf lite

// LiteStep RC files
autocmd BufNewFile,BufRead */LiteStep/*/*.rc		setf litestep

// Livebook
autocmd BufNewFile,BufRead *.livemd			setf livebook

// Login access
autocmd BufNewFile,BufRead */etc/login.access	setf loginaccess

// Login defs
autocmd BufNewFile,BufRead */etc/login.defs		setf logindefs

// Logtalk
autocmd BufNewFile,BufRead *.lgt			setf logtalk

// LOTOS
autocmd BufNewFile,BufRead *.lotos		setf lotos

// LOTOS or LaTeX \listoftables files
autocmd BufNewFile,BufRead *.lot
	\ if getline(1) =~# '\\contentsline' |setf tex|else|setf lotos|endif

// Lout (also: *.lt)
autocmd BufNewFile,BufRead *.lou,*.lout		setf lout

// Lua, Texlua
autocmd BufNewFile,BufRead *.lua,*.tlu,.lua_history	setf lua

// Luau
autocmd BufNewFile,BufRead *.luau		setf luau

// Luau config
autocmd BufNewFile,BufRead .luaurc		setf jsonc

// Luacheck
autocmd BufNewFile,BufRead .luacheckrc		setf lua

// Luarocks
autocmd BufNewFile,BufRead *.rockspec,rock_manifest	setf lua

// Linden Scripting Language (Second Life)
autocmd BufNewFile,BufRead *.lsl			call dist#ft#FTlsl()

// Lynx style file (or LotusScript!)
autocmd BufNewFile,BufRead *.lss			setf lss

// M4
autocmd BufNewFile,BufRead *.m4
	\ if expand("<afile>") !~? 'html.m4$\|fvwm2rc' | setf m4 | endif
autocmd BufNewFile,BufRead .m4_history		setf m4

// MaGic Point
autocmd BufNewFile,BufRead *.mgp			setf mgp

// Mail (for Elm, trn, mutt, muttng, rn, slrn, neomutt)
autocmd BufNewFile,BufRead snd.\d\+,.letter,.letter.\d\+,.followup,.article,.article.\d\+,pico.\d\+,ae\d\+.txt,/tmp/SLRN[0-9A-Z.]\+,*.eml setf mail

// Mail aliases
autocmd BufNewFile,BufRead */etc/mail/aliases,*/etc/aliases	setf mailaliases

// Mailcap configuration file
autocmd BufNewFile,BufRead .mailcap,mailcap		setf mailcap

// Makefile
autocmd BufNewFile,BufRead *[mM]akefile,*.mk,*.mak	call dist#ft#FTmake()
autocmd BufNewFile,BufRead Kbuild			setf make

// MakeIndex
autocmd BufNewFile,BufRead *.ist,*.mst		setf ist

// Mallard
autocmd BufNewFile,BufRead *.page			setf mallard

// Manpage
autocmd BufNewFile,BufRead *.man			setf man

// Man config
autocmd BufNewFile,BufRead */etc/man.conf,man.config	setf manconf

// Maple V
autocmd BufNewFile,BufRead *.mv,*.mpl,*.mws		setf maple

// Map (UMN mapserver config file)
autocmd BufNewFile,BufRead *.map
	\ if getline(1) =~ '^\*\+$' |
	\   setf lnkmap |
	\ else |
	\   setf map |
	\ endif

// Markdown
autocmd BufNewFile,BufRead *.markdown,*.mdown,*.mkd,*.mkdn,*.mdwn,*.md
	\ if exists("g:filetype_md") |
	\   exe "setf " .. g:filetype_md |
	\ else |
	\   setf markdown |
	\ endif

// Mason (it used to include *.comp, are those Mason files?)
autocmd BufNewFile,BufRead *.mason,*.mhtml	setf mason

// Mathematica, Matlab, Murphi, Objective C or Octave
autocmd BufNewFile,BufRead *.m			call dist#ft#FTm()

// Mathematica notebook and package files
autocmd BufNewFile,BufRead *.nb,*.wl			setf mma

// Maya Extension Language
autocmd BufNewFile,BufRead *.mel			setf mel

// mbsync
autocmd BufNewFile,BufRead *.mbsyncrc,isyncrc	setf mbsync

// mcmeta
autocmd BufNewFile,BufRead *.mcmeta			setf json

// MediaWiki
autocmd BufNewFile,BufRead *.mw,*.wiki		setf mediawiki

// Mercurial (hg) commit file
autocmd BufNewFile,BufRead hg-editor-*.txt		setf hgcommit

// Mercurial config (looks like generic config file)
autocmd BufNewFile,BufRead *.hgrc,*hgrc		setf cfg

// Mermaid
autocmd BufNewFile,BufRead *.mmd,*.mmdc,*.mermaid	setf mermaid

// Meson Build system config
autocmd BufNewFile,BufRead meson.build,meson.options,meson_options.txt setf meson
autocmd BufNewFile,BufRead *.wrap			setf dosini

// Metafont
autocmd BufNewFile,BufRead *.mf			setf mf

// MetaPost
autocmd BufNewFile,BufRead *.mp			setf mp
autocmd BufNewFile,BufRead *.mpxl,*.mpiv,*.mpvi	let b:mp_metafun = 1 | setf mp

// MGL
autocmd BufNewFile,BufRead *.mgl			setf mgl

// MIX - Knuth assembly
autocmd BufNewFile,BufRead *.mix,*.mixal		setf mix

// MMIX or VMS makefile
autocmd BufNewFile,BufRead *.mms			call dist#ft#FTmms()

// msmtp
autocmd BufNewFile,BufRead .msmtprc			setf msmtp

// Symbian meta-makefile definition (MMP)
autocmd BufNewFile,BufRead *.mmp			setf mmp

// ABB Rapid, Modula-2, Modsim III or LambdaProlog
autocmd BufNewFile,BufRead *.mod			call dist#ft#FTmod()
autocmd BufNewFile,BufRead *.modx			setf rapid

// Modula-3 (.m3, .i3, .mg, .ig)
autocmd BufNewFile,BufRead *.[mi][3g]		setf modula3

// Larch/Modula-3
autocmd BufNewFile,BufRead *.lm3			setf modula3

// Modconf
autocmd BufNewFile,BufRead */etc/modules.conf,*/etc/modules,*/etc/conf.modules setf modconf

// Monk
autocmd BufNewFile,BufRead *.isc,*.monk,*.ssc,*.tsc	setf monk

// MOO
autocmd BufNewFile,BufRead *.moo			setf moo

// Moonscript
autocmd BufNewFile,BufRead *.moon			setf moonscript

// Move language
autocmd BufNewFile,BufRead *.move			setf move

// MPD is based on XML
autocmd BufNewFile,BufRead *.mpd			setf xml

// Mplayer config
autocmd BufNewFile,BufRead mplayer.conf,*/.mplayer/config	setf mplayerconf

// Motorola S record
autocmd BufNewFile,BufRead *.s19,*.s28,*.s37,*.mot,*.srec	setf srec

// Mrxvtrc
autocmd BufNewFile,BufRead mrxvtrc,.mrxvtrc		setf mrxvtrc

// Msql
autocmd BufNewFile,BufRead *.msql			setf msql

// Mysql
autocmd BufNewFile,BufRead *.mysql,.mysql_history	setf mysql

// Tcl Shell RC file
autocmd BufNewFile,BufRead tclsh.rc			setf tcl

// M$ Resource files
// /etc/Muttrc.d/file.rc is muttrc
autocmd BufNewFile,BufRead *.rc,*.rch
	\ if expand("<afile>") !~ "/etc/Muttrc.d/" |
	\   setf rc |
	\ endif

// Mojo
// Mojo files use either .mojo or .🔥 as extension
autocmd BufNewFile,BufRead *.mojo,*.🔥		setf mojo

// MuPAD source
autocmd BufRead,BufNewFile *.mu			setf mupad

// Mush
autocmd BufNewFile,BufRead *.mush			setf mush

// Mustache
autocmd BufNewFile,BufRead *.mustache		setf mustache

// Mutt setup file (also for Muttng)
autocmd BufNewFile,BufRead Mutt{ng,}rc		setf muttrc

// N1QL
autocmd BufRead,BufNewfile *.n1ql,*.nql		setf n1ql

// Neomutt log
autocmd BufNewFile,BufRead *.neomuttdebug*		setf neomuttlog

// Nano
autocmd BufNewFile,BufRead */etc/nanorc,*.nanorc	setf nanorc

// Natural
autocmd BufNewFile,BufRead *.NS[ACGLMNPS]		setf natural

// Noemutt setup file
autocmd BufNewFile,BufRead Neomuttrc			setf neomuttrc

// Netrc
autocmd BufNewFile,BufRead .netrc			setf netrc

// Neofetch
autocmd BufNewFile,BufRead */neofetch/config.conf	setf sh

// Nginx
autocmd BufNewFile,BufRead *.nginx,nginx*.conf,*nginx.conf,*/nginx/*.conf	setf nginx

// Nim file
autocmd BufNewFile,BufRead *.nim,*.nims,*.nimble	setf nim

// Ninja file
autocmd BufNewFile,BufRead *.ninja			setf ninja

// Nix
autocmd BufRead,BufNewFile *.nix			setf nix

// Norg
autocmd BufNewFile,BufRead *.norg		setf norg

// NPM RC file
autocmd BufNewFile,BufRead npmrc,.npmrc		setf dosini

// Novell netware batch files
autocmd BufNewFile,BufRead *.ncf			setf ncf

// Nroff/Troff (*.ms and *.t are checked below)
autocmd BufNewFile,BufRead *.me
	\ if expand("<afile>") != "read.me" && expand("<afile>") != "click.me" |
	\   setf nroff |
	\ endif
autocmd BufNewFile,BufRead *.tr,*.nr,*.roff,*.tmac,*.mom	setf nroff
autocmd BufNewFile,BufRead *.[0-9],*.[013]p,*.[1-8]x,*.3{am,perl,pm,posix,type},*.n	call dist#ft#FTnroff()

// Nroff or Objective C++
autocmd BufNewFile,BufRead *.mm			call dist#ft#FTmm()

// Not Quite C
autocmd BufNewFile,BufRead *.nqc			setf nqc

// notmuch
autocmd BufNewFile,BufRead .notmuch-config{,.*}		setf dosini
autocmd BufNewFile,BufRead ~/.config/notmuch/*/config	setf dosini
if exists('$XDG_CONFIG_HOME')
  autocmd BufNewFile,BufRead $XDG_CONFIG_HOME/notmuch/*/config setf dosini
endif

// NSE - Nmap Script Engine - uses Lua syntax
autocmd BufNewFile,BufRead *.nse			setf lua

// NSIS
autocmd BufNewFile,BufRead *.nsi,*.nsh		setf nsis

// N-Triples
autocmd BufNewFile,BufRead *.nt			setf ntriples

// Nu
autocmd BufNewFile,BufRead *.nu		setf nu

// Numbat
autocmd BufNewFile,BufRead *.nbt		setf numbat

// Oblivion Language and Oblivion Script Extender
autocmd BufNewFile,BufRead *.obl,*.obse,*.oblivion,*.obscript  setf obse

// Objdump
autocmd BufNewFile,BufRead *.objdump,*.cppobjdump  setf objdump

// OCaml
autocmd BufNewFile,BufRead *.ml,*.mli,*.mll,*.mly,.ocamlinit,*.mlt,*.mlp,*.mlip,*.mli.cppo,*.ml.cppo setf ocaml

// Occam
autocmd BufNewFile,BufRead *.occ			setf occam

// Octave
autocmd BufNewFile,BufRead octave.conf,.octaverc,octaverc,*/octave/history	setf octave

// Odin
autocmd BufNewFile,BufRead *.odin			setf odin

// Omnimark
autocmd BufNewFile,BufRead *.xom,*.xin		setf omnimark

// ondir
autocmd BufNewFile,BufRead .ondirrc			setf ondir

// OPAM
autocmd BufNewFile,BufRead opam,*.opam,*.opam.template,opam.locked,*.opam.locked setf opam

// OpenAL Soft config files
autocmd BufNewFile,BufRead .alsoftrc,alsoft.conf,alsoft.ini,alsoftrc.sample setf dosini

// OpenFOAM
autocmd BufNewFile,BufRead fvSchemes,fvSolution,fvConstrains,fvModels,*/constant/g	call dist#ft#FTfoam()

// OpenROAD
autocmd BufNewFile,BufRead *.or				setf openroad

// OPL
autocmd BufNewFile,BufRead *.[Oo][Pp][Ll]			setf opl

// OpenSCAD
autocmd BufNewFile,BufRead *.scad				setf openscad

// Oracle config file
autocmd BufNewFile,BufRead *.ora				setf ora

// Org (Emacs' org-mode)
autocmd BufNewFile,BufRead *.org,*.org_archive		setf org

// Packet filter conf
autocmd BufNewFile,BufRead pf.conf				setf pf

// ini style config files, using # comments
autocmd BufNewFile,BufRead pacman.conf,mpv.conf		setf confini
autocmd BufNewFile,BufRead */.aws/config,*/.aws/credentials	setf confini
autocmd BufNewFile,BufRead *.nmconnection			setf confini
autocmd BufNewFile,BufRead paru.conf				setf confini
autocmd BufNewFile,BufRead */{,.}gnuradio/*.conf		setf confini
autocmd BufNewFile,BufRead */gnuradio/conf.d/*.conf		setf confini

// Pacman hooks
autocmd BufNewFile,BufRead *.hook
	\ if getline(1) == '[Trigger]' |
	\   setf confini |
	\ endif

// Pacman makepkg
autocmd BufNewFile,BufRead {.,}makepkg.conf			setf sh

// Pacman log
autocmd BufRead pacman.log*					call s:StarSetf('pacmanlog')

// Pam conf
autocmd BufNewFile,BufRead */etc/pam.conf			setf pamconf

// Pam environment
autocmd BufNewFile,BufRead pam_env.conf,.pam_environment	setf pamenv

// PApp
autocmd BufNewFile,BufRead *.papp,*.pxml,*.pxsl		setf papp

// Password file
autocmd BufNewFile,BufRead */etc/passwd,*/etc/passwd-,*/etc/passwd.edit,*/etc/shadow,*/etc/shadow-,*/etc/shadow.edit,*/var/backups/passwd.bak,*/var/backups/shadow.bak setf passwd

// Pascal (also *.p, *.pp, *.inc)
autocmd BufNewFile,BufRead *.pas				setf pascal

// Pascal or Puppet manifest
autocmd BufNewFile,BufRead *.pp				call dist#ft#FTpp()

// Delphi
autocmd BufNewFile,BufRead *.dpr				setf pascal

// Xilinx labtools project file or Lazarus program file
autocmd BufNewFile,BufRead *.lpr
	\ if getline(1) =~# "<?xml" |
	\   setf xml |
	\ else |
	\   setf pascal |
	\ endif

// Free Pascal makefile definition file
autocmd BufNewFile,BufRead *.fpc				setf fpcmake

// Path of Exile item filter
autocmd BufNewFile,BufRead *.filter				setf poefilter

// PDF
autocmd BufNewFile,BufRead *.pdf				setf pdf

// PCMK - HAE - crm configure edit
autocmd BufNewFile,BufRead *.pcmk				setf pcmk

// PEM (Privacy-Enhanced Mail)
autocmd BufNewFile,BufRead *.pem,*.cer,*.crt,*.csr		setf pem

// Perl or Prolog
autocmd BufNewFile,BufRead *.pl				call dist#ft#FTpl()
autocmd BufNewFile,BufRead *.plx,*.al,*.psgi			setf perl

// Perl Reply
autocmd BufNewFile,BufRead .replyrc				setf dosini

// Perl, XPM or XPM2
autocmd BufNewFile,BufRead *.pm
	\ if getline(1) =~ "XPM2" |
	\   setf xpm2 |
	\ elseif getline(1) =~ "XPM" |
	\   setf xpm |
	\ else |
	\   setf perl |
	\ endif

// Perl POD
autocmd BufNewFile,BufRead *.pod			setf pod

// Php, php3, php4, etc.
// Also Phtml (was used for PHP 2 in the past).
// Also .ctp for Cake template file.
// Also .phpt for php tests.
// Also .theme for Drupal theme files.
autocmd BufNewFile,BufRead *.php,*.php\d,*.phtml,*.ctp,*.phpt,*.theme	setf php

// Pike and Cmod
autocmd BufNewFile,BufRead *.pike,*.pmod		setf pike
autocmd BufNewFile,BufRead *.cmod			setf cmod

// Pinfo config
autocmd BufNewFile,BufRead */etc/pinforc,*/.pinforc	setf pinfo

// Palm Resource compiler
autocmd BufNewFile,BufRead *.rcp			setf pilrc

// Pine config
autocmd BufNewFile,BufRead .pinerc,pinerc,.pinercex,pinercex		setf pine

// Pip requirements
autocmd BufNewFile,BufRead *.pip			setf requirements
autocmd BufNewFile,BufRead requirements.txt		setf requirements
autocmd BufNewFile,BufRead *-requirements.txt	setf requirements
autocmd BufNewFile,BufRead requirements-*.txt	setf requirements
autocmd BufNewFile,BufRead constraints.txt		setf requirements
autocmd BufNewFile,BufRead requirements.in		setf requirements
autocmd BufNewFile,BufRead requirements/*.txt	setf requirements
autocmd BufNewFile,BufRead requires/*.txt		setf requirements

// Pipenv Pipfiles
autocmd BufNewFile,BufRead Pipfile			setf toml
autocmd BufNewFile,BufRead Pipfile.lock		setf json

// Pixi lock
autocmd BufNewFile,BufRead pixi.lock			setf yaml

// Pkl
autocmd BufNewFile,BufRead *.pkl			setf pkl

// PL/1, PL/I
autocmd BufNewFile,BufRead *.pli,*.pl1		setf pli

// PL/M (also: *.inp)
autocmd BufNewFile,BufRead *.plm,*.p36,*.pac		setf plm

// PL/SQL
autocmd BufNewFile,BufRead *.pls,*.plsql		setf plsql

// PLP
autocmd BufNewFile,BufRead *.plp			setf plp

// PO and PO template (GNU gettext)
autocmd BufNewFile,BufRead *.po,*.pot		setf po

// Pony
autocmd BufNewFile,BufRead *.pony			setf pony

// Postfix main config
autocmd BufNewFile,BufRead main.cf,main.cf.proto	setf pfmain

// PostScript (+ font files, encapsulated PostScript, Adobe Illustrator)
autocmd BufNewFile,BufRead *.ps,*.pfa,*.afm,*.eps,*.epsf,*.epsi,*.ai	  setf postscr

// PostScript Printer Description
autocmd BufNewFile,BufRead *.ppd			setf ppd

// Povray
autocmd BufNewFile,BufRead *.pov			setf pov

// Povray configuration
autocmd BufNewFile,BufRead .povrayrc			setf povini

// Povray, Pascal, PHP or assembly
autocmd BufNewFile,BufRead *.inc			call dist#ft#FTinc()

// PowerShell
autocmd BufNewFile,BufRead	*.ps1,*.psd1,*.psm1,*.pssc	setf ps1
autocmd BufNewFile,BufRead	*.ps1xml			setf ps1xml
autocmd BufNewFile,BufRead	*.cdxml,*.psc1			setf xml

// Power Query M
autocmd BufNewFile,BufRead *.pq			setf pq

// Printcap and Termcap
autocmd BufNewFile,BufRead *printcap
	\ let b:ptcap_type = "print" | setf ptcap
autocmd BufNewFile,BufRead *termcap
	\ let b:ptcap_type = "term" | setf ptcap

// Prisma
autocmd BufRead,BufNewFile *.prisma			setf prisma

// PPWizard
autocmd BufNewFile,BufRead *.it,*.ih			setf ppwiz

// Pug
autocmd BufRead,BufNewFile *.pug			setf pug

// Puppet
autocmd BufNewFile,BufRead Puppetfile		setf ruby

// Embedded Puppet
autocmd BufNewFile,BufRead *.epp			setf epuppet

// Obj 3D file format
// TODO: is there a way to avoid MS-Windows Object files?
autocmd BufNewFile,BufRead *.obj			setf obj

// Oracle Pro*C/C++
autocmd BufNewFile,BufRead *.pc			setf proc

// Privoxy actions file
autocmd BufNewFile,BufRead *.action			setf privoxy

// Procmail
autocmd BufNewFile,BufRead .procmail,.procmailrc	setf procmail

// Progress or CWEB
autocmd BufNewFile,BufRead *.w			call dist#ft#FTprogress_cweb()

// Progress or assembly or Swig
autocmd BufNewFile,BufRead *.i			call dist#ft#FTi()

// Progress or Pascal
autocmd BufNewFile,BufRead *.p			call dist#ft#FTprogress_pascal()

// Software Distributor Product Specification File (POSIX 1387.2-1995)
autocmd BufNewFile,BufRead *.psf			setf psf
autocmd BufNewFile,BufRead INDEX,INFO
	\ if getline(1) =~ '^\s*\(distribution\|installed_software\|root\|bundle\|product\)\s*$' |
	\   setf psf |
	\ endif

// Prolog
autocmd BufNewFile,BufRead *.pdb			setf prolog

// Promela
autocmd BufNewFile,BufRead *.pml			setf promela

// Property Specification Language (PSL)
autocmd BufNewFile,BufRead *.psl			setf psl

// Google protocol buffers
autocmd BufNewFile,BufRead *.proto			setf proto
autocmd BufNewFile,BufRead *.txtpb,*.textproto,*.textpb,*.pbtxt setf pbtxt

// Poke
autocmd BufNewFile,BufRead *.pk			setf poke

// Protocols
autocmd BufNewFile,BufRead */etc/protocols		setf protocols

// Nvidia PTX (Parallel Thread Execution)
// See https://docs.nvidia.com/cuda/parallel-thread-execution/
autocmd BufNewFile,BufRead *.ptx			setf ptx

// Purescript
autocmd BufNewFile,BufRead *.purs			setf purescript

// PyPA manifest files
autocmd BufNewFile,BufRead MANIFEST.in		setf pymanifest

// Pyret
autocmd BufNewFile,BufRead *.arr			setf pyret

// Pyrex/Cython
autocmd BufNewFile,BufRead *.pyx,*.pyx+,*.pxd,*.pxi	setf pyrex

// Python, Python Shell Startup and Python Stub Files
// Quixote (Python-based web framework) and IPython
autocmd BufNewFile,BufRead *.py,*.pyw,.pythonstartup,.pythonrc,.python_history,.jline-jython.history	setf python
autocmd BufNewFile,BufRead *.ipy,*.ptl,*.pyi,SConstruct		   setf python

// QL
autocmd BufRead,BufNewFile *.ql,*.qll		setf ql

// QML
autocmd BufRead,BufNewFile *.qml,*.qbs			setf qml

// QMLdir
autocmd BufRead,BufNewFile qmldir			setf qmldir

// Quarto
autocmd BufRead,BufNewFile *.qmd			setf quarto

// QuickBms
autocmd BufRead,BufNewFile *.bms			setf quickbms

// Racket (formerly detected as "scheme")
autocmd BufNewFile,BufRead *.rkt,*.rktd,*.rktl	setf racket

// Radiance
autocmd BufNewFile,BufRead *.rad,*.mat		setf radiance

// Raku (formerly Perl6)
autocmd BufNewFile,BufRead *.pm6,*.p6,*.t6,*.pod6,*.raku,*.rakumod,*.rakudoc,*.rakutest  setf raku

// Ratpoison config/command files
autocmd BufNewFile,BufRead .ratpoisonrc,ratpoisonrc	setf ratpoison

// RCS file
autocmd BufNewFile,BufRead *\,v			setf rcs

// Readline
autocmd BufNewFile,BufRead .inputrc,inputrc		setf readline

// Registry for MS-Windows
autocmd BufNewFile,BufRead *.reg
	\ if getline(1) =~? '^REGEDIT[0-9]*\s*$\|^Windows Registry Editor Version \d*\.\d*\s*$' | setf registry | endif

// Renderman Interface Bytestream
autocmd BufNewFile,BufRead *.rib			setf rib

// Rego Policy Language
autocmd BufNewFile,BufRead *.rego			setf rego

// Rexx
autocmd BufNewFile,BufRead *.rex,*.orx,*.rxo,*.rxj,*.jrexx,*.rexxj,*.rexx,*.testGroup,*.testUnit	setf rexx

// Ripgrep rc
autocmd BufNewFile,BufRead {.,}ripgreprc			setf conf

// R Help file
autocmd BufNewFile,BufRead *.rd			setf rhelp

// R noweb file
autocmd BufNewFile,BufRead *.rnw,*.snw			setf rnoweb

// R Markdown file
autocmd BufNewFile,BufRead *.rmd,*.smd			setf rmd

// R profile file
autocmd BufNewFile,BufRead .Rhistory,.Rprofile,Rprofile,Rprofile.site	setf r

// RSS looks like XML
autocmd BufNewFile,BufRead *.rss				setf xml

// R reStructuredText file
autocmd BufNewFile,BufRead *.rrst,*.srst			setf rrst

// Rexx, Rebol or R
autocmd BufNewFile,BufRead *.r,*.R				call dist#ft#FTr()

// Remind
autocmd BufNewFile,BufRead .reminders,*.remind,*.rem		setf remind

// ReScript
autocmd BufNewFile,BufRead *.res,*.resi			setf rescript

// Resolv.conf
autocmd BufNewFile,BufRead resolv.conf		setf resolv

// Relax NG Compact
autocmd BufNewFile,BufRead *.rnc			setf rnc

// Relax NG XML
autocmd BufNewFile,BufRead *.rng			setf rng

// ILE RPG
autocmd BufNewFile,BufRead *.rpgle,*.rpgleinc	setf rpgle

// RPL/2
autocmd BufNewFile,BufRead *.rpl			setf rpl

// Robot Framework
autocmd BufNewFile,BufRead *.robot,*.resource	setf robot

// Robots.txt
autocmd BufNewFile,BufRead robots.txt		setf robots

// Roc
autocmd BufNewFile,BufRead *.roc			setf roc

// RON (Rusty Object Notation)
autocmd BufNewFile,BufRead *.ron			setf ron

// MikroTik RouterOS script
autocmd BufRead,BufNewFile *.rsc			setf routeros

// Rpcgen
autocmd BufNewFile,BufRead *.x			setf rpcgen

// reStructuredText Documentation Format
autocmd BufNewFile,BufRead *.rst			setf rst

// RTF
autocmd BufNewFile,BufRead *.rtf			setf rtf

// Interactive Ruby shell
autocmd BufNewFile,BufRead .irbrc,irbrc,.irb_history,irb_history	setf ruby

// Ruby
autocmd BufNewFile,BufRead *.rb,*.rbw		setf ruby

// RubyGems
autocmd BufNewFile,BufRead *.gemspec			setf ruby

// RBS (Ruby Signature)
autocmd BufNewFile,BufRead *.rbs			setf rbs

// Rackup
autocmd BufNewFile,BufRead *.ru			setf ruby

// Bundler
autocmd BufNewFile,BufRead Gemfile			setf ruby

// Ruby on Rails
autocmd BufNewFile,BufRead *.builder,*.rxml,*.rjs	setf ruby

// Rantfile and Rakefile is like Ruby
autocmd BufNewFile,BufRead [rR]antfile,*.rant,[rR]akefile,*.rake	setf ruby

// Rust
autocmd BufNewFile,BufRead *.rs			setf rust
autocmd BufNewFile,BufRead Cargo.lock,*/.cargo/config,*/.cargo/credentials	setf toml

// S-lang
autocmd BufNewFile,BufRead *.sl			setf slang

// Sage
autocmd BufNewFile,BufRead *.sage			setf sage

// Samba config
autocmd BufNewFile,BufRead smb.conf			setf samba

// SAS script
autocmd BufNewFile,BufRead *.sas			setf sas

// Sass
autocmd BufNewFile,BufRead *.sass			setf sass

// Sather, TI linear assembly
autocmd BufNewFile,BufRead *.sa			call dist#ft#FTsa()

// Scala
autocmd BufNewFile,BufRead *.scala,*.mill		setf scala

// SBT - Scala Build Tool
autocmd BufNewFile,BufRead *.sbt			setf sbt

// Slang Shading Language
autocmd BufNewFile,BufRead *.slang			setf shaderslang

// Slint
autocmd BufNewFile,BufRead *.slint			setf slint

// SuperCollider
autocmd BufNewFile,BufRead *.sc			call dist#ft#FTsc()

autocmd BufNewFile,BufRead *.quark			setf supercollider

// scdoc
autocmd BufNewFile,BufRead *.scd			call dist#ft#FTscd()

// Scilab
autocmd BufNewFile,BufRead *.sci,*.sce		setf scilab


// SCSS
autocmd BufNewFile,BufRead *.scss			setf scss

// SD: Streaming Descriptors
autocmd BufNewFile,BufRead *.sd			setf sd

// SDL
autocmd BufNewFile,BufRead *.sdl,*.pr		setf sdl

// sed
autocmd BufNewFile,BufRead *.sed			setf sed

// SubRip
autocmd BufNewFile,BufRead *.srt			setf srt

// SubStation Alpha
autocmd BufNewFile,BufRead *.ass,*.ssa		setf ssa

// svelte
autocmd BufNewFile,BufRead *.svelte			setf svelte

// Sieve (RFC 3028, 5228)
autocmd BufNewFile,BufRead *.siv,*.sieve		setf sieve

// Sendmail
autocmd BufNewFile,BufRead sendmail.cf		setf sm

// Sendmail .mc files are actually m4.  Could also be MS Message text file or
// Maxima.
autocmd BufNewFile,BufRead *.mc			call dist#ft#McSetf()

// Services
autocmd BufNewFile,BufRead */etc/services		setf services

// Service Location config
autocmd BufNewFile,BufRead */etc/slp.conf		setf slpconf

// Service Location registration
autocmd BufNewFile,BufRead */etc/slp.reg		setf slpreg

// Service Location SPI
autocmd BufNewFile,BufRead */etc/slp.spi		setf slpspi

// Setserial config
autocmd BufNewFile,BufRead */etc/serial.conf		setf setserial

// SGML
autocmd BufNewFile,BufRead *.sgm,*.sgml
	\ if getline(1).getline(2).getline(3).getline(4).getline(5) =~? 'linuxdoc' |
	\   setf sgmllnx |
	\ elseif getline(1) =~ '<!DOCTYPE.*DocBook' || getline(2) =~ '<!DOCTYPE.*DocBook' |
	\   let b:docbk_type = "sgml" |
	\   let b:docbk_ver = 4 |
	\   setf docbk |
	\ else |
	\   setf sgml |
	\ endif

// SGMLDECL
autocmd BufNewFile,BufRead *.decl,*.dcl,*.dec
	\ if getline(1).getline(2).getline(3) =~? '^<!SGML' |
	\    setf sgmldecl |
	\ endif

// SGML catalog file
autocmd BufNewFile,BufRead catalog			setf catalog

// Shell scripts (sh, ksh, bash, bash2, csh); Allow .profile_foo etc.
// Gentoo ebuilds and Arch Linux PKGBUILDs are actually bash scripts.
// NOTE: Patterns ending in a star are further down, these have lower priority.
autocmd BufNewFile,BufRead .bashrc,bashrc,bash.bashrc,.bash[_-]profile,.bash[_-]logout,.bash[_-]aliases,.bash[_-]history,bash-fc[-.],*.ebuild,*.bash,*.eclass,PKGBUILD,*.bats,*.cygport call dist#ft#SetFileTypeSH("bash")
autocmd BufNewFile,BufRead .kshrc,*.ksh call dist#ft#SetFileTypeSH("ksh")
autocmd BufNewFile,BufRead */etc/profile,.profile,*.sh,*.env{rc,} call dist#ft#SetFileTypeSH(getline(1))
// Alpine Linux APKBUILDs are actually POSIX sh scripts with special treatment.
autocmd BufNewFile,BufRead APKBUILD	setf apkbuild

// Shell script (Arch Linux) or PHP file (Drupal)
autocmd BufNewFile,BufRead *.install
	\ if getline(1) =~ '<?php' |
	\   setf php |
	\ else |
	\   call dist#ft#SetFileTypeSH("bash") |
	\ endif

// tcsh scripts (patterns ending in a star further below)
autocmd BufNewFile,BufRead .tcshrc,*.tcsh,tcsh.tcshrc,tcsh.login	call dist#ft#SetFileTypeShell("tcsh")

// csh scripts, but might also be tcsh scripts (on some systems csh is tcsh)
// (patterns ending in a start further below)
autocmd BufNewFile,BufRead .login,.cshrc,csh.cshrc,csh.login,csh.logout,*.csh,.alias  call dist#ft#CSH()

// TriG
autocmd BufNewFile,BufRead *.trig			setf trig

// Zig and Zig Object Notation (ZON)
autocmd BufNewFile,BufRead *.zig,*.zon		setf zig

// Ziggy and Ziggy Schema
autocmd BufNewFile,BufRead *.ziggy                   setf ziggy
autocmd BufNewFile,BufRead *.ziggy-schema            setf ziggy_schema

// Zserio
autocmd BufNewFile,BufRead *.zs			setf zserio

// Z-Shell script (patterns ending in a star further below)
autocmd BufNewFile,BufRead .zprofile,*/etc/zprofile,.zfbfmarks  setf zsh
autocmd BufNewFile,BufRead .zshrc,.zshenv,.zlogin,.zlogout,.zcompdump,.zsh_history setf zsh
autocmd BufNewFile,BufRead *.zsh,*.zsh-theme,*.zunit		setf zsh

// Salt state files
autocmd BufNewFile,BufRead *.sls			setf salt

// Scheme, Supertux configuration, Lips.js history, Guile init file ("racket" patterns are now separate, see above)
autocmd BufNewFile,BufRead *.scm,*.ss,*.sld,*.stsg,*/supertux2/config,.lips_repl_history,.guile	setf scheme

// Screen RC
autocmd BufNewFile,BufRead .screenrc,screenrc	setf screen

// Sexplib
autocmd BufNewFile,BufRead *.sexp setf sexplib

// Simula
autocmd BufNewFile,BufRead *.sim			setf simula

// SINDA
autocmd BufNewFile,BufRead *.sin,*.s85		setf sinda

// SiSU
autocmd BufNewFile,BufRead *.sst,*.ssm,*.ssi,*.-sst,*._sst setf sisu
autocmd BufNewFile,BufRead *.sst.meta,*.-sst.meta,*._sst.meta setf sisu

// SKILL
autocmd BufNewFile,BufRead *.il,*.ils,*.cdf		setf skill

// Cadence
autocmd BufNewFile,BufRead *.cdc			setf cdc

// SLRN
autocmd BufNewFile,BufRead .slrnrc			setf slrnrc
autocmd BufNewFile,BufRead *.score			setf slrnsc

// Smali
autocmd BufNewFile,BufRead *.smali			setf smali

// Smalltalk
autocmd BufNewFile,BufRead *.st			setf st

// Smalltalk (and Rexx, TeX, and Visual Basic)
autocmd BufNewFile,BufRead *.cls			call dist#ft#FTcls()

// Smarty templates
autocmd BufNewFile,BufRead *.tpl			setf smarty

// SMIL or XML
autocmd BufNewFile,BufRead *.smil
	\ if getline(1) =~ '<?\s*xml.*?>' |
	\   setf xml |
	\ else |
	\   setf smil |
	\ endif

// SMIL or SNMP MIB file
autocmd BufNewFile,BufRead *.smi
	\ if getline(1) =~ '\<smil\>' |
	\   setf smil |
	\ else |
	\   setf mib |
	\ endif

// SMITH
autocmd BufNewFile,BufRead *.smt,*.smith		setf smith

// Smithy
autocmd BufNewFile,BufRead *.smithy			setf smithy

// Snakemake
autocmd BufNewFile,BufRead Snakefile,*.smk		setf snakemake

// Snobol4 and spitbol
autocmd BufNewFile,BufRead *.sno,*.spt		setf snobol4

// SNMP MIB files
autocmd BufNewFile,BufRead *.mib,*.my		setf mib

// Snort Configuration
autocmd BufNewFile,BufRead *.hog,snort.conf,vision.conf	setf hog
autocmd BufNewFile,BufRead *.rules			call dist#ft#FTRules()

// Solidity
autocmd BufRead,BufNewFile *.sol			setf solidity

// SPARQL queries
autocmd BufNewFile,BufRead *.rq,*.sparql		setf sparql

// Spec (Linux RPM)
autocmd BufNewFile,BufRead *.spec			setf spec

// Speedup (AspenTech plant simulator)
autocmd BufNewFile,BufRead *.speedup,*.spdata,*.spd	setf spup

// Slice
autocmd BufNewFile,BufRead *.ice			setf slice

// Microsoft Visual Studio Solution
autocmd BufNewFile,BufRead *.sln			setf solution
autocmd BufNewFile,BufRead *.slnf			setf json
autocmd BufNewFile,BufRead *.slnx			setf xml

// Spice
autocmd BufNewFile,BufRead *.sp,*.spice		setf spice

// Spyce
autocmd BufNewFile,BufRead *.spy,*.spi		setf spyce

// Squid
autocmd BufNewFile,BufRead squid.conf		setf squid

// SQL for Oracle Designer
autocmd BufNewFile,BufRead *.tyb,*.tyc,*.pkb,*.pks	setf sql

// *.typ can be either SQL or Typst files
autocmd BufNewFile,BufRead *.typ			call dist#ft#FTtyp()

// SQL
autocmd BufNewFile,BufRead *.sql			call dist#ft#SQL()
autocmd BufNewFile,BufRead .sqlite_history		setf sql

// SQLJ
autocmd BufNewFile,BufRead *.sqlj			setf sqlj

// PRQL
autocmd BufNewFile,BufRead *.prql			setf prql

// SQR
autocmd BufNewFile,BufRead *.sqr,*.sqi		setf sqr

// Squirrel
autocmd BufNewFile,BufRead *.nut			setf squirrel

// OpenSSH configuration
autocmd BufNewFile,BufRead ssh_config,*/.ssh/config,*/.ssh/*.conf	setf sshconfig
autocmd BufNewFile,BufRead */etc/ssh/ssh_config.d/*.conf		setf sshconfig

// OpenSSH server configuration
autocmd BufNewFile,BufRead sshd_config			setf sshdconfig
autocmd BufNewFile,BufRead */etc/ssh/sshd_config.d/*.conf	setf sshdconfig

// Starlark
autocmd BufNewFile,BufRead *.ipd,*.star,*.starlark	setf starlark

// OpenVPN configuration
autocmd BufNewFile,BufRead *.ovpn			setf openvpn
autocmd BufNewFile,BufRead */openvpn/*/*.conf	setf openvpn

// Stata
autocmd BufNewFile,BufRead *.ado,*.do,*.imata,*.mata	setf stata
// Also *.class, but not when it's a Java bytecode file
autocmd BufNewFile,BufRead *.class
	\ if getline(1) !~ "^\xca\xfe\xba\xbe" | setf stata | endif

// SMCL
autocmd BufNewFile,BufRead *.hlp,*.ihlp,*.smcl	setf smcl

// SPA JSON
autocmd BufNewFile,BufRead */pipewire/*.conf		setf spajson
autocmd BufNewFile,BufRead */wireplumber/*.conf	setf spajson

// Stored Procedures
autocmd BufNewFile,BufRead *.stp			setf stp

// Standard ML
autocmd BufNewFile,BufRead *.sml			setf sml

// Sratus VOS command macro
autocmd BufNewFile,BufRead *.cm			setf voscm

// Sway (programming language)
autocmd BufNewFile,BufRead *.sw			setf sway

// Swift
autocmd BufNewFile,BufRead *.swift,*.swiftinterface	setf swift
autocmd BufNewFile,BufRead *.swift.gyb		setf swiftgyb

// Swift Intermediate Language or SILE
autocmd BufNewFile,BufRead *.sil			call dist#ft#FTsil()

// Swig
autocmd BufNewFile,BufRead *.swg,*.swig setf swig

// Sysctl
autocmd BufNewFile,BufRead */etc/sysctl.conf,*/etc/sysctl.d/*.conf	setf sysctl

// Systemd unit files
autocmd BufNewFile,BufRead */systemd/*.{automount,dnssd,link,mount,netdev,network,nspawn,path,service,slice,socket,swap,target,timer}	setf systemd
// Systemd overrides
autocmd BufNewFile,BufRead */etc/systemd/*.conf.d/*.conf	setf systemd
autocmd BufNewFile,BufRead */etc/systemd/system/*.d/*.conf	setf systemd
autocmd BufNewFile,BufRead */.config/systemd/user/*.d/*.conf	setf systemd
// Systemd temp files
autocmd BufNewFile,BufRead */etc/systemd/system/*.d/.#*	setf systemd
autocmd BufNewFile,BufRead */etc/systemd/system/.#*		setf systemd
autocmd BufNewFile,BufRead */.config/systemd/user/*.d/.#*	setf systemd
autocmd BufNewFile,BufRead */.config/systemd/user/.#*	setf systemd

// Synopsys Design Constraints
autocmd BufNewFile,BufRead *.sdc			setf sdc

// Sudoers
autocmd BufNewFile,BufRead */etc/sudoers,sudoers.tmp	setf sudoers

// SVG (Scalable Vector Graphics)
autocmd BufNewFile,BufRead *.svg			setf svg

// Surface
autocmd BufRead,BufNewFile *.sface			setf surface

// LLVM TableGen
autocmd BufNewFile,BufRead *.td			setf tablegen

// Tads (or Nroff or Perl test file)
autocmd BufNewFile,BufRead *.t
	\ if !dist#ft#FTnroff() && !dist#ft#FTperl() | setf tads | endif

// Tags
autocmd BufNewFile,BufRead tags			setf tags

// TAK
autocmd BufNewFile,BufRead *.tak			setf tak

// Unx Tal
autocmd BufNewFile,BufRead *.tal			setf tal

// Task
autocmd BufRead,BufNewFile {pending,completed,undo}.data  setf taskdata
autocmd BufRead,BufNewFile *.task			setf taskedit

// Tcl (JACL too)
autocmd BufNewFile,BufRead *.tcl,*.tm,*.tk,*.itcl,*.itk,*.jacl,.tclshrc,.wishrc,.tclsh-history	setf tcl

// Xilinx's xsct and xsdb use tcl
autocmd BufNewFile,BufRead .xsctcmdhistory,.xsdbcmdhistory	setf tcl

// templ
autocmd BufNewFile,BufRead *.templ			setf templ

// Teal
autocmd BufRead,BufNewFile *.tl			setf teal

// TealInfo
autocmd BufNewFile,BufRead *.tli			setf tli

// Telix Salt
autocmd BufNewFile,BufRead *.slt			setf tsalt

// Tera Term Language or Turtle
autocmd BufRead,BufNewFile *.ttl
	\ if getline(1) =~ '^@\?\(prefix\|base\)' |
	\   setf turtle |
	\ else |
	\   setf teraterm |
	\ endif

// Terminfo
autocmd BufNewFile,BufRead *.ti			setf terminfo

// Tera
autocmd BufRead,BufNewFile *.tera			setf tera

// Terraform variables
autocmd BufRead,BufNewFile *.tfvars			setf terraform-vars

// TeX
autocmd BufNewFile,BufRead *.latex,*.sty,*.dtx,*.ltx,*.bbl	setf tex
autocmd BufNewFile,BufRead *.tex			call dist#ft#FTtex()
autocmd BufNewFile,BufRead texdoc.cnf		setf conf

// LaTeX packages will generate some medium LaTeX files during compiling
// They should be ignored by .gitignore https://github.com/github/gitignore/blob/main/TeX.gitignore
// Sometime we need to view its content for debugging
autocmd BufNewFile,BufRead *.{pgf,nlo,nls,thm,eps_tex,pygtex,pygstyle,clo,aux,brf,ind,lof,loe,nav,vrb,ins,tikz,bbx,cbx,beamer}	setf tex

// LaTeX files generated by Inkscape
autocmd BufNewFile,BufRead *.pdf_tex			setf tex

// ConTeXt
autocmd BufNewFile,BufRead *.mkii,*.mkiv,*.mkvi,*.mkxl,*.mklx   setf context

// Texinfo
autocmd BufNewFile,BufRead *.texinfo,*.texi,*.txi	setf texinfo

// TeX configuration
autocmd BufNewFile,BufRead texmf.cnf			setf texmf

// Thrift (Apache)
autocmd BufNewFile,BufRead *.thrift			setf thrift

// Tidy config
autocmd BufNewFile,BufRead .tidyrc,tidyrc,tidy.conf	setf tidy

// TF (TinyFugue) mud client
autocmd BufNewFile,BufRead .tfrc,tfrc		setf tf

// TF (TinyFugue) mud client or terraform
autocmd BufNewFile,BufRead *.tf			call dist#ft#FTtf()

// TLA+
autocmd BufNewFile,BufRead *.tla			setf tla

// tmux configuration
autocmd BufNewFile,BufRead {.,}tmux*.conf		setf tmux

// TOML
autocmd BufNewFile,BufRead *.toml,uv.lock		setf toml

// TPP - Text Presentation Program
autocmd BufNewFile,BufRead *.tpp			setf tpp

// TRACE32 Script Language
autocmd BufNewFile,BufRead *.cmm,*.cmmt,*.t32	setf trace32

// Treetop
autocmd BufRead,BufNewFile *.treetop			setf treetop

// Trustees
autocmd BufNewFile,BufRead trustees.conf		setf trustees

// TSS - Geometry
autocmd BufNewFile,BufReadPost *.tssgm		setf tssgm

// TSS - Optics
autocmd BufNewFile,BufReadPost *.tssop		setf tssop

// TSS - Command Line (temporary)
autocmd BufNewFile,BufReadPost *.tsscl		setf tsscl

// TSV Files
autocmd BufNewFile,BufRead *.tsv			setf tsv

// Tutor mode
autocmd BufNewFile,BufReadPost *.tutor		setf tutor

// TWIG files
autocmd BufNewFile,BufReadPost *.twig		setf twig

// TypeScript or Qt translation file (which is XML)
autocmd BufNewFile,BufReadPost *.ts
	\ if getline(1) =~ '<?xml' |
	\   setf xml |
	\ else |
	\   setf typescript |
	\ endif
autocmd BufNewFile,BufRead .ts_node_repl_history	setf typescript

// TypeScript module and common
autocmd BufNewFile,BufRead *.mts,*.cts		setf typescript

// TypeScript with React
autocmd BufNewFile,BufRead *.tsx			setf typescriptreact

// TypeSpec files
autocmd BufNewFile,BufRead *.tsp			setf typespec

// Motif UIT/UIL files
autocmd BufNewFile,BufRead *.uit,*.uil		setf uil

// Udev conf
autocmd BufNewFile,BufRead */etc/udev/udev.conf	setf udevconf

// Udev permissions
autocmd BufNewFile,BufRead */etc/udev/permissions.d/*.permissions setf udevperm
//
// Udev symlinks config
autocmd BufNewFile,BufRead */etc/udev/cdsymlinks.conf	setf sh

// Ungrammar, AKA Un-grammar
autocmd BufNewFile,BufRead *.ungram			setf ungrammar

// UnrealScript
autocmd BufNewFile,BufRead *.uc			setf uc

// Updatedb
autocmd BufNewFile,BufRead */etc/updatedb.conf	setf updatedb

// Upstart (init(8)) config files
autocmd BufNewFile,BufRead */usr/share/upstart/*.conf	       setf upstart
autocmd BufNewFile,BufRead */usr/share/upstart/*.override	       setf upstart
autocmd BufNewFile,BufRead */etc/init/*.conf,*/etc/init/*.override  setf upstart
autocmd BufNewFile,BufRead */.init/*.conf,*/.init/*.override	       setf upstart
autocmd BufNewFile,BufRead */.config/upstart/*.conf		       setf upstart
autocmd BufNewFile,BufRead */.config/upstart/*.override	       setf upstart

// URL shortcut
autocmd BufNewFile,BufRead *.url			setf urlshortcut

// V
autocmd BufNewFile,BufRead *.vsh,*.vv			setf v

// Vala
autocmd BufNewFile,BufRead *.vala			setf vala

// VDF
autocmd BufNewFile,BufRead *.vdf			setf vdf

// VDM
autocmd BufRead,BufNewFile *.vdmpp,*.vpp		setf vdmpp
autocmd BufRead,BufNewFile *.vdmrt			setf vdmrt
autocmd BufRead,BufNewFile *.vdmsl,*.vdm		setf vdmsl

// Vento
autocmd BufNewFile,BufRead *.vto			setf vento

// Vera
autocmd BufNewFile,BufRead *.vr,*.vri,*.vrh		setf vera

// Vagrant (uses Ruby syntax)
autocmd BufNewFile,BufRead Vagrantfile		setf ruby

// Verilog HDL, V or Coq
autocmd BufNewFile,BufRead *.v			call dist#ft#FTv()

// Verilog-AMS HDL
autocmd BufNewFile,BufRead *.va,*.vams		setf verilogams

// SystemVerilog
autocmd BufNewFile,BufRead *.sv,*.svh		setf systemverilog

// VHS tape
// .tape is also used by TapeCalc, which we do not support ATM.  If TapeCalc
// support is needed the contents of the file needs to be inspected.
autocmd BufNewFile,BufRead *.tape			setf vhs

// VHDL
autocmd BufNewFile,BufRead *.hdl,*.vhd,*.vhdl,*.vbe,*.vst,*.vho  setf vhdl

// Vim script
autocmd BufNewFile,BufRead *.vim,.exrc,_exrc,.netrwhist	setf vim

// Viminfo file
autocmd BufNewFile,BufRead .viminfo,_viminfo		setf viminfo

// Virata Config Script File or Drupal module
autocmd BufRead,BufNewFile *.hw,*.module,*.pkg
	\ if getline(1) =~ '<?php' |
	\   setf php |
	\ else |
	\   setf virata |
	\ endif

// Visual Basic (see also *.bas *.cls)

// Visual Basic or FORM
autocmd BufNewFile,BufRead *.frm			call dist#ft#FTfrm()

// Visual Basic
// user control, ActiveX document form, active designer, property page
autocmd BufNewFile,BufRead *.ctl,*.dob,*.dsr,*.pag	setf vb

// Visual Basic or Vimball Archiver
autocmd BufNewFile,BufRead *.vba			call dist#ft#FTvba()

// Visual Basic Project
autocmd BufNewFile,BufRead *.vbp			setf dosini

// VBScript (close to Visual Basic)
autocmd BufNewFile,BufRead *.vbs			setf vb

// Visual Basic .NET (close to Visual Basic)
autocmd BufNewFile,BufRead *.vb			setf vb

// Visual Studio Macro
autocmd BufNewFile,BufRead *.dsm			setf vb

// SaxBasic (close to Visual Basic)
autocmd BufNewFile,BufRead *.sba			setf vb

// Vgrindefs file
autocmd BufNewFile,BufRead vgrindefs			setf vgrindefs

// VRML V1.0c
autocmd BufNewFile,BufRead *.wrl			setf vrml

// Vroom (vim testing and executable documentation)
autocmd BufNewFile,BufRead *.vroom			setf vroom

// Vue.js Single File Component
autocmd BufNewFile,BufRead *.vue			setf vue

// Waybar config
autocmd BufNewFile,BufRead */waybar/config		setf jsonc

// WebAssembly
autocmd BufNewFile,BufRead *.wat,*.wast		setf wat

// WebAssembly Interface Type (WIT)
autocmd BufNewFile,BufRead *.wit			setf wit

// Webmacro
autocmd BufNewFile,BufRead *.wm			setf webmacro

// Wget config
autocmd BufNewFile,BufRead .wgetrc,wgetrc		setf wget

// Wget2 config
autocmd BufNewFile,BufRead .wget2rc,wget2rc		setf wget2

// WebGPU Shading Language (WGSL)
autocmd BufNewFile,BufRead *.wgsl			setf wgsl

// Website MetaLanguage
autocmd BufNewFile,BufRead *.wml			setf wml

// Winbatch
autocmd BufNewFile,BufRead *.wbt			setf winbatch

// WSML
autocmd BufNewFile,BufRead *.wsml			setf wsml

// WPL
autocmd BufNewFile,BufRead *.wpl			setf xml

// WvDial
autocmd BufNewFile,BufRead wvdial.conf,.wvdialrc	setf wvdial

// CVS RC file
autocmd BufNewFile,BufRead .cvsrc			setf cvsrc

// CVS commit file
autocmd BufNewFile,BufRead cvs\d\+			setf cvs

// WEB (*.web is also used for Winbatch: Guess, based on expecting "%" comment
// lines in a WEB file).
autocmd BufNewFile,BufRead *.web
	\ if getline(1)[0].getline(2)[0].getline(3)[0].getline(4)[0].getline(5)[0] =~ "%" |
	\   setf web |
	\ else |
	\   setf winbatch |
	\ endif

// Windows Scripting Host and Windows Script Component
autocmd BufNewFile,BufRead *.ws[fc]			setf wsh

// Xdg-user-dirs
autocmd BufNewFile,BufRead user-dirs.dirs,user-dirs.defaults		setf sh

// XHTML
autocmd BufNewFile,BufRead *.xhtml,*.xht		setf xhtml

// X11vnc
autocmd BufNewFile,BufRead .x11vncrc			setf conf

// Xprofile
autocmd BufNewFile,BufRead .xprofile			setf sh

// X Pixmap (dynamically sets colors, this used to trigger on BufEnter to make
// it work better, but that breaks setting 'filetype' manually)
autocmd BufNewFile,BufRead *.xpm
	\ if getline(1) =~ "XPM2" |
	\   setf xpm2 |
	\ else |
	\   setf xpm |
	\ endif
autocmd BufNewFile,BufRead *.xpm2			setf xpm2

// XFree86 config
autocmd BufNewFile,BufRead XF86Config
	\ if getline(1) =~ '\<XConfigurator\>' |
	\   let b:xf86conf_xfree86_version = 3 |
	\ endif |
	\ setf xf86conf
autocmd BufNewFile,BufRead */xorg.conf.d/*.conf
	\ let b:xf86conf_xfree86_version = 4 |
	\ setf xf86conf

// Xorg config
autocmd BufNewFile,BufRead xorg.conf,xorg.conf-4	let b:xf86conf_xfree86_version = 4 | setf xf86conf

// Xinetd conf
autocmd BufNewFile,BufRead */etc/xinetd.conf		setf xinetd

// Xilinx Vivado/Vitis project files and block design files
autocmd BufNewFile,BufRead *.xpr,*.xpfm,*.spfm,*.bxml,*.mmi		setf xml
autocmd BufNewFile,BufRead *.bd,*.bda,*.xci				setf json
autocmd BufNewFile,BufRead *.mss					setf mss

// XS Perl extension interface language
autocmd BufNewFile,BufRead *.xs			setf xs

// X compose file
autocmd BufNewFile,BufRead .XCompose,Compose	setf xcompose

// X resources file
autocmd BufNewFile,BufRead .Xdefaults,.Xpdefaults,.Xresources,xdm-config,*.ad setf xdefaults

// Xmath
autocmd BufNewFile,BufRead *.msc,*.msf		setf xmath
autocmd BufNewFile,BufRead *.ms
	\ if !dist#ft#FTnroff() | setf xmath | endif

// XML  specific variants: docbk and xbl
autocmd BufNewFile,BufRead *.xml			call dist#ft#FTxml()

// XMI (holding UML models) is also XML
autocmd BufNewFile,BufRead *.xmi			setf xml

// CSPROJ files are Visual Studio.NET's XML-based C# project config files
autocmd BufNewFile,BufRead *.csproj,*.csproj.user	setf xml

// FSPROJ files are Visual Studio.NET's XML-based F# project config files
autocmd BufNewFile,BufRead *.fsproj,*.fsproj.user	setf xml

// VBPROJ files are Visual Studio.NET's XML-based Visual Basic project config files
autocmd BufNewFile,BufRead *.vbproj,*.vbproj.user	setf xml

// MSBUILD configuration files are also XML
autocmd BufNewFile,BufRead Directory.Packages.props,Directory.Build.targets,Directory.Build.props	setf xml

// Unison Language
autocmd BufNewFile,BufRead *.u,*.uu				setf unison

// Qt Linguist translation source and Qt User Interface Files are XML
// However, for .ts TypeScript is more common.
autocmd BufNewFile,BufRead *.ui			setf xml

// TPM's are RDF-based descriptions of TeX packages (Nikolai Weibull)
autocmd BufNewFile,BufRead *.tpm			setf xml

// Xdg menus
autocmd BufNewFile,BufRead */etc/xdg/menus/*.menu	setf xml

// ATI graphics driver configuration
autocmd BufNewFile,BufRead fglrxrc			setf xml

// Web Services Description Language (WSDL)
autocmd BufNewFile,BufRead *.wsdl			setf xml

// Workflow Description Language (WDL)
autocmd BufNewFile,BufRead *.wdl			setf wdl

// XLIFF (XML Localisation Interchange File Format) is also XML
autocmd BufNewFile,BufRead *.xlf			setf xml
autocmd BufNewFile,BufRead *.xliff			setf xml

// XML User Interface Language
autocmd BufNewFile,BufRead *.xul			setf xml

// X11 xmodmap (also see below)
autocmd BufNewFile,BufRead *Xmodmap			setf xmodmap

// Xquery
autocmd BufNewFile,BufRead *.xq,*.xql,*.xqm,*.xquery,*.xqy	setf xquery

// XSD
autocmd BufNewFile,BufRead *.xsd			setf xsd

// Xslt
autocmd BufNewFile,BufRead *.xsl,*.xslt		setf xslt

// Yacc
autocmd BufNewFile,BufRead *.yy,*.yxx,*.y++		setf yacc

// Yacc or racc
autocmd BufNewFile,BufRead *.y			call dist#ft#FTy()

// Yaml
autocmd BufNewFile,BufRead *.yaml,*.yml,*.eyaml		setf yaml
autocmd BufNewFile,BufRead */.kube/config	setf yaml

// Raml
autocmd BufNewFile,BufRead *.raml			setf raml

// yum conf (close enough to dosini)
autocmd BufNewFile,BufRead */etc/yum.conf		setf dosini

// YANG
autocmd BufRead,BufNewFile *.yang			setf yang

// Yuck
autocmd BufNewFile,BufRead *.yuck			setf yuck

// Zimbu
autocmd BufNewFile,BufRead *.zu			setf zimbu
// Zimbu Templates
autocmd BufNewFile,BufRead *.zut			setf zimbutempl

// Zope
//   dtml (zope dynamic template markup language), pt (zope page template),
//   cpt (zope form controller page template)
autocmd BufNewFile,BufRead *.dtml,*.pt,*.cpt		call dist#ft#FThtml()
//   zsql (zope sql method)
autocmd BufNewFile,BufRead *.zsql			call dist#ft#SQL()

// Z80 assembler asz80
autocmd BufNewFile,BufRead *.z8a			setf z8a

augroup END

// Check for "*" after loading myfiletypefile
// Don't do this for compressed files.
augroup filetypedetect


// Plain text files, needs to be far down to not override others.  This avoids
// the "conf" type being used if there is a line starting with '#'.
// But before patterns matching everything in a directory.
autocmd BufNewFile,BufRead *.text,README,LICENSE,COPYING,AUTHORS	setf text


// Extra checks for when no filetype has been detected now.  Mostly used for
// patterns that end in "*".  E.g., "zsh*" matches "zsh.vim", but that's a Vim script file.
// Most of these should call s:StarSetf() to avoid names ending in .gz and the
// like are used.

// More Apache style config files
autocmd BufNewFile,BufRead */etc/proftpd/*.conf*,*/etc/proftpd/conf.*/*	call s:StarSetf('apachestyle')
autocmd BufNewFile,BufRead proftpd.conf*					call s:StarSetf('apachestyle')

// More Apache config files
autocmd BufNewFile,BufRead access.conf*,apache.conf*,apache2.conf*,httpd.conf*,httpd-*.conf*,srm.conf*,proxy-html.conf*	call s:StarSetf('apache')
autocmd BufNewFile,BufRead */etc/apache2/*.conf*,*/etc/apache2/conf.*/*,*/etc/apache2/mods-*/*,*/etc/apache2/sites-*/*,*/etc/httpd/conf.*/*,*/etc/httpd/mods-*/*,*/etc/httpd/sites-*/*,*/etc/httpd/conf.d/*.conf*		call s:StarSetf('apache')

// APT config file
autocmd BufNewFile,BufRead */etc/apt/apt.conf.d/{[-_[:alnum:]]\+,[-_.[:alnum:]]\+.conf} call s:StarSetf('aptconf')

// Asterisk config file
autocmd BufNewFile,BufRead *asterisk/*.conf*		call s:StarSetf('asterisk')
autocmd BufNewFile,BufRead *asterisk*/*voicemail.conf* call s:StarSetf('asteriskvm')

// Bazaar version control
autocmd BufNewFile,BufRead bzr_log.*			setf bzr

// Bazel and Buck2 build file
if !has("fname_case")
  autocmd BufNewFile,BufRead *.BUILD,BUILD,BUCK	setf bzl
endif

// BIND zone
autocmd BufNewFile,BufRead */named/db.*,*/bind/db.*	call s:StarSetf('bindzone')

autocmd BufNewFile,BufRead cabal.project.*		call s:StarSetf('cabalproject')

// Calendar
autocmd BufNewFile,BufRead */.calendar/*,
	\*/share/calendar/*/calendar.*,*/share/calendar/calendar.*
	\					call s:StarSetf('calendar')

// Changelog
autocmd BufNewFile,BufRead [cC]hange[lL]og*
	\ if getline(1) =~ '; urgency='
	\|  call s:StarSetf('debchangelog')
	\|else
	\|  call s:StarSetf('changelog')
	\|endif

// Crontab
autocmd BufNewFile,BufRead crontab,crontab.*,*/etc/cron.d/*		call s:StarSetf('crontab')

// dnsmasq(8) configuration
autocmd BufNewFile,BufRead */etc/dnsmasq.d/*		call s:StarSetf('dnsmasq')

// Dockerfile
autocmd BufNewFile,BufRead Dockerfile.*,Containerfile.*	call s:StarSetf('dockerfile')

// Dracula
autocmd BufNewFile,BufRead drac.*			call s:StarSetf('dracula')

// Execline (s6) scripts
autocmd BufNewFile,BufRead s6-*			call s:StarSetf('execline')

// Fvwm
autocmd BufNewFile,BufRead */.fvwm/*			call s:StarSetf('fvwm')
autocmd BufNewFile,BufRead *fvwmrc*,*fvwm95*.hook
	\ let b:fvwm_version = 1 | call s:StarSetf('fvwm')
autocmd BufNewFile,BufRead *fvwm2rc*
	\ if expand("<afile>:e") == "m4"
	\|  call s:StarSetf('fvwm2m4')
	\|else
	\|  let b:fvwm_version = 2 | call s:StarSetf('fvwm')
	\|endif

// Gedcom
autocmd BufNewFile,BufRead */tmp/lltmp*		call s:StarSetf('gedcom')

// Git
autocmd BufNewFile,BufRead */.gitconfig.d/*,*/etc/gitconfig.d/*	call s:StarSetf('gitconfig')

// Gitolite
autocmd BufNewFile,BufRead */gitolite-admin/conf/*	call s:StarSetf('gitolite')

// GTK RC
autocmd BufNewFile,BufRead .gtkrc*,gtkrc*		call s:StarSetf('gtkrc')

// Jam
autocmd BufNewFile,BufRead Prl*.*,JAM*.*		call s:StarSetf('jam')

// Jargon
autocmd! BufNewFile,BufRead *jarg*
	\ if getline(1).getline(2).getline(3).getline(4).getline(5) =~? 'THIS IS THE JARGON FILE'
	\|  call s:StarSetf('jargon')
	\|endif

// Java Properties resource file (note: doesn't catch font.properties.pl)
autocmd BufNewFile,BufRead *.properties_??_??_*	call s:StarSetf('jproperties')

// Kconfig
autocmd BufNewFile,BufRead Kconfig.*,Config.in.*	call s:StarSetf('kconfig')

// Lilo: Linux loader
autocmd BufNewFile,BufRead lilo.conf*		call s:StarSetf('lilo')

// Libsensors
autocmd BufNewFile,BufRead */etc/sensors.d/[^.]*	call s:StarSetf('sensors')

// Logcheck
autocmd BufNewFile,BufRead */etc/logcheck/*.d*/*	call s:StarSetf('logcheck')

// Makefile
autocmd BufNewFile,BufRead [mM]akefile*		if expand('<afile>:t') !~ g:ft_ignore_pat | call dist#ft#FTmake() | endif

// Ruby Makefile
autocmd BufNewFile,BufRead [rR]akefile*		call s:StarSetf('ruby')

// Mail (also matches muttrc.vim, so this is below the other checks)
autocmd BufNewFile,BufRead {neo,}mutt[[:alnum:]._-]\\\{6\}	setf mail

autocmd BufNewFile,BufRead reportbug-*		call s:StarSetf('mail')

// Messages (logs mostly)
autocmd BufNewFile,BufRead */log/{auth,cron,daemon,debug,kern,lpr,mail,messages,news/news,syslog,user}{,.log,.err,.info,.warn,.crit,.notice}{,.[0-9]*,-[0-9]*}
      \ 					call s:StarSetf('messages')

// Modconf
autocmd BufNewFile,BufRead */etc/modutils/*
	\ if executable(expand("<afile>")) != 1
	\|  call s:StarSetf('modconf')
	\|endif
autocmd BufNewFile,BufRead */etc/modprobe.*		call s:StarSetf('modconf')

// Mutt setup files (must be before catch *.rc)
autocmd BufNewFile,BufRead */etc/Muttrc.d/*		call s:StarSetf('muttrc')

// Mutt setup file
autocmd BufNewFile,BufRead .mutt{ng,}rc*,*/.mutt{ng,}/mutt{ng,}rc*	call s:StarSetf('muttrc')
autocmd BufNewFile,BufRead mutt{ng,}rc*,Mutt{ng,}rc*		call s:StarSetf('muttrc')

// Neomutt setup file
autocmd BufNewFile,BufRead .neomuttrc*,*/.neomutt/neomuttrc*	call s:StarSetf('neomuttrc')
autocmd BufNewFile,BufRead neomuttrc*,Neomuttrc*		call s:StarSetf('neomuttrc')

// Nfs
autocmd BufNewFile,BufRead nfs.conf,nfsmount.conf		setf dosini

// Nginx
autocmd BufNewFile,BufRead */etc/nginx/*,*/usr/local/nginx/conf/*	call s:StarSetf('nginx')

// Nroff macros
autocmd BufNewFile,BufRead tmac.*			call s:StarSetf('nroff')

// OpenBSD hostname.if
autocmd BufNewFile,BufRead */etc/hostname.*		call s:StarSetf('config')

// OpenFOAM
autocmd BufNewFile,BufRead [a-zA-Z0-9]*Dict{,.*},[a-zA-Z]*Properties{,.*},*Transport.*,*/0{,.orig}/*
      \ if expand("<amatch>") !~ g:ft_ignore_pat
      \|  call dist#ft#FTfoam()
      \|endif

// Pam conf
autocmd BufNewFile,BufRead */etc/pam.d/*		call s:StarSetf('pamconf')

// Pandoc
autocmd BufNewFile,BufRead,BufFilePost *.pandoc,*.pdk,*.pd,*.pdc	setf pandoc

// PHP config
autocmd BufNewFile,BufRead php.ini-*,php-fpm.conf*,www.conf*		call s:StarSetf('dosini')

// Printcap and Termcap
autocmd BufNewFile,BufRead *printcap*
	\ if !did_filetype()
	\|  let b:ptcap_type = "print" | call s:StarSetf('ptcap')
	\|endif
autocmd BufNewFile,BufRead *termcap*
	\ if !did_filetype()
	\|  let b:ptcap_type = "term" | call s:StarSetf('ptcap')
	\|endif

// ReDIF
// Only used when the .rdf file was not detected to be XML.
autocmd BufRead,BufNewFile *.rdf			call dist#ft#Redif()

// Remind
autocmd BufNewFile,BufRead .reminders*		call s:StarSetf('remind')

// SGML catalog file
autocmd BufNewFile,BufRead sgml.catalog*		call s:StarSetf('catalog')

// Stylus
autocmd BufNewFile,BufReadPost *.styl,*.stylus	setf stylus

// avoid doc files being recognized a shell files
autocmd BufNewFile,BufRead */doc/{,.}bash[_-]completion{,.d,.sh}{,/*} setf text

// Shell scripts ending in a star
autocmd BufNewFile,BufRead .bashrc*,.bash[_-]profile*,.bash[_-]logout*,.bash[_-]aliases*,bash-fc[-.]*,PKGBUILD*,APKBUILD*,*/{,.}bash[_-]completion{,.d,.sh}{,/*} call dist#ft#SetFileTypeSH("bash")
autocmd BufNewFile,BufRead .kshrc* call dist#ft#SetFileTypeSH("ksh")
autocmd BufNewFile,BufRead .profile* call dist#ft#SetFileTypeSH(getline(1))

// Sudoers
autocmd BufNewFile,BufRead */etc/sudoers.d/*		call s:StarSetf('sudoers')

// tcsh scripts ending in a star
autocmd BufNewFile,BufRead .tcshrc*	call dist#ft#SetFileTypeShell("tcsh")

// csh scripts ending in a star
autocmd BufNewFile,BufRead .login*,.cshrc*  call dist#ft#CSH()

// tmux configuration with arbitrary extension
autocmd BufNewFile,BufRead {.,}tmux*.conf*		call s:StarSetf('tmux')

// Universal Scene Description
autocmd BufNewFile,BufRead *.usda,*.usd		setf usd

// UCI
// UCI files are normally in /etc/config, but that might be mounted over sshfs or similar, so we match more loosely.
// There was some concern[1] that this pattern would match too much, so now we check the file content as well.
// [1]: https://github.com/vim/vim/pull/14385#discussion_r1558878741
autocmd BufNewFile,BufRead */etc/config/*		if dist#ft#Detect_UCI_statements() | call s:StarSetf('uci') | endif

// VHDL
autocmd BufNewFile,BufRead *.vhdl_[0-9]*		call s:StarSetf('vhdl')

// Vim script
autocmd BufNewFile,BufRead *vimrc*			call s:StarSetf('vim')

// Subversion commit file
autocmd BufNewFile,BufRead svn-commit*.tmp		setf svn

// X resources file
autocmd BufNewFile,BufRead Xresources*,*/app-defaults/*,*/Xresources/* call s:StarSetf('xdefaults')

// XFree86 config
autocmd BufNewFile,BufRead XF86Config-4*
	\ let b:xf86conf_xfree86_version = 4 | call s:StarSetf('xf86conf')
autocmd BufNewFile,BufRead XF86Config*
	\ if getline(1) =~ '\<XConfigurator\>'
	\|  let b:xf86conf_xfree86_version = 3
	\|endif
	\|call s:StarSetf('xf86conf')

// XKB
autocmd BufNewFile,BufRead */{,.}xkb/{compat,geometry,keycodes,symbols,types}/*	call s:StarSetf('xkb')

// X11 xmodmap
autocmd BufNewFile,BufRead *xmodmap*			call s:StarSetf('xmodmap')

// Xinetd conf
autocmd BufNewFile,BufRead */etc/xinetd.d/*		call s:StarSetf('xinetd')

// yum conf (close enough to dosini)
autocmd BufNewFile,BufRead */etc/yum.repos.d/*	call s:StarSetf('dosini')

// Yarn lock
autocmd BufNewFile,BufRead yarn.lock			setf yaml

// Zathurarc
autocmd BufNewFile,BufRead zathurarc			setf zathurarc

// Rofi stylesheet
autocmd BufNewFile,BufRead *.rasi			setf rasi

// Z-Shell script ending in a star
autocmd BufNewFile,BufRead .zsh*,.zlog*,.zcompdump*  call s:StarSetf('zsh')
autocmd BufNewFile,BufRead zsh*,zlog*		call s:StarSetf('zsh')

// Zsh module
// mdd: https://github.com/zsh-users/zsh/blob/57248b88830ce56adc243a40c7773fb3825cab34/Etc/zsh-development-guide#L285-L288
// mdh, pro: https://github.com/zsh-users/zsh/blob/57248b88830ce56adc243a40c7773fb3825cab34/Etc/zsh-development-guide#L268-L271
// *.mdd will generate *.mdh, *.pro and *.epro.
// module's *.c will #include *.mdh containing module dependency information and
// *.pro containing all static declarations of *.c
// *.epro contains all external declarations of *.c
autocmd BufNewFile,BufRead *.mdh,*.epro		setf c
autocmd BufNewFile,BufRead *.mdd			setf sh

// Help files match *.txt but should have a last line that is a modeline.
autocmd BufNewFile,BufRead *.txt
	\  if getline('$') !~ 'vim:.*ft=help'
	\|   setf text
	\| endif

// Blueprint markup files
autocmd BufNewFile,BufRead *.blp			setf blueprint

// Blueprint build system file
autocmd BufNewFile,BufRead *.bp			setf bp

// Use the filetype detect plugins. They may overrule any of the previously // detected filetypes.
runtime! ftdetect/*.vim

// NOTE: The above command could have ended the filetypedetect autocmd group
// and started another one. Let's make sure it has ended to get to a consistent state.
augroup END

// Generic configuration file. Use FALLBACK, it's just guessing!
autocmd filetypedetect BufNewFile,BufRead *
	\ if !did_filetype() && expand("<amatch>") !~ g:ft_ignore_pat
	\    && (expand("<amatch>") =~ '\.conf$'
	\	|| getline(1) =~ '^#' || getline(2) =~ '^#'
	\	|| getline(3) =~ '^#' || getline(4) =~ '^#'
	\	|| getline(5) =~ '^#') |
	\   setf FALLBACK conf |
	\ endif

// Function called for testing all functions defined here.  These are
// script-local, thus need to be executed here.
// Returns a string with error messages (hopefully empty).
func TestFiletypeFuncs(testlist)
  let output = ''
  for f in a:testlist
    try
      exe f
    catch
      let output = output .. "\n" .. f .. ": " .. v:exception
    endtry
  endfor
  return output
endfunc

