//{{{settings

// Eegl filetype script
// Language:	C
// Maintainer:	The Eegl Project
// Last Change:	2025 Aug 08
// Former Maintainer:	Bram Moolenaar <Bram@vim.org>

// Only do this when not done yet for this buffer
if exists("b:did_ftplugin")
  finish
endif

// Don't load another plugin for this buffer
let b:did_ftplugin = 1

// Using line continuation here.

let b:undo_ftplugin = "setl fo< com< ofu< cms< def< inc< | if has('vms') | setl isk< | endif"

// Set 'formatoptions' to break comment lines but not other lines,
// and insert the comment leader when hitting <CR> or using "o".
setlocal fo-=t fo+=croql

// These options have the right value as default, but the user may have
// overruled that.
setlocal commentstring=/*\ %s\ */ define& include&

// Set completion with CTRL-X CTRL-O to autoloaded function.
if exists('&ofu') && has("vim9script")
  setlocal ofu=ccomplete#Complete
endif

// Set 'comments' to format dashed lists in comments.
// Also include ///, used for Doxygen.
setlocal comments=sO:*\ -,mO:*\ \ ,exO:*/,s1:/*,mb:*,ex:*/,:///,://


// When the matchit plugin is loaded, this makes the % command skip parens and
// braces in comments properly.
if !exists("b:match_words")
  let b:match_words = '^\s*#\s*if\%(\|def\|ndef\)\>:^\s*#\s*elif\%(\|def\|ndef\)\>:^\s*#\s*else\>:^\s*#\s*endif\>'
  let b:match_skip = 's:comment\|string\|character\|special'
  let b:undo_ftplugin ..= " | unlet! b:match_skip b:match_words"
endif

//}}}
//{{{syntax

// Quit when a (custom) syntax file was already loaded
if exists("b:currentSyntax")
  finish
endif


let s:ft = matchstr(&ft, '^\%([^.]\)\+')

// A bunch of useful C keywords
syn keyword	cKeyword	if for while restrict define typedef goto break return continue asm
syn keyword	cKeyword2	private static else ei struct enum include const constexpr
syn region cComment start="//" skip="\\$" end="$" keepend
// Define the default highlighting.
// Only used when an item doesn't have highlighting yet
hi def link cKeyword		Keyword
hi def link cKeyword2		Keyword
hi def link cComment		Comment

let b:currentSyntax = "c"

unlet s:ft

//}}}
//{{{indent

// Only load this indent file when no other was loaded.
if exists("b:did_indent")
   finish
endif
let b:did_indent = 1

// C indenting is built-in, thus this is very simple
setlocal cindent

let b:undo_indent = "setl cin<"

//}}}
