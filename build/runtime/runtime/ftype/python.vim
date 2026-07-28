//{{{settings

// Vim filetype plugin file
// Language:	python
// Maintainer:	Tom Picton <tom@tompicton.com>
// Previous Maintainer: James Sully <sullyj3@gmail.com>
// Previous Maintainer: Johannes Zellner <johannes@zellner.org>
// Repository: https://github.com/tpict/vim-ftplugin-python
// Last Change: 2024/05/13
//              2024 Nov 30 use pytest compiler (#16130)
// Vim syntax file
// Language:	Python
// Maintainer:	Zvezdan Petkovic <zpetkovic@acm.org>
// Last Change:	2025 Aug 13
// Credits:	Neil Schemenauer <nas@python.ca>
//		Dmitry Vasiliev
//		Rob B
//
//		This version is a major rewrite by Zvezdan Petkovic.
//
//		- introduced highlighting of doctests
//		- updated keywords, built-ins, and exceptions
//		- corrected regular expressions for
//
//		  * functions
//		  * decorators
//		  * strings
//		  * escapes
//		  * numbers
//		  * space error
//
//		- corrected synchronization
//		- more highlighting is ON by default, except
//		- space error highlighting is OFF by default
//

if exists("b:did_ftplugin") | finish | endif
let b:did_ftplugin = 1
let s:keepcpo= &cpo


setlocal cinkeys-=0#
setlocal indentkeys-=0#
setlocal include=^\\s*\\(from\\\|import\\)
setlocal define=^\\s*\\(\\(async\\s\\+\\)\\?def\\\|class\\)

// For imports with leading .., append / and replace additional .s with ../
let b:grandparent_match = '^\(.\.\)\(\.*\)'
let b:grandparent_sub = '\=submatch(1)."/".repeat("../",strlen(submatch(2)))'

// For imports with a single leading ., replace it with ./
let b:parent_match = '^\.\(\.\)\@!'
let b:parent_sub = './'

// Replace any . sandwiched between word characters with /
let b:child_match = '\(\w\)\.\(\w\)'
let b:child_sub = '\1/\2'

setlocal includeexpr=substitute(substitute(substitute(
      \v:fname,
      \b:grandparent_match,b:grandparent_sub,''),
      \b:parent_match,b:parent_sub,''),
      \b:child_match,b:child_sub,'g')

setlocal suffixesadd=.py
setlocal comments=b:#,fb:-
setlocal commentstring=#\ %s

if has('python3')
  setlocal omnifunc=python3complete#Complete
elseif has('python')
  setlocal omnifunc=pythoncomplete#Complete
endif

set wildignore+=*.pyc

let b:next_toplevel='\v%$\|^(class\|def\|async def)>'
let b:prev_toplevel='\v^(class\|def\|async def)>'
let b:next_endtoplevel='\v%$\|\S.*\n+(def\|class)'
let b:prev_endtoplevel='\v\S.*\n+(def\|class)'
let b:next='\v%$\|^\s*(class\|def\|async def)>'
let b:prev='\v^\s*(class\|def\|async def)>'
let b:next_end='\v\S\n*(%$\|^(\s*\n*)*(class\|def\|async def)\|^\S)'
let b:prev_end='\v\S\n*(^(\s*\n*)*(class\|def\|async def)\|^\S)'

if !exists('g:no_plugin_maps') && !exists('g:no_python_maps')
    execute "nnoremap <silent> <buffer> ]] :<C-U>call <SID>Python_jump('n', '". b:next_toplevel."', 'W', v:count1)<cr>"
    execute "nnoremap <silent> <buffer> [[ :<C-U>call <SID>Python_jump('n', '". b:prev_toplevel."', 'Wb', v:count1)<cr>"
    execute "nnoremap <silent> <buffer> ][ :<C-U>call <SID>Python_jump('n', '". b:next_endtoplevel."', 'W', v:count1, 0)<cr>"
    execute "nnoremap <silent> <buffer> [] :<C-U>call <SID>Python_jump('n', '". b:prev_endtoplevel."', 'Wb', v:count1, 0)<cr>"
    execute "nnoremap <silent> <buffer> ]m :<C-U>call <SID>Python_jump('n', '". b:next."', 'W', v:count1)<cr>"
    execute "nnoremap <silent> <buffer> [m :<C-U>call <SID>Python_jump('n', '". b:prev."', 'Wb', v:count1)<cr>"
    execute "nnoremap <silent> <buffer> ]M :<C-U>call <SID>Python_jump('n', '". b:next_end."', 'W', v:count1, 0)<cr>"
    execute "nnoremap <silent> <buffer> [M :<C-U>call <SID>Python_jump('n', '". b:prev_end."', 'Wb', v:count1, 0)<cr>"

    execute "onoremap <silent> <buffer> ]] :call <SID>Python_jump('o', '". b:next_toplevel."', 'W', v:count1)<cr>"
    execute "onoremap <silent> <buffer> [[ :call <SID>Python_jump('o', '". b:prev_toplevel."', 'Wb', v:count1)<cr>"
    execute "onoremap <silent> <buffer> ][ :call <SID>Python_jump('o', '". b:next_endtoplevel."', 'W', v:count1, 0)<cr>"
    execute "onoremap <silent> <buffer> [] :call <SID>Python_jump('o', '". b:prev_endtoplevel."', 'Wb', v:count1, 0)<cr>"
    execute "onoremap <silent> <buffer> ]m :call <SID>Python_jump('o', '". b:next."', 'W', v:count1)<cr>"
    execute "onoremap <silent> <buffer> [m :call <SID>Python_jump('o', '". b:prev."', 'Wb', v:count1)<cr>"
    execute "onoremap <silent> <buffer> ]M :call <SID>Python_jump('o', '". b:next_end."', 'W', v:count1, 0)<cr>"
    execute "onoremap <silent> <buffer> [M :call <SID>Python_jump('o', '". b:prev_end."', 'Wb', v:count1, 0)<cr>"

    execute "xnoremap <silent> <buffer> ]] :call <SID>Python_jump('x', '". b:next_toplevel."', 'W', v:count1)<cr>"
    execute "xnoremap <silent> <buffer> [[ :call <SID>Python_jump('x', '". b:prev_toplevel."', 'Wb', v:count1)<cr>"
    execute "xnoremap <silent> <buffer> ][ :call <SID>Python_jump('x', '". b:next_endtoplevel."', 'W', v:count1, 0)<cr>"
    execute "xnoremap <silent> <buffer> [] :call <SID>Python_jump('x', '". b:prev_endtoplevel."', 'Wb', v:count1, 0)<cr>"
    execute "xnoremap <silent> <buffer> ]m :call <SID>Python_jump('x', '". b:next."', 'W', v:count1)<cr>"
    execute "xnoremap <silent> <buffer> [m :call <SID>Python_jump('x', '". b:prev."', 'Wb', v:count1)<cr>"
    execute "xnoremap <silent> <buffer> ]M :call <SID>Python_jump('x', '". b:next_end."', 'W', v:count1, 0)<cr>"
    execute "xnoremap <silent> <buffer> [M :call <SID>Python_jump('x', '". b:prev_end."', 'Wb', v:count1, 0)<cr>"
endif

if !exists('*<SID>Python_jump')
  fun! <SID>Python_jump(mode, motion, flags, count, ...) range
      let l:startofline = (a:0 >= 1) ? a:1 : 1

      if a:mode == 'x'
          normal! gv
      endif

      if l:startofline == 1
          normal! 0
      endif

      let cnt = a:count
      mark '
      while cnt > 0
          call search(a:motion, a:flags)
          let cnt = cnt - 1
      endwhile

      if l:startofline == 1
          normal! ^
      endif
  endfun
endif

if has("browsefilter") && !exists("b:browsefilter")
    let b:browsefilter = "Python Files (*.py)\t*.py\n"
    if has("win32")
	let b:browsefilter .= "All Files (*.*)\t*\n"
    else
	let b:browsefilter .= "All Files (*)\t*\n"
    endif
endif

if !exists("g:python_recommended_style") || g:python_recommended_style != 0
    " As suggested by PEP8.
    setlocal expandtab tabstop=4 softtabstop=4 shiftwidth=4
endif

// Use pydoc for keywordprg.
// Unix users preferentially get pydoc3, then pydoc2.
// Windows doesn't have a standalone pydoc executable in $PATH by default, nor
// does it have separate python2/3 executables, so Windows users just get
// whichever version corresponds to their installed Python version.
if executable('python3')
  setlocal keywordprg=python3\ -m\ pydoc
elseif executable('python')
  setlocal keywordprg=python\ -m\ pydoc
endif

if expand('%:t') =~# '\v^test_.*\.py$|_test\.py$' && executable('pytest')
  compiler pytest
  let &l:makeprg .= ' %:S'
endif

// Script for filetype switching to undo the local stuff we may have changed
let b:undo_ftplugin = 'setlocal cinkeys<'
      \ . '|setlocal comments<'
      \ . '|setlocal commentstring<'
      \ . '|setlocal expandtab<'
      \ . '|setlocal include<'
      \ . '|setlocal includeexpr<'
      \ . '|setlocal indentkeys<'
      \ . '|setlocal keywordprg<'
      \ . '|setlocal omnifunc<'
      \ . '|setlocal shiftwidth<'
      \ . '|setlocal softtabstop<'
      \ . '|setlocal suffixesadd<'
      \ . '|setlocal tabstop<'
      \ . '|setlocal makeprg<'
      \ . '|silent! nunmap <buffer> [M'
      \ . '|silent! nunmap <buffer> [['
      \ . '|silent! nunmap <buffer> []'
      \ . '|silent! nunmap <buffer> [m'
      \ . '|silent! nunmap <buffer> ]M'
      \ . '|silent! nunmap <buffer> ]['
      \ . '|silent! nunmap <buffer> ]]'
      \ . '|silent! nunmap <buffer> ]m'
      \ . '|silent! ounmap <buffer> [M'
      \ . '|silent! ounmap <buffer> [['
      \ . '|silent! ounmap <buffer> []'
      \ . '|silent! ounmap <buffer> [m'
      \ . '|silent! ounmap <buffer> ]M'
      \ . '|silent! ounmap <buffer> ]['
      \ . '|silent! ounmap <buffer> ]]'
      \ . '|silent! ounmap <buffer> ]m'
      \ . '|silent! xunmap <buffer> [M'
      \ . '|silent! xunmap <buffer> [['
      \ . '|silent! xunmap <buffer> []'
      \ . '|silent! xunmap <buffer> [m'
      \ . '|silent! xunmap <buffer> ]M'
      \ . '|silent! xunmap <buffer> ]['
      \ . '|silent! xunmap <buffer> ]]'
      \ . '|silent! xunmap <buffer> ]m'
      \ . '|unlet! b:browsefilter'
      \ . '|unlet! b:child_match'
      \ . '|unlet! b:child_sub'
      \ . '|unlet! b:grandparent_match'
      \ . '|unlet! b:grandparent_sub'
      \ . '|unlet! b:next'
      \ . '|unlet! b:next_end'
      \ . '|unlet! b:next_endtoplevel'
      \ . '|unlet! b:next_toplevel'
      \ . '|unlet! b:parent_match'
      \ . '|unlet! b:parent_sub'
      \ . '|unlet! b:prev'
      \ . '|unlet! b:prev_end'
      \ . '|unlet! b:prev_endtoplevel'
      \ . '|unlet! b:prev_toplevel'
      \ . '|unlet! b:undo_ftplugin'

let &cpo = s:keepcpo
unlet s:keepcpo

//}}}
//{{{indent

// Only load this indent file when no other was loaded.
if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

// Some preliminary settings
setlocal nolisp		// Make sure lisp indenting doesn't supersede us
setlocal autoindent	// indentexpr isn't much help otherwise

setlocal indentexpr=python#GetIndent(v:lnum)
setlocal indentkeys+=<:>,=elif,=except

let b:undo_indent = "setl ai< inde< indk< lisp<"

// Only define the function once.
if exists("*GetPythonIndent")
  finish
endif

// Keep this for backward compatibility, new scripts should use
// python#GetIndent()
function GetPythonIndent(lnum)
  return python#GetIndent(a:lnum)
endfunction

//}}}
//{{{syntax

// Optional highlighting can be controlled using these variables.
//
//   let python_no_builtin_highlight = 1
//   let python_no_doctest_code_highlight = 1
//   let python_no_doctest_highlight = 1
//   let python_no_exception_highlight = 1
//   let python_no_number_highlight = 1
//   let python_space_error_highlight = 1
//
// All the options above can be switched on together.
//
//   let python_highlight_all = 1
//
// The use of Python 2 compatible syntax highlighting can be enforced.
// The straddling code (Python 2 and 3 compatible), up to Python 3.5,
// will be also supported.
//
//   let python_use_python2_syntax = 1
//
// This option will exclude all modern Python 3.6 or higher features.
//

// quit when a syntax file was already loaded.
if exists("b:currentSyntax")
  finish
endif

// Use of Python 2 and 3.5 or lower requested.
if exists("python_use_python2_syntax")
  runtime! syntax/python2.vim
  finish
endif

// We need nocompatible mode in order to continue lines with backslashes.
// Original setting will be restored.



if exists("python_no_doctest_highlight")
  let python_no_doctest_code_highlight = 1
endif

if exists("python_highlight_all")
  if exists("python_no_builtin_highlight")
    unlet python_no_builtin_highlight
  endif
  if exists("python_no_doctest_code_highlight")
    unlet python_no_doctest_code_highlight
  endif
  if exists("python_no_doctest_highlight")
    unlet python_no_doctest_highlight
  endif
  if exists("python_no_exception_highlight")
    unlet python_no_exception_highlight
  endif
  if exists("python_no_number_highlight")
    unlet python_no_number_highlight
  endif
  let python_space_error_highlight = 1
endif

// Keep Python keywords in alphabetical order inside groups for easy
// comparison with the table in the 'Python Language Reference'
// https://docs.python.org/reference/lexical_analysis.html#keywords.
// Groups are in the order presented in NAMING CONVENTIONS in syntax.txt.
// Exceptions come last at the end of each group (class and def below).
//
// The list can be checked using:
//
// python3 -c 'import keyword, pprint; pprint.pprint(keyword.kwlist + keyword.softkwlist, compact=True)'
//
syn keyword pythonStatement	False None True
syn keyword pythonStatement	as assert break continue del global
syn keyword pythonStatement	lambda nonlocal pass return with yield
syn keyword pythonStatement	class nextgroup=pythonClass skipwhite
syn keyword pythonStatement	def nextgroup=pythonFunction skipwhite
syn keyword pythonConditional	elif else if
syn keyword pythonRepeat	for while
syn keyword pythonOperator	and in is not or
syn keyword pythonException	except finally raise try
syn keyword pythonInclude	from import
syn keyword pythonAsync		async await

// Soft keywords
// These keywords do not mean anything unless used in the right context.
// See https://docs.python.org/3/reference/lexical_analysis.html#soft-keywords
// for more on this.
syn match   pythonConditional   "^\s*\zscase\%(\s\+.*:.*$\)\@="
syn match   pythonConditional   "^\s*\zsmatch\%(\s\+.*:\s*\%(#.*\)\=$\)\@="

// These names are special by convention. While they aren't real keywords,
// giving them distinct highlighting provides a nice visual cue.
syn keyword pythonClassVar	self cls

// Decorators
// A dot must be allowed because of @MyClass.myfunc decorators.
syn match   pythonDecorator	"@" display contained
syn match   pythonDecoratorName	"@\s*\h\%(\w\|\.\)*" display contains=pythonDecorator

// Python 3.5 introduced the use of the same symbol for matrix multiplication:
// https://www.python.org/dev/peps/pep-0465/.  We now have to exclude the
// symbol from highlighting when used in that context.
// Single line multiplication.
syn match   pythonMatrixMultiply
      \ "\%(\w\|[])]\)\s*@"
      \ contains=ALLBUT,pythonDecoratorName,pythonDecorator,pythonClass,pythonFunction,pythonDoctestValue
      \ transparent
// Multiplication continued on the next line after backslash.
syn match   pythonMatrixMultiply
      \ "[^\\]\\\s*\n\%(\s*\.\.\.\s\)\=\s\+@"
      \ contains=ALLBUT,pythonDecoratorName,pythonDecorator,pythonClass,pythonFunction,pythonDoctestValue
      \ transparent
// Multiplication in a parenthesized expression over multiple lines with @ at
// the start of each continued line; very similar to decorators and complex.
syn match   pythonMatrixMultiply
      \ "^\s*\%(\%(>>>\|\.\.\.\)\s\+\)\=\zs\%(\h\|\%(\h\|[[(]\).\{-}\%(\w\|[])]\)\)\s*\n\%(\s*\.\.\.\s\)\=\s\+@\%(.\{-}\n\%(\s*\.\.\.\s\)\=\s\+@\)*"
      \ contains=ALLBUT,pythonDecoratorName,pythonDecorator,pythonClass,pythonFunction,pythonDoctestValue
      \ transparent

syn match   pythonClass		"\h\w*" display contained
syn match   pythonFunction	"\h\w*" display contained

syn match   pythonComment	"#.*$" contains=pythonTodo,@Spell
syn keyword pythonTodo		FIXME NOTE NOTES TODO XXX contained

// Triple-quoted strings can contain doctests.
syn region  pythonString matchgroup=pythonQuotes
      \ start=+[uU]\=\z(['"]\)+ end="\z1" skip="\\\\\|\\\z1"
      \ contains=pythonEscape,pythonUnicodeEscape,@Spell
syn region  pythonString matchgroup=pythonTripleQuotes
      \ start=+[uU]\=\z('''\|"""\)+ end="\z1" keepend
      \ contains=pythonEscape,pythonUnicodeEscape,pythonSpaceError,pythonDoctest,@Spell
syn region  pythonRawString matchgroup=pythonQuotes
      \ start=+[rR]\z(['"]\)+ end="\z1" skip="\\\\\|\\\z1"
      \ contains=@Spell
syn region  pythonRawString matchgroup=pythonTripleQuotes
      \ start=+[rR]\z('''\|"""\)+ end="\z1" keepend
      \ contains=pythonSpaceError,pythonDoctest,@Spell

// Formatted string literals (f-strings)
// https://docs.python.org/3/reference/lexical_analysis.html#f-strings
syn region  pythonFString
      \ matchgroup=pythonQuotes
      \ start=+\cF\z(['"]\)+
      \ end="\z1"
      \ skip="\\\\\|\\\z1"
      \ contains=pythonFStringField,pythonFStringSkip,pythonEscape,pythonUnicodeEscape,@Spell
syn region  pythonFString
      \ matchgroup=pythonTripleQuotes
      \ start=+\cF\z('''\|"""\)+
      \ end="\z1"
      \ keepend
      \ contains=pythonFStringField,pythonFStringSkip,pythonEscape,pythonUnicodeEscape,pythonSpaceError,pythonDoctest,@Spell
syn region  pythonRawFString
      \ matchgroup=pythonQuotes
      \ start=+\c\%(FR\|RF\)\z(['"]\)+
      \ end="\z1"
      \ skip="\\\\\|\\\z1"
      \ contains=pythonFStringField,pythonFStringSkip,@Spell
syn region  pythonRawFString
      \ matchgroup=pythonTripleQuotes
      \ start=+\c\%(FR\|RF\)\z('''\|"""\)+
      \ end="\z1"
      \ keepend
      \ contains=pythonFStringField,pythonFStringSkip,pythonSpaceError,pythonDoctest,@Spell

// Bytes
syn region  pythonBytes
      \ matchgroup=pythonQuotes
      \ start=+\cB\z(['"]\)+
      \ end="\z1"
      \ skip="\\\\\|\\\z1"
      \ contains=pythonEscape
syn region  pythonBytes
      \ matchgroup=pythonTripleQuotes
      \ start=+\cB\z('''\|"""\)+
      \ end="\z1"
      \ keepend
      \ contains=pythonEscape
syn region  pythonRawBytes
      \ matchgroup=pythonQuotes
      \ start=+\c\%(BR\|RB\)\z(['"]\)+
      \ end="\z1"
      \ skip="\\\\\|\\\z1"
syn region  pythonRawBytes
      \ matchgroup=pythonTripleQuotes
      \ start=+\c\%(BR\|RB\)\z('''\|"""\)+
      \ end="\z1"
      \ keepend

// F-string replacement fields
//
// - Matched parentheses, brackets and braces are ignored
// - A bare # is ignored to end of line
// - A bare = (surrounded by optional whitespace) enables debugging
// - A bare ! prefixes a conversion field
// - A bare : begins a format specification
//     - Matched braces inside a format specification are ignored
//
syn region  pythonFStringField
    \ matchgroup=pythonFStringDelimiter
    \ start=/{/
    \ skip=/([^)]*)\|\[[^]]*]\|{[^}]*}\|#.*$/
    \ end=/\%(\s*=\s*\)\=\%(!\a\)\=\%(:\%({[^}]*}\|[^}]*\)\+\)\=}/
    \ contained
// Doubled braces and Unicode escapes are not replacement fields
syn match   pythonFStringSkip	/{{\|\\N{/ transparent contained contains=NONE

syn match   pythonEscape	+\\[abfnrtv'"\\]+ contained
syn match   pythonEscape	"\\\o\{1,3}" contained
syn match   pythonEscape	"\\x\x\{2}" contained
syn match   pythonUnicodeEscape	"\%(\\u\x\{4}\|\\U\x\{8}\)" contained
// Python allows case-insensitive Unicode IDs: http://www.unicode.org/charts/
// The specification: https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-4/#G135165
syn match   pythonUnicodeEscape	"\\N{\a\+\%(\%(\s\a\+[[:alnum:]]*\)\|\%(-[[:alnum:]]\+\)\)*}" contained
syn match   pythonEscape	"\\$"

// It is very important to understand all details before changing the
// regular expressions below or their order.
// The word boundaries are *not* the floating-point number boundaries
// because of a possible leading or trailing decimal point.
// The expressions below ensure that all valid number literals are
// highlighted, and invalid number literals are not.  For example,
//
// - a decimal point in '4.' at the end of a line is highlighted,
// - a second dot in 1.0.0 is not highlighted,
// - 08 is not highlighted,
// - 08e0 or 08j are highlighted,
//
// and so on, as specified in the 'Python Language Reference'.
// https://docs.python.org/reference/lexical_analysis.html#numeric-literals
if !exists("python_no_number_highlight")
  // numbers (including complex)
  syn match   pythonNumber	"\<0[oO]\%(_\=\o\)\+\>"
  syn match   pythonNumber	"\<0[xX]\%(_\=\x\)\+\>"
  syn match   pythonNumber	"\<0[bB]\%(_\=[01]\)\+\>"
  syn match   pythonNumber	"\<\%([1-9]\%(_\=\d\)*\|0\+\%(_\=0\)*\)\>"
  syn match   pythonNumber	"\<\d\%(_\=\d\)*[jJ]\>"
  syn match   pythonNumber	"\<\d\%(_\=\d\)*[eE][+-]\=\d\%(_\=\d\)*[jJ]\=\>"
  syn match   pythonNumber
        \ "\<\d\%(_\=\d\)*\.\%([eE][+-]\=\d\%(_\=\d\)*\)\=[jJ]\=\%(\W\|$\)\@="
  syn match   pythonNumber
        \ "\%(^\|\W\)\zs\%(\d\%(_\=\d\)*\)\=\.\d\%(_\=\d\)*\%([eE][+-]\=\d\%(_\=\d\)*\)\=[jJ]\=\>"
endif

// Group the built-ins in the order in the 'Python Library Reference' for
// easier comparison.
// https://docs.python.org/library/constants.html
// http://docs.python.org/library/functions.html
// Python built-in functions are in alphabetical order.
//
// The list can be checked using:
//
// python3 -c 'import builtins, pprint; pprint.pprint(dir(builtins), compact=True)'
//
// The constants added by the `site` module are not listed below because they
// should not be used in programs, only in interactive interpreter.
// Similarly for some other attributes and functions `__`-enclosed from the
// output of the above command.
//
if !exists("python_no_builtin_highlight")
  // built-in constants
  // 'False', 'True', and 'None' are also reserved words in Python 3
  syn keyword pythonBuiltin	False True None
  syn keyword pythonBuiltin	NotImplemented Ellipsis __debug__
  // constants added by the `site` module
  syn keyword pythonBuiltin	quit exit copyright credits license
  // built-in functions
  syn keyword pythonBuiltin	abs all any ascii bin bool breakpoint bytearray
  syn keyword pythonBuiltin	bytes callable chr classmethod compile complex
  syn keyword pythonBuiltin	delattr dict dir divmod enumerate eval exec
  syn keyword pythonBuiltin	filter float format frozenset getattr globals
  syn keyword pythonBuiltin	hasattr hash help hex id input int isinstance
  syn keyword pythonBuiltin	issubclass iter len list locals map max
  syn keyword pythonBuiltin	memoryview min next object oct open ord pow
  syn keyword pythonBuiltin	print property range repr reversed round set
  syn keyword pythonBuiltin	setattr slice sorted staticmethod str sum super
  syn keyword pythonBuiltin	tuple type vars zip __import__
  // avoid highlighting attributes as builtins
  syn match   pythonAttribute	/\.\h\w*/hs=s+1
	\ contains=ALLBUT,pythonBuiltin,pythonClass,pythonFunction,pythonAsync
	\ transparent
endif

// From the 'Python Library Reference' class hierarchy at the bottom.
// http://docs.python.org/library/exceptions.html
if !exists("python_no_exception_highlight")
  // builtin base exceptions (used mostly as base classes for other exceptions)
  syn keyword pythonExceptions	BaseException Exception
  syn keyword pythonExceptions	ArithmeticError BufferError LookupError
  // builtin exceptions (actually raised)
  syn keyword pythonExceptions	AssertionError AttributeError EOFError
  syn keyword pythonExceptions	FloatingPointError GeneratorExit ImportError
  syn keyword pythonExceptions	IndentationError IndexError KeyError
  syn keyword pythonExceptions	KeyboardInterrupt MemoryError
  syn keyword pythonExceptions	ModuleNotFoundError NameError
  syn keyword pythonExceptions	NotImplementedError OSError OverflowError
  syn keyword pythonExceptions	RecursionError ReferenceError RuntimeError
  syn keyword pythonExceptions	StopAsyncIteration StopIteration SyntaxError
  syn keyword pythonExceptions	SystemError SystemExit TabError TypeError
  syn keyword pythonExceptions	UnboundLocalError UnicodeDecodeError
  syn keyword pythonExceptions	UnicodeEncodeError UnicodeError
  syn keyword pythonExceptions	UnicodeTranslateError ValueError
  syn keyword pythonExceptions	ZeroDivisionError
  // builtin exception aliases for OSError
  syn keyword pythonExceptions	EnvironmentError IOError WindowsError
  // builtin OS exceptions in Python 3
  syn keyword pythonExceptions	BlockingIOError BrokenPipeError
  syn keyword pythonExceptions	ChildProcessError ConnectionAbortedError
  syn keyword pythonExceptions	ConnectionError ConnectionRefusedError
  syn keyword pythonExceptions	ConnectionResetError FileExistsError
  syn keyword pythonExceptions	FileNotFoundError InterruptedError
  syn keyword pythonExceptions	IsADirectoryError NotADirectoryError
  syn keyword pythonExceptions	PermissionError ProcessLookupError TimeoutError
  // builtin warnings
  syn keyword pythonExceptions	BytesWarning DeprecationWarning FutureWarning
  syn keyword pythonExceptions	ImportWarning PendingDeprecationWarning
  syn keyword pythonExceptions	ResourceWarning RuntimeWarning
  syn keyword pythonExceptions	SyntaxWarning UnicodeWarning
  syn keyword pythonExceptions	UserWarning Warning
endif

if exists("python_space_error_highlight")
  // trailing whitespace
  syn match   pythonSpaceError	display excludenl "\s\+$"
  // mixed tabs and spaces
  syn match   pythonSpaceError	display " \+\t"
  syn match   pythonSpaceError	display "\t\+ "
endif

// Do not spell doctests inside strings.
// Notice that the end of a string, either ''', or """, will end the contained
// doctest too.  Thus, we do *not* need to have it as an end pattern.
if !exists("python_no_doctest_highlight")
  if !exists("python_no_doctest_code_highlight")
    syn region pythonDoctest
	  \ start="^\s*>>>\s" end="^\s*$"
	  \ contained contains=ALLBUT,pythonDoctest,pythonClass,pythonFunction,@Spell
    syn region pythonDoctestValue
	  \ start=+^\s*\%(>>>\s\|\.\.\.\s\|"""\|'''\)\@!\S\++ end="$"
	  \ contained
  else
    syn region pythonDoctest
	  \ start="^\s*>>>" end="^\s*$"
	  \ contained contains=@NoSpell
  endif
endif

// Sync at the beginning of (async) function or class definitions.
syn sync match pythonSync grouphere NONE "^\%(async\s\+def\|def\|class\)\s\+\h\w*\s*[(:]"

// The default highlight links.  Can be overridden later.
hi def link pythonStatement		Statement
hi def link pythonConditional		Conditional
hi def link pythonRepeat		Repeat
hi def link pythonOperator		Operator
hi def link pythonException		Exception
hi def link pythonInclude		Include
hi def link pythonAsync			Statement
hi def link pythonClassVar		Identifier
hi def link pythonDecorator		Define
hi def link pythonDecoratorName		Function
hi def link pythonClass			Structure
hi def link pythonFunction		Function
hi def link pythonComment		Comment
hi def link pythonTodo			Todo
hi def link pythonString		String
hi def link pythonRawString		String
hi def link pythonFString		String
hi def link pythonRawFString		String
hi def link pythonBytes 		String
hi def link pythonRawBytes 		String
hi def link pythonQuotes		String
hi def link pythonTripleQuotes		pythonQuotes
hi def link pythonEscape		Special
hi def link pythonUnicodeEscape		pythonEscape
hi def link pythonFStringDelimiter	Special
if !exists("python_no_number_highlight")
  hi def link pythonNumber		Number
endif
if !exists("python_no_builtin_highlight")
  hi def link pythonBuiltin		Function
endif
if !exists("python_no_exception_highlight")
  hi def link pythonExceptions		Structure
endif
if exists("python_space_error_highlight")
  hi def link pythonSpaceError		Error
endif
if !exists("python_no_doctest_highlight")
  hi def link pythonDoctest		Special
  hi def link pythonDoctestValue	Define
endif

let b:currentSyntax = "python"

//}}}
