//{{{settings

// Vim filetype plugin file
// Language:		Java
// Maintainer:		Aliaksei Budavei <0x000c70 AT gmail DOT com>
// Former Maintainer:	Dan Sharp
// Repository:		https://github.com/zzzyxwvut/java-vim.git
// Last Change:		2025 May 08

// Make sure the continuation lines below do not cause problems in
// compatibility mode.

set cpo-=C

if (exists("g:java_ignore_javadoc") || exists("g:java_ignore_markdown")) &&
	\ exists("*javaformat#RemoveCommonMarkdownWhitespace")
    delfunction javaformat#RemoveCommonMarkdownWhitespace
    unlet! g:loaded_javaformat
endif

if exists("b:did_ftplugin")
    let &cpo = s:save_cpo
    unlet s:save_cpo
    finish
endif

let b:did_ftplugin = 1

// For filename completion, prefer the .java extension over the .class
// extension.
set suffixes+=.class

// Set up "&define" and "&include".
let s:peek = ''

try
    " Since v7.3.1037.
    if 'ab' !~ 'a\@1<!b'
	let s:peek = string(strlen('instanceof') + 8)
    endif
catch /\<E59:/
endtry

// Treat "s:common" as a non-backtracking unit to avoid matching constructor
// declarations whose package-private headers are indistinguishable from method
// invocation.  Note that "[@-]" must not and "$" may not be in "&l:iskeyword".
let s:common = '\%(\%(\%(@\%(interface\)\@!\%(\K\k*\.\)*\K\k*\)\s\+\)*' .
	\ '\%(p\%(rivate\|rotected\|ublic\)\s\+\)\=\)\@>'
let s:types = '\%(\%(abstract\|final\|non-sealed\|s\%(ealed\|tatic\|trictfp\)\)\s\+\)*' .
	\ '\%(class\|enum\|@\=interface\|record\)\s\+\ze\K\k*\>'
let s:methods = '\%(\%(abstract\|default\|final\|native\|s\%(tatic\|trictfp\|ynchronized\)\)\s\+\)*' .
	\ '\%(<.\{-1,}>\s\+\)\=\%(\K\k*\.\)*\K\k*\s*\%(<.\{-1,}>\%(\s\|\[\)\@=\)\=\s*\%(\[\]\s*\)*' .
	\ '\s\+\ze\%(\<\%(assert\|case\|instanceof\|new\|return\|throw\|when\)\s\+\)\@' .
	\ s:peek . '<!\K\k*\s*('
let &l:define = printf('\C\m^\s*%s\%%(%s\|%s\)', s:common, s:types, s:methods)
let &l:include = '\C\m^\s*import\s\+\ze\%(\K\k*\.\)\+\K\k*;'
unlet s:methods s:types s:common s:peek

// Enable gf on import statements.  Convert . in the package
// name to / and append .java to the name, then search the path.
setlocal includeexpr=substitute(v:fname,'\\.','/','g')
setlocal suffixesadd=.java

// Clean up in case this file is sourced again.
unlet! s:zip_func_upgradable

//""" STRIVE TO REMAIN COMPATIBLE FOR AT LEAST VIM 7.0.

// Documented in ":help ft-java-plugin".
if exists("g:ftplugin_java_source_path") &&
		\ type(g:ftplugin_java_source_path) == type("")
    if filereadable(g:ftplugin_java_source_path)
	if exists("#zip") &&
		    \ g:ftplugin_java_source_path =~# '.\.\%(jar\|zip\)$'
	    if !exists("s:zip_files")
		let s:zip_files = {}
	    endif

	    let s:zip_files[bufnr('%')] = g:ftplugin_java_source_path
	    let s:zip_files[0] = g:ftplugin_java_source_path
	    let s:zip_func_upgradable = 1

	    function! JavaFileTypeZipFile() abort
		let @/ = substitute(v:fname, '\.', '\\/', 'g') . '.java'
		return get(s:zip_files, bufnr('%'), s:zip_files[0])
	    endfunction

	    " E120 for "inex=s:JavaFileTypeZipFile()" before v8.2.3900.
	    setlocal includeexpr=JavaFileTypeZipFile()
	    setlocal suffixesadd<
	endif
    else
	let &l:path = g:ftplugin_java_source_path . ',' . &l:path
    endif
endif

// Set 'formatoptions' to break comment lines but not other lines,
// and insert the comment leader when hitting <CR> or using "o".
setlocal formatoptions-=t formatoptions+=croql

// Set 'comments' to format Markdown Javadoc comments and dashed lists
// in other multi-line comments (it behaves just like C).
setlocal comments& comments^=:///,sO:*\ -,mO:*\ \ ,exO:*/

setlocal commentstring=//\ %s

// Change the :browse e filter to primarily show Java-related files.
if (has("gui_win32") || has("gui_gtk")) && !exists("b:browsefilter")
    let  b:browsefilter="Java Files (*.java)\t*.java\n" .
		\	"Properties Files (*.prop*)\t*.prop*\n" .
		\	"Manifest Files (*.mf)\t*.mf\n"
    if has("win32")
	let b:browsefilter .= "All Files (*.*)\t*\n"
    else
	let b:browsefilter .= "All Files (*)\t*\n"
    endif
endif

//""" Support pre- and post-compiler actions for SpotBugs.
if (!empty(get(g:, 'spotbugs_properties', {})) ||
	\ !empty(get(b:, 'spotbugs_properties', {}))) &&
	\ filereadable($VIMRUNTIME . '/compiler/spotbugs.vim')

    function! s:SpotBugsGetProperty(name, default) abort
	return get(
	    \ {s:spotbugs_properties_scope}spotbugs_properties,
	    \ a:name,
	    \ a:default)
    endfunction

    function! s:SpotBugsHasProperty(name) abort
	return has_key(
	    \ {s:spotbugs_properties_scope}spotbugs_properties,
	    \ a:name)
    endfunction

    function! s:SpotBugsGetProperties() abort
	return {s:spotbugs_properties_scope}spotbugs_properties
    endfunction

    " Work around ":bar"s and ":autocmd"s.
    function! JavaFileTypeExecuteActionOnce(cleanup_cmd, action_cmd) abort
	try
	    execute a:cleanup_cmd
	finally
	    execute a:action_cmd
	endtry
    endfunction

    if exists("b:spotbugs_properties")
	let s:spotbugs_properties_scope = 'b:'

	" Merge global entries, if any, in buffer-local entries, favouring
	" defined buffer-local ones.
	call extend(
	    \ b:spotbugs_properties,
	    \ get(g:, 'spotbugs_properties', {}),
	    \ 'keep')
    elseif exists("g:spotbugs_properties")
	let s:spotbugs_properties_scope = 'g:'
    endif

    let s:commands = {}

    for s:name in ['DefaultPreCompilerCommand',
	    \ 'DefaultPreCompilerTestCommand',
	    \ 'DefaultPostCompilerCommand']
	if s:SpotBugsHasProperty(s:name)
	    let s:commands[s:name] = remove(
		\ s:SpotBugsGetProperties(),
		\ s:name)
	endif
    endfor

    if s:SpotBugsHasProperty('compiler')
	" XXX: Postpone loading the script until all state, if any, has been
	" collected.
	if !empty(s:commands)
	    let g:spotbugs#state = {
		\ 'compiler': remove(s:SpotBugsGetProperties(), 'compiler'),
		\ 'commands': copy(s:commands),
	    \ }
	else
	    let g:spotbugs#state = {
		\ 'compiler': remove(s:SpotBugsGetProperties(), 'compiler'),
	    \ }
	endif

	" Merge default entries in global (or buffer-local) entries, favouring
	" defined global (or buffer-local) ones.
	call extend(
	    \ {s:spotbugs_properties_scope}spotbugs_properties,
	    \ spotbugs#DefaultProperties(),
	    \ 'keep')
    elseif !empty(s:commands)
	" XXX: Postpone loading the script until all state, if any, has been
	" collected.
	let g:spotbugs#state = {'commands': copy(s:commands)}
    endif

    unlet s:commands s:name
    let s:request = 0

    if s:SpotBugsHasProperty('PostCompilerAction')
	let s:request += 4
    endif

    if s:SpotBugsHasProperty('PreCompilerTestAction')
	let s:dispatcher = printf('call call(%s, [])',
	    \ string(s:SpotBugsGetProperties().PreCompilerTestAction))
	let s:request += 2
    endif

    if s:SpotBugsHasProperty('PreCompilerAction')
	let s:dispatcher = printf('call call(%s, [])',
	    \ string(s:SpotBugsGetProperties().PreCompilerAction))
	let s:request += 1
    endif

    " Adapt the tests for "s:FindClassFiles()" from "compiler/spotbugs.vim".
    if (s:request == 3 || s:request == 7) &&
	    \ (!empty(s:SpotBugsGetProperty('sourceDirPath', [])) &&
		\ !empty(s:SpotBugsGetProperty('classDirPath', [])) &&
	    \ !empty(s:SpotBugsGetProperty('testSourceDirPath', [])) &&
		\ !empty(s:SpotBugsGetProperty('testClassDirPath', [])))
	function! s:DispatchAction(paths_action_pairs) abort
	    let name = expand('%:p')

	    for [paths, Action] in a:paths_action_pairs
		for path in paths
		    if name =~# (path . '.\{-}\.java\=$')
			call Action()
			return
		    endif
		endfor
	    endfor
	endfunction

	let s:dir_cnt = min([
	    \ len(s:SpotBugsGetProperties().sourceDirPath),
	    \ len(s:SpotBugsGetProperties().classDirPath)])
	let s:test_dir_cnt = min([
	    \ len(s:SpotBugsGetProperties().testSourceDirPath),
	    \ len(s:SpotBugsGetProperties().testClassDirPath)])

	" Do not break up path pairs with filtering!
	let s:dispatcher = printf('call s:DispatchAction(%s)',
	    \ string([[s:SpotBugsGetProperties().sourceDirPath[0 : s:dir_cnt - 1],
			\ s:SpotBugsGetProperties().PreCompilerAction],
		\ [s:SpotBugsGetProperties().testSourceDirPath[0 : s:test_dir_cnt - 1],
			\ s:SpotBugsGetProperties().PreCompilerTestAction]]))
	unlet s:test_dir_cnt s:dir_cnt
    endif

    if exists("s:dispatcher")
	function! s:ExecuteActions(pre_action, post_action) abort
	    try
		execute a:pre_action
	    catch /\<E42:/
		execute a:post_action
	    endtry
	endfunction
    endif

    if s:request
	if exists("b:spotbugs_syntax_once") || empty(join(getline(1, 8), ''))
	    let s:actions = [{'event': 'User'}]
	else
	    " XXX: Handle multiple FileType events when vimrc contains more
	    " than one filetype setting for the language, e.g.:
	    "	:filetype plugin indent on
	    "	:autocmd BufRead,BufNewFile *.java setlocal filetype=java ...
	    " XXX: DO NOT ADD b:spotbugs_syntax_once TO b:undo_ftplugin !
	    let b:spotbugs_syntax_once = 1
	    let s:actions = [{
		    \ 'event': 'Syntax',
		    \ 'once': 1,
		\ }, {
		    \ 'event': 'User',
		\ }]
	endif

	for s:idx in range(len(s:actions))
	    if s:request == 7 || s:request == 6 || s:request == 5
		let s:actions[s:idx].cmd = printf('call s:ExecuteActions(%s, %s)',
		    \ string(s:dispatcher),
		    \ string(printf('compiler spotbugs | call call(%s, [])',
			\ string(s:SpotBugsGetProperties().PostCompilerAction))))
	    elseif s:request == 4
		let s:actions[s:idx].cmd = printf(
		    \ 'compiler spotbugs | call call(%s, [])',
		    \ string(s:SpotBugsGetProperties().PostCompilerAction))
	    elseif s:request == 3 || s:request == 2 || s:request == 1
		let s:actions[s:idx].cmd = printf('call s:ExecuteActions(%s, %s)',
		    \ string(s:dispatcher),
		    \ string('compiler spotbugs'))
	    else
		let s:actions[s:idx].cmd = ''
	    endif
	endfor

	if !exists("#java_spotbugs")
	    augroup java_spotbugs
	    augroup END
	endif

	" The events are defined in s:actions.
	silent! autocmd! java_spotbugs User <buffer>
	silent! autocmd! java_spotbugs Syntax <buffer>

	for s:action in s:actions
	    if has_key(s:action, 'once')
		execute printf('autocmd java_spotbugs %s <buffer> ' .
			\ 'call JavaFileTypeExecuteActionOnce(%s, %s)',
		    \ s:action.event,
		    \ string(printf('autocmd! java_spotbugs %s <buffer>',
			\ s:action.event)),
		    \ string(s:action.cmd))
	    else
		execute printf('autocmd java_spotbugs %s <buffer> %s',
		    \ s:action.event,
		    \ s:action.cmd)
	    endif
	endfor

	if s:SpotBugsHasProperty('PostCompilerActionExecutor') &&
		\ (s:request == 7 || s:request == 6 ||
		\ s:request == 5 || s:request == 4)
	    let s:augroup = s:SpotBugsGetProperty(
		\ 'augroupForPostCompilerAction',
		\ 'java_spotbugs_post')
	    let s:augroup = !empty(s:augroup) ? s:augroup : 'java_spotbugs_post'

	    for s:candidate in ['java_spotbugs_post', s:augroup]
		if !exists("#" . s:candidate)
		    execute printf('augroup %s | augroup END', s:candidate)
		endif
	    endfor

	    silent! autocmd! java_spotbugs_post User <buffer>

	    " Define a User ":autocmd" to define a once-only ShellCmdPost
	    " ":autocmd" that will invoke "PostCompilerActionExecutor" and let
	    " it decide whether to proceed with ":compiler spotbugs" etc.; and
	    " seek explicit synchronisation with ":doautocmd ShellCmdPost" by
	    " omitting "nested" for "java_spotbugs_post" and "java_spotbugs".
	    execute printf('autocmd java_spotbugs_post User <buffer> ' .
				\ 'call JavaFileTypeExecuteActionOnce(%s, %s)',
		\ string(printf('autocmd! %s ShellCmdPost <buffer>', s:augroup)),
		\ string(printf('autocmd %s ShellCmdPost <buffer> ' .
				\ 'call JavaFileTypeExecuteActionOnce(%s, %s)',
		    \ s:augroup,
		    \ string(printf('autocmd! %s ShellCmdPost <buffer>', s:augroup)),
		    \ string(printf('call call(%s, [%s])',
			\ string(s:SpotBugsGetProperties().PostCompilerActionExecutor),
			\ string(printf('compiler spotbugs | call call(%s, [])',
			    \ string(s:SpotBugsGetProperties().PostCompilerAction))))))))
	endif

	unlet! s:candidate s:augroup s:action s:actions s:idx s:dispatcher
    endif

    delfunction s:SpotBugsGetProperties
    delfunction s:SpotBugsHasProperty
    delfunction s:SpotBugsGetProperty
    unlet! s:request s:spotbugs_properties_scope
endif

function! JavaFileTypeCleanUp() abort
    setlocal suffixes< suffixesadd< formatoptions< comments< commentstring< path< includeexpr< include< define<
    unlet! b:browsefilter

    " The concatenated ":autocmd" removals may be misparsed as an ":autocmd".
    " A _once-only_ ShellCmdPost ":autocmd" is always a call-site definition.
    silent! autocmd! java_spotbugs User <buffer>
    silent! autocmd! java_spotbugs Syntax <buffer>
    silent! autocmd! java_spotbugs_post User <buffer>
endfunction

// Undo the stuff we changed.
let b:undo_ftplugin = 'call JavaFileTypeCleanUp() | delfunction JavaFileTypeCleanUp'

// See ":help vim9-mix".
if !has("vim9script")
    let &cpo = s:save_cpo
    unlet s:save_cpo
    finish
endif

if exists("s:zip_func_upgradable")
    delfunction! JavaFileTypeZipFile

    def! s:JavaFileTypeZipFile(): string
	@/ = substitute(v:fname, '\.', '\\/', 'g') .. '.java'
	return get(zip_files, bufnr('%'), zip_files[0])
    enddef

    setlocal includeexpr=s:JavaFileTypeZipFile()
    setlocal suffixesadd<
endif

if exists("*s:DispatchAction")
    def! s:DispatchAction(paths_action_pairs: list<list<any>>)
	const name: string = expand('%:p')

	for [paths: list<string>, Action: func: any] in paths_action_pairs
	    for path in paths
		if name =~# (path .. '.\{-}\.java\=$')
		    Action()
		    return
		endif
	    endfor
	endfor
    enddef
endif

// Restore the saved compatibility options.
let &cpo = s:save_cpo
unlet s:save_cpo
// vim: fdm=syntax sw=4 ts=8 noet sta

//}}}
//{{{indent

// Only load this indent file when no other was loaded.
if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

// Indent Java anonymous classes correctly.
setlocal cindent cinoptions& cinoptions+=j1

// The "extends" and "implements" lines start off with the wrong indent.
setlocal indentkeys& indentkeys+=0=extends indentkeys+=0=implements

// Set the function to do the work.
setlocal indentexpr=GetJavaIndent()

let b:undo_indent = "set cin< cino< indentkeys< indentexpr<"

// Only define the function once.
if exists("*GetJavaIndent")
  finish
endif

let s:keepcpo= &cpo


function! SkipJavaBlanksAndComments(startline)
  let lnum = a:startline
  while lnum > 1
    let lnum = prevnonblank(lnum)
    if getline(lnum) =~ '\*/\s*$'
      while getline(lnum) !~ '/\*' && lnum > 1
        let lnum = lnum - 1
      endwhile
      if getline(lnum) =~ '^\s*/\*'
        let lnum = lnum - 1
      else
        break
      endif
    elseif getline(lnum) =~ '^\s*//'
      let lnum = lnum - 1
    else
      break
    endif
  endwhile
  return lnum
endfunction

function GetJavaIndent()

  " Java is just like C; use the built-in C indenting and then correct a few
  " specific cases.
  let theIndent = cindent(v:lnum)

  " If we're in the middle of a comment then just trust cindent
  if getline(v:lnum) =~ '^\s*\*'
    return theIndent
  endif

  " find start of previous line, in case it was a continuation line
  let lnum = SkipJavaBlanksAndComments(v:lnum - 1)

  " If the previous line starts with '@', we should have the same indent as
  " the previous one
  if getline(lnum) =~ '^\s*@.*$'
    return indent(lnum)
  endif

  let prev = lnum
  while prev > 1
    let next_prev = SkipJavaBlanksAndComments(prev - 1)
    if getline(next_prev) !~ ',\s*$'
      break
    endif
    let prev = next_prev
  endwhile

  " Try to align "throws" lines for methods and "extends" and "implements" for
  " classes.
  if getline(v:lnum) =~ '^\s*\(throws\|extends\|implements\)\>'
        \ && getline(lnum) !~ '^\s*\(throws\|extends\|implements\)\>'
    let theIndent = theIndent + shiftwidth()
  endif

  " correct for continuation lines of "throws", "implements" and "extends"
  let cont_kw = matchstr(getline(prev),
        \ '^\s*\zs\(throws\|implements\|extends\)\>\ze.*,\s*$')
  if strlen(cont_kw) > 0
    let amount = strlen(cont_kw) + 1
    if getline(lnum) !~ ',\s*$'
      let theIndent = theIndent - (amount + shiftwidth())
      if theIndent < 0
        let theIndent = 0
      endif
    elseif prev == lnum
      let theIndent = theIndent + amount
      if cont_kw ==# 'throws'
        let theIndent = theIndent + shiftwidth()
      endif
    endif
  elseif getline(prev) =~ '^\s*\(throws\|implements\|extends\)\>'
        \ && (getline(prev) =~ '{\s*$'
        \  || getline(v:lnum) =~ '^\s*{\s*$')
    let theIndent = theIndent - shiftwidth()
  endif

  " When the line starts with a }, try aligning it with the matching {,
  " skipping over "throws", "extends" and "implements" clauses.
  if getline(v:lnum) =~ '^\s*}\s*\(//.*\|/\*.*\)\=$'
    call cursor(v:lnum, 1)
    silent normal! %
    let lnum = line('.')
    if lnum < v:lnum
      while lnum > 1
        let next_lnum = SkipJavaBlanksAndComments(lnum - 1)
        if getline(lnum) !~ '^\s*\(throws\|extends\|implements\)\>'
              \ && getline(next_lnum) !~ ',\s*$'
          break
        endif
        let lnum = prevnonblank(next_lnum)
      endwhile
      return indent(lnum)
    endif
  endif

  " Below a line starting with "}" never indent more.  Needed for a method
  " below a method with an indented "throws" clause.
  let lnum = SkipJavaBlanksAndComments(v:lnum - 1)
  if getline(lnum) =~ '^\s*}\s*\(//.*\|/\*.*\)\=$' && indent(lnum) < theIndent
    let theIndent = indent(lnum)
  endif

  return theIndent
endfunction

let &cpo = s:keepcpo
unlet s:keepcpo

// vi: sw=2 et

//}}}
//{{{syntax

// Do not aggregate syntax items from circular inclusion.
if exists("b:currentSyntax")
  finish
endif

if exists("g:main_syntax")
  // Reject attendant circularity for every :syn-included syntax file,
  // but ACCEPT FAILURE when "g:main_syntax" is set to "java".
  if g:main_syntax == 'html'
    if !exists("g:java_ignore_html")
      let g:java_ignore_html = 1
      let s:clear_java_ignore_html = 1
    endif
  elseif g:main_syntax == 'markdown'
    if !exists("g:java_ignore_markdown")
      let g:java_ignore_markdown = 1
      let s:clear_java_ignore_markdown = 1
    endif
  endif
else
  " Allow syntax files that include this file test for its inclusion.
  let g:main_syntax = 'java'
endif




//""" STRIVE TO REMAIN COMPATIBLE FOR AT LEAST VIM 7.0.
let s:ff = {}

function! s:ff.LeftConstant(x, y) abort
  return a:x
endfunction

function! s:ff.RightConstant(x, y) abort
  return a:y
endfunction

function! s:ff.IsAnyRequestedPreviewFeatureOf(ns) abort
  return exists("g:java_syntax_previews") &&
    \ !empty(filter(a:ns, printf('index(%s, v:val) + 1',
			    \ string(g:java_syntax_previews))))
endfunction

if !exists("*s:ReportOnce")
  function s:ReportOnce(message) abort
    echomsg 'syntax/java.vim: ' . a:message
  endfunction
else
  function! s:ReportOnce(dummy)
  endfunction
endif

if exists("g:java_foldtext_show_first_or_second_line")
  function! s:LazyPrefix(prefix, dashes, count) abort
    return empty(a:prefix)
      \ ? printf('+-%s%3d lines: ', a:dashes, a:count)
      \ : a:prefix
  endfunction

  function! JavaSyntaxFoldTextExpr() abort
    " Piggyback on NGETTEXT.
    let summary = foldtext()
    return getline(v:foldstart) !~ '/\*\+\s*$'
      \ ? summary
      \ : s:LazyPrefix(matchstr(summary, '^+-\+\s*\d\+\s.\{-1,}:\s'),
			\ v:folddashes,
			\ (v:foldend - v:foldstart + 1)) .
	  \ getline(v:foldstart + 1)
  endfunction

  " E120 for "fdt=s:JavaSyntaxFoldTextExpr()" before v8.2.3900.
  setlocal foldtext=JavaSyntaxFoldTextExpr()
endif

// Admit the ASCII dollar sign to keyword characters (JLS-17, §3.8):
try
  exec 'syntax iskeyword ' . &l:iskeyword . ',$'
catch /\<E410:/
  call s:ReportOnce(v:exception)
  setlocal iskeyword+=$
endtry

// some characters that cannot be in a java program (outside a string)
syn match javaError "[\\@`]"
syn match javaError "<<<\|\.\.\|=>\|||=\|&&=\|\*\/"

// use separate name so that it can be deleted in javacc.vim
syn match   javaError2 "#\|=<"

// Keywords (JLS-17, §3.9):
syn keyword javaExternal	native package
syn match   javaExternal	"\<import\>\%(\s\+static\>\)\="
syn keyword javaError		goto const
syn keyword javaConditional	if else switch
syn keyword javaRepeat		while for do
syn keyword javaBoolean		true false
syn keyword javaConstant	null
syn keyword javaTypedef		this super
syn keyword javaOperator	new instanceof
syn match   javaOperator	"\<var\>\%(\s*(\)\@!"

if s:ff.IsAnyRequestedPreviewFeatureOf([476, 494])
  // Module imports can be used in any source file.
  syn match   javaExternal	"\<import\s\+module\>" contains=javaModuleImport
  syn keyword javaModuleImport	contained module
  hi def link javaModuleImport	Statement
endif

// Since the yield statement, which could take a parenthesised operand,
// and _qualified_ yield methods get along within the switch block
// (JLS-17, §3.8), it seems futile to make a region definition for this
// block; instead look for the _yield_ word alone, and if found,
// backtrack (arbitrarily) 80 bytes, at most, on the matched line and,
// if necessary, on the line before that (h: \@<=), trying to match
// neither a method reference nor a qualified method invocation.
try
  if !empty(get(g:, 'java_lookbehind_byte_counts', {}))
    exec 'syn match javaOperator "\%(\%(::\|\.\)[[:space:]\n]*\)\@' . max([0, get(g:java_lookbehind_byte_counts, 'javaOperator', 80)]) . '<!\<yield\>"'

    function! s:ff.PeekFor(item, count) abort
      return string(max([0, get(g:java_lookbehind_byte_counts, a:item, a:count)]))
    endfunction
  else
    syn match  javaOperator	"\%(\%(::\|\.\)[[:space:]\n]*\)\@80<!\<yield\>"

    function! s:ff.PeekFor(dummy, count) abort
      return string(a:count)
    endfunction
  endif

  let s:ff.Peek = s:ff.LeftConstant
catch /\<E59:/
  call s:ReportOnce(v:exception)
  syn match  javaOperator	"\%(\%(::\|\.\)[[:space:]\n]*\)\@<!\<yield\>"

  function! s:ff.PeekFor(dummy_a, dummy_b) abort
    return ""
  endfunction

  let s:ff.Peek = s:ff.RightConstant
endtry

syn keyword javaType		boolean char byte short int long float double
syn keyword javaType		void
syn keyword javaStatement	return
syn keyword javaStorageClass	static synchronized transient volatile strictfp
syn keyword javaExceptions	throw try catch finally
syn keyword javaAssert		assert
syn keyword javaMethodDecl	throws
// Differentiate a "MyClass.class" literal from the keyword "class".
syn match   javaTypedef		"\.\s*\<class\>"ms=s+1
syn keyword javaClassDecl	enum extends implements interface
syn match   javaClassDecl	"\<permits\>\%(\s*(\)\@!"
syn match   javaClassDecl	"\<record\>\%(\s*(\)\@!"
syn match   javaClassDecl	"^class\>"
syn match   javaClassDecl	"[^.]\s*\<class\>"ms=s+1
syn match   javaAnnotation	"@\%(\K\k*\.\)*\K\k*\>"
syn region  javaAnnotation	transparent matchgroup=javaAnnotationStart start=/@\%(\K\k*\.\)*\K\k*(/ end=/)/ skip=/\/\*.\{-}\*\/\|\/\/.*$/ contains=javaAnnotation,javaParenT,javaBlock,javaString,javaBoolean,javaNumber,javaTypedef,javaComment,javaLineComment
syn match   javaClassDecl	"@interface\>"
syn keyword javaBranch		break continue nextgroup=javaUserLabelRef skipwhite
syn match   javaUserLabelRef	contained "\k\+"
syn match   javaVarArg		"\.\.\."
syn keyword javaScopeDecl	public protected private
syn keyword javaConceptKind	abstract final
syn match   javaConceptKind	"\<non-sealed\>"
syn match   javaConceptKind	"\<sealed\>\%(\s*(\)\@!"
syn match   javaConceptKind	"\<default\>\%(\s*\%(:\|->\)\)\@!"

if !(v:version < 704)
  " Request the new regexp engine for [:upper:] and [:lower:].
  let [s:ff.Engine, s:ff.UpperCase, s:ff.LowerCase] = repeat([s:ff.LeftConstant], 3)
else
  " XXX: \C\<[^a-z0-9]\k*\> rejects "type", but matches "τύπος".
  " XXX: \C\<[^A-Z0-9]\k*\> rejects "Method", but matches "Μέθοδος".
  let [s:ff.Engine, s:ff.UpperCase, s:ff.LowerCase] = repeat([s:ff.RightConstant], 3)
endif

if exists("g:java_highlight_signature")
  let [s:ff.PeekTo, s:ff.PeekFrom, s:ff.GroupArgs] = repeat([s:ff.LeftConstant], 3)
else
  let [s:ff.PeekTo, s:ff.PeekFrom, s:ff.GroupArgs] = repeat([s:ff.RightConstant], 3)
endif

let s:with_html = !exists("g:java_ignore_html")
let s:with_markdown = !exists("g:java_ignore_markdown")
lockvar s:with_html s:with_markdown

// Java module declarations (JLS-17, §7.7).
//
// Note that a "module-info" file will be recognised with an arbitrary
// file extension (or no extension at all) so that more than one such
// declaration for the same Java module can be maintained for modular
// testing in a project without attendant confusion for IDEs, with the
// ".java\=" extension used for a production version and an arbitrary
// extension used for a testing version.
if fnamemodify(bufname("%"), ":t") =~ '^module-info\>\%(\.class\>\)\@!'
  syn keyword javaModuleStorageClass	module transitive
  syn keyword javaModuleStmt		open requires exports opens uses provides
  syn keyword javaModuleExternal	to with
  hi def link javaModuleStorageClass	StorageClass
  hi def link javaModuleStmt		Statement
  hi def link javaModuleExternal	Include

  if !exists("g:java_ignore_javadoc") && (s:with_html || s:with_markdown) && g:main_syntax != 'jsp'
    syn match javaDocProvidesTag	contained "@provides\_s\+\S\+" contains=javaDocParam
    syn match javaDocUsesTag		contained "@uses\_s\+\S\+" contains=javaDocParam
    hi def link javaDocProvidesTag	Special
    hi def link javaDocUsesTag		Special
  endif
endif

if exists("g:java_highlight_java_lang_ids")
  let g:java_highlight_all = 1
endif

if exists("g:java_highlight_all") || exists("g:java_highlight_java") || exists("g:java_highlight_java_lang")
  // java.lang.*
  //
  // The type names in ":syn-keyword"s (and in ":syn-match"es) of the
  // "java[CEIRX]_JavaLang" syntax groups are sub-grouped according to
  // the Java version of their introduction, and sub-group names are
  // arranged in alphabetical order, so that future newer names can be
  // pre-sorted and appended without disturbing their placement.

  syn keyword javaR_JavaLang ArithmeticException ArrayIndexOutOfBoundsException ArrayStoreException ClassCastException IllegalArgumentException IllegalMonitorStateException IllegalThreadStateException IndexOutOfBoundsException NegativeArraySizeException NullPointerException NumberFormatException RuntimeException SecurityException StringIndexOutOfBoundsException IllegalStateException UnsupportedOperationException EnumConstantNotPresentException TypeNotPresentException IllegalCallerException LayerInstantiationException WrongThreadException MatchException
  syn cluster javaClasses add=javaR_JavaLang
  hi def link javaR_JavaLang javaR_Java
  syn keyword javaC_JavaLang Boolean Character ClassLoader Compiler Double Float Integer Long Math Number Object Process Runtime SecurityManager String StringBuffer Thread ThreadGroup Byte Short Void Package RuntimePermission StrictMath StackTraceElement ProcessBuilder StringBuilder Module ModuleLayer StackWalker Record
  syn match   javaC_JavaLang "\<System\>"	" See javaDebug.
  " Generic non-interfaces:
  syn match   javaC_JavaLang "\<Class\>"
  syn match   javaC_JavaLang "\<InheritableThreadLocal\>"
  syn match   javaC_JavaLang "\<ThreadLocal\>"
  syn match   javaC_JavaLang "\<Enum\>"
  syn match   javaC_JavaLang "\<ClassValue\>"
  exec 'syn match javaC_JavaLang "\%(\<Enum\.\)\@' . s:ff.Peek('5', '') . '<=\<EnumDesc\>"'
  // Member classes:
  exec 'syn match javaC_JavaLang "\%(\<Character\.\)\@' . s:ff.Peek('10', '') . '<=\<Subset\>"'
  exec 'syn match javaC_JavaLang "\%(\<Character\.\)\@' . s:ff.Peek('10', '') . '<=\<UnicodeBlock\>"'
  exec 'syn match javaC_JavaLang "\%(\<ProcessBuilder\.\)\@' . s:ff.Peek('15', '') . '<=\<Redirect\>"'
  exec 'syn match javaC_JavaLang "\%(\<ModuleLayer\.\)\@' . s:ff.Peek('12', '') . '<=\<Controller\>"'
  exec 'syn match javaC_JavaLang "\%(\<Runtime\.\)\@' . s:ff.Peek('8', '') . '<=\<Version\>"'
  exec 'syn match javaC_JavaLang "\%(\<System\.\)\@' . s:ff.Peek('7', '') . '<=\<LoggerFinder\>"'
  // Member enumerations:
  exec 'syn match javaC_JavaLang "\%(\<Thread\.\)\@' . s:ff.Peek('7', '') . '<=\<State\>"'
  exec 'syn match javaC_JavaLang "\%(\<Character\.\)\@' . s:ff.Peek('10', '') . '<=\<UnicodeScript\>"'
  exec 'syn match javaC_JavaLang "\%(\<ProcessBuilder\.Redirect\.\)\@' . s:ff.Peek('24', '') . '<=\<Type\>"'
  exec 'syn match javaC_JavaLang "\%(\<StackWalker\.\)\@' . s:ff.Peek('12', '') . '<=\<Option\>"'
  exec 'syn match javaC_JavaLang "\%(\<System\.Logger\.\)\@' . s:ff.Peek('14', '') . '<=\<Level\>"'
  syn cluster javaClasses add=javaC_JavaLang
  hi def link javaC_JavaLang javaC_Java
  syn keyword javaE_JavaLang AbstractMethodError ClassCircularityError ClassFormatError Error IllegalAccessError IncompatibleClassChangeError InstantiationError InternalError LinkageError NoClassDefFoundError NoSuchFieldError NoSuchMethodError OutOfMemoryError StackOverflowError ThreadDeath UnknownError UnsatisfiedLinkError VerifyError VirtualMachineError ExceptionInInitializerError UnsupportedClassVersionError AssertionError BootstrapMethodError
  syn cluster javaClasses add=javaE_JavaLang
  hi def link javaE_JavaLang javaE_Java
  syn keyword javaX_JavaLang ClassNotFoundException CloneNotSupportedException Exception IllegalAccessException InstantiationException InterruptedException NoSuchMethodException Throwable NoSuchFieldException ReflectiveOperationException
  syn cluster javaClasses add=javaX_JavaLang
  hi def link javaX_JavaLang javaX_Java
  syn keyword javaI_JavaLang Cloneable Runnable CharSequence Appendable Deprecated Override Readable SuppressWarnings AutoCloseable SafeVarargs FunctionalInterface ProcessHandle
  " Generic non-classes:
  syn match   javaI_JavaLang "\<Comparable\>"
  syn match   javaI_JavaLang "\<Iterable\>"
  // Member interfaces:
  exec 'syn match javaI_JavaLang "\%(\<Thread\.\)\@' . s:ff.Peek('7', '') . '<=\<UncaughtExceptionHandler\>"'
  exec 'syn match javaI_JavaLang "\%(\<ProcessHandle\.\)\@' . s:ff.Peek('14', '') . '<=\<Info\>"'
  exec 'syn match javaI_JavaLang "\%(\<System\.\)\@' . s:ff.Peek('7', '') . '<=\<Logger\>"'
  exec 'syn match javaI_JavaLang "\%(\<StackWalker\.\)\@' . s:ff.Peek('12', '') . '<=\<StackFrame\>"'
  exec 'syn match javaI_JavaLang "\%(\<Thread\.\)\@' . s:ff.Peek('7', '') . '<=\<Builder\>"'
  exec 'syn match javaI_JavaLang "\%(\<Thread\.Builder\.\)\@' . s:ff.Peek('15', '') . '<=\<OfPlatform\>"'
  exec 'syn match javaI_JavaLang "\%(\<Thread\.Builder\.\)\@' . s:ff.Peek('15', '') . '<=\<OfVirtual\>"'
  syn cluster javaClasses add=javaI_JavaLang
  hi def link javaI_JavaLang javaI_Java

  // Common groups for generated "javaid.vim" syntax items:
  hi def link javaR_Java javaR_
  hi def link javaC_Java javaC_
  hi def link javaE_Java javaE_
  hi def link javaX_Java javaX_
  hi def link javaI_Java javaI_
  hi def link javaX_ javaExceptions
  hi def link javaR_ javaExceptions
  hi def link javaE_ javaExceptions
  hi def link javaC_ javaConstant
  hi def link javaI_ javaTypedef

  syn keyword javaLangObject getClass notify notifyAll wait

  // Lower the syntax priority of overridable java.lang.Object method
  // names for zero-width matching (define g:java_highlight_signature
  // and see their base declarations for java.lang.Object):
  syn match javaLangObject "\<clone\>"
  syn match javaLangObject "\<equals\>"
  syn match javaLangObject "\<finalize\>"
  syn match javaLangObject "\<hashCode\>"
  syn match javaLangObject "\<toString\>"
  hi def link javaLangObject javaConstant

  // As of JDK 24, SecurityManager is rendered non-functional
  //	(JDK-8338625).
  //	(Note that SecurityException and RuntimePermission are still
  //	not deprecated.)
  // As of JDK 21, Compiler is no more (JDK-8205129).
  syn keyword javaLangDeprecated Compiler SecurityManager
endif

runtime syntax/javaid.vim

// Type parameter sections (JLS-17, §4.4, §4.5).
//
// Note that false positives may elsewhere occur whenever an identifier
// is butted against a less-than operator.  Cf. (X<Y) and (X < Y).
if exists("g:java_highlight_generics")
  syn keyword javaWildcardBound contained extends super
  syn cluster javaTypeParams contains=javaAnnotation,javaWildcardBound,javaType,@javaClasses,javaComment,javaLineComment

  // Match sections of generic methods and constructors and their
  // parameterised use.
  exec 'syn region javaTypeParamSection transparent matchgroup=javaGenericsCX start=/' . s:ff.Engine('\%#=2', '') . '\%(^\|\s\)\@' . s:ff.Peek('1', '') . '<=<\%(\%([^(){}]\|\n\)\+[[:space:]-]\@' . s:ff.Peek('1', '') . '<!>\_s\+\%(\%(void\|\%(b\%(oolean\|yte\)\|char\|short\|int\|long\|float\|double\|\%(\<\K\k*\>\.\)*\<' . s:ff.UpperCase('[$_[:upper:]]', '[^a-z0-9]') . '\k*\>\%(<\%([^(){}]\|\n\)\+[[:space:]-]\@' . s:ff.Peek('1', '') . '<!>\)\=\)\%(\[\]\)*\)\_s\+\)\=\<\K\k*\>\s*(\)\@=/ end=/>/ contains=javaGenerics,@javaTypeParams'
  exec 'syn region javaTypeParamSection transparent matchgroup=javaGenericsCX start=/\%(\%(\<new\|::\|\.\)[[:space:]\n]*\)\@' . s:ff.PeekFor('javaTypeParamSection', 80) . '<=<>\@!/ end=/>/ contains=javaGenerics,@javaTypeParams'

  for s:ctx in [{'gsg': 'javaGenerics', 'ghg': 'javaGenericsC1', 'csg': 'javaGenericsX', 'c': ''},
      \ {'gsg': 'javaGenericsX', 'ghg': 'javaGenericsC2', 'csg': 'javaGenerics', 'c': ' contained'}]
    " Match sections of generic types and their parameterised use.
    exec 'syn region ' . s:ctx.gsg . s:ctx.c . ' transparent matchgroup=' . s:ctx.ghg . ' start=/' . s:ff.Engine('\%#=2', '') . '\%(\<\K\k*\>\.\)*\<' . s:ff.UpperCase('[$_[:upper:]]', '[^a-z0-9]') . '\k*\><\%([[:space:]\n]*\%([?@]\|\<\%(b\%(oolean\|yte\)\|char\|short\|int\|long\|float\|double\)\|\%(\<\K\k*\>\.\)*\<' . s:ff.UpperCase('[$_[:upper:]]', '[^a-z0-9]') . '\k*\>\)\)\@=/ end=/>/ contains=' . s:ctx.csg . ',@javaTypeParams'
  endfor

  unlet s:ctx
  hi def link javaWildcardBound	Question
  hi def link javaGenericsC1	Function
  hi def link javaGenericsC2	Type
  hi def link javaGenericsCX	javaGenericsC2
endif

if exists("g:java_space_errors")
  if !exists("g:java_no_trail_space_error")
    syn match javaSpaceError "\s\+$"
  endif
  if !exists("g:java_no_tab_space_error")
    syn match javaSpaceError " \+\t"me=e-1
  endif
  hi def link javaSpaceError Error
endif

exec 'syn match javaUserLabel "^\s*\<\K\k*\>\%(\<default\>\)\@' . s:ff.Peek('7', '') . '<!\s*::\@!"he=e-1'

if s:ff.IsAnyRequestedPreviewFeatureOf([455, 488])
  syn region  javaLabelRegion	transparent matchgroup=javaLabel start="\<case\>" matchgroup=NONE end=":\|->" contains=javaBoolean,javaNumber,javaCharacter,javaString,javaConstant,@javaClasses,javaGenerics,javaType,javaLabelDefault,javaLabelVarType,javaLabelWhenClause
else
  syn region  javaLabelRegion	transparent matchgroup=javaLabel start="\<case\>" matchgroup=NONE end=":\|->" contains=javaLabelCastType,javaLabelNumber,javaCharacter,javaString,javaConstant,@javaClasses,javaGenerics,javaLabelDefault,javaLabelVarType,javaLabelWhenClause
  syn keyword javaLabelCastType	contained char byte short int
  syn match   javaLabelNumber	contained "\<0\>[lL]\@!"
  syn match   javaLabelNumber	contained "\<\%(0\%([xX]\x\%(_*\x\)*\|_*\o\%(_*\o\)*\|[bB][01]\%(_*[01]\)*\)\|[1-9]\%(_*\d\)*\)\>[lL]\@!"
  hi def link javaLabelCastType	javaType
  hi def link javaLabelNumber	javaNumber
endif

syn region  javaLabelRegion	transparent matchgroup=javaLabel start="\<default\>\%(\s*\%(:\|->\)\)\@=" matchgroup=NONE end=":\|->" oneline
// Consider grouped _default_ _case_ labels, i.e.
// case null, default ->
// case null: default:
syn keyword javaLabelDefault	contained default
syn keyword javaLabelVarType	contained var
// Allow for the contingency of the enclosing region not being able to
// _keep_ its _end_, e.g. case ':':.
syn region  javaLabelWhenClause	contained transparent matchgroup=javaLabel start="\<when\>" matchgroup=NONE end=":"me=e-1 end="->"me=e-2 contains=TOP,javaExternal,javaLambdaDef

// Comments
syn keyword javaTodo		contained TODO FIXME XXX

if exists("g:java_comment_strings")
  syn region  javaCommentString	contained start=+"+ end=+"+ end=+$+ end=+\*/+me=s-1,he=s-1 contains=javaSpecial,javaCommentStar,javaSpecialChar,@Spell
  syn region  javaCommentString	contained start=+"""[ \t\x0c\r]*$+hs=e+1 end=+"""+he=s-1 contains=javaSpecial,javaCommentStar,javaSpecialChar,@Spell,javaSpecialError,javaTextBlockError
  syn region  javaComment2String contained start=+"+ end=+$\|"+ contains=javaSpecial,javaSpecialChar,@Spell
  syn match   javaCommentCharacter contained "'\\[^']\{1,6\}'" contains=javaSpecialChar
  syn match   javaCommentCharacter contained "'\\''" contains=javaSpecialChar
  syn match   javaCommentCharacter contained "'[^\\]'"
  syn cluster javaCommentSpecial add=javaCommentString,javaCommentCharacter,javaNumber,javaStrTempl
  syn cluster javaCommentSpecial2 add=javaComment2String,javaCommentCharacter,javaNumber,javaStrTempl
endif

syn region  javaComment		matchgroup=javaCommentStart start="/\*" end="\*/" contains=@javaCommentSpecial,javaTodo,javaCommentError,javaSpaceError,@Spell fold
syn match   javaCommentStar	contained "^\s*\*[^/]"me=e-1
syn match   javaCommentStar	contained "^\s*\*$"
syn match   javaLineComment	"//.*" contains=@javaCommentSpecial2,javaTodo,javaCommentMarkupTag,javaSpaceError,@Spell
syn match   javaCommentMarkupTag contained "@\%(end\|highlight\|link\|replace\|start\)\>" nextgroup=javaCommentMarkupTagAttr,javaSpaceError skipwhite
syn match   javaCommentMarkupTagAttr contained "\<region\>" nextgroup=javaCommentMarkupTagAttr,javaSpaceError skipwhite
exec 'syn region javaCommentMarkupTagAttr contained transparent matchgroup=javaHtmlArg start=/\<\%(re\%(gex\|gion\|placement\)\|substring\|t\%(arget\|ype\)\)\%(\s*=\)\@=/ matchgroup=javaHtmlString end=/\%(=\s*\)\@' . s:ff.PeekFor('javaCommentMarkupTagAttr', 80) . '<=\%("[^"]\+"\|' . "\x27[^\x27]\\+\x27" . '\|\%([.-]\|\k\)\+\)/ nextgroup=javaCommentMarkupTagAttr,javaSpaceError skipwhite oneline'
syn match   javaCommentError contained "/\*"me=e-1 display

if !exists("g:java_ignore_javadoc") && (s:with_html || s:with_markdown) && g:main_syntax != 'jsp'
  // The overridable "html*" and "markdown*" default links must be
  // defined _before_ the inclusion of the same default links from
  // "html.vim" and "markdown.vim".
  if s:with_html || s:with_markdown
    hi def link htmlComment		Special
    hi def link htmlCommentPart		Special
    hi def link htmlArg			Type
    hi def link htmlString		String
  endif

  if s:with_markdown
    hi def link markdownCode		Special
    hi def link markdownCodeBlock	Special
    hi def link markdownCodeDelimiter	Special
    hi def link markdownLinkDelimiter	Comment
  endif

  syntax case ignore

  // Note that javaDocSeeTag is valid in HTML and Markdown.
  let s:ff.WithMarkdown = s:ff.RightConstant

  // Include HTML syntax coloring for Javadoc comments.
  if s:with_html
    try
      if exists("g:html_syntax_folding") && !exists("g:java_consent_to_html_syntax_folding")
	let s:html_syntax_folding_copy = g:html_syntax_folding
	unlet g:html_syntax_folding
      endif

      syntax include @javaHtml syntax/html.vim
    finally
      unlet! b:currentSyntax

      if exists("s:html_syntax_folding_copy")
	let g:html_syntax_folding = s:html_syntax_folding_copy
	unlet s:html_syntax_folding_copy
      endif
    endtry
  endif

  // Include Markdown syntax coloring (v7.2.437) for Javadoc comments.
  if s:with_markdown
    try
      if exists("g:html_syntax_folding") && !exists("g:java_consent_to_html_syntax_folding")
	let s:html_syntax_folding_copy = g:html_syntax_folding
	unlet g:html_syntax_folding
      endif

      syntax include @javaMarkdown syntax/markdown.vim

      try
	syn clear markdownId markdownLineStart markdownH1 markdownH2 markdownHeadingRule markdownRule markdownCode markdownCodeBlock markdownIdDeclaration
	let s:ff.WithMarkdown = s:ff.LeftConstant
      catch /\<E28:/
	call s:ReportOnce(v:exception)
	let s:no_support = 1
	unlet! g:java_ignore_markdown
	let g:java_ignore_markdown = 28
      endtry
    catch /\<E48[45]:/
      call s:ReportOnce(v:exception)
      let s:no_support = 1
    finally
      unlet! b:currentSyntax

      if exists("s:html_syntax_folding_copy")
	let g:html_syntax_folding = s:html_syntax_folding_copy
	unlet s:html_syntax_folding_copy
      endif

      if exists("s:no_support")
	unlet s:no_support
	unlockvar s:with_markdown
	let s:with_markdown = 0
	lockvar s:with_markdown
	hi clear markdownCode
	hi clear markdownCodeBlock
	hi clear markdownCodeDelimiter
	hi clear markdownLinkDelimiter
      endif
    endtry
  endif

  // HTML enables spell checking for all text that is not in a syntax
  // item (:syntax spell toplevel); instead, limit spell checking to
  // items matchable with syntax groups containing the @Spell cluster.
  try
    syntax spell default
  catch /\<E390:/
    call s:ReportOnce(v:exception)
  endtry

  if s:with_markdown
    syn region javaMarkdownComment	start="///" skip="^\s*///.*$" end="^" keepend contains=javaMarkdownCommentTitle,javaMarkdownShortcutLink,@javaMarkdown,@javaDocTags,javaTodo,@Spell nextgroup=javaMarkdownCommentTitle fold
    syn match javaMarkdownCommentMask	contained "^\s*///"
    exec 'syn region javaMarkdownCommentTitle contained matchgroup=javaMarkdownComment start="\%(///.*\r\=\n\s*\)\@' . s:ff.PeekFor('javaMarkdownCommentTitle', 120) . '<!///" matchgroup=javaMarkdownCommentTitle end="\.$" end="\.[ \t\r]\@=" end="\n\%(\s*///\s*$\)\@=" end="\%(^\s*///\s*\)\@' . s:ff.PeekFor('javaMarkdownCommentTitle', 120) . '<=@"me=s-2,he=s-1 contains=javaMarkdownShortcutLink,@javaMarkdown,javaMarkdownCommentMask,javaTodo,@Spell,@javaDocTags'
    exec 'syn region javaMarkdownCommentTitle contained matchgroup=javaMarkdownComment start="\%(///.*\r\=\n\s*\)\@' . s:ff.PeekFor('javaMarkdownCommentTitle', 120) . '<!///\s*\%({@return\>\)\@=" matchgroup=javaMarkdownCommentTitle end="}\%(\s*\.*\)*" contains=javaMarkdownShortcutLink,@javaMarkdown,javaMarkdownCommentMask,javaTodo,@Spell,@javaDocTags,javaTitleSkipBlock'
    exec 'syn region javaMarkdownCommentTitle contained matchgroup=javaMarkdownComment start="\%(///.*\r\=\n\s*\)\@' . s:ff.PeekFor('javaMarkdownCommentTitle', 120) . '<!///\s*\%({@summary\>\)\@=" matchgroup=javaMarkdownCommentTitle end="}" contains=javaMarkdownShortcutLink,@javaMarkdown,javaMarkdownCommentMask,javaTodo,@Spell,@javaDocTags,javaTitleSkipBlock'

    // REDEFINE THE MARKDOWN ITEMS ANCHORED WITH "^", OBSERVING THE
    // DEFINITION ORDER.
    syn match markdownLineStart		contained "^\s*///\s*[<@]\@!" contains=@markdownBlock,javaMarkdownCommentTitle,javaMarkdownCommentMask nextgroup=@markdownBlock,htmlSpecialChar
    // See https://spec.commonmark.org/0.31.2/#setext-headings.
    syn match markdownH1		contained "^\s*/// \{,3}.\+\r\=\n\s*/// \{,3}=\+\s*$" contains=@markdownInline,markdownHeadingRule,markdownAutomaticLink,javaMarkdownCommentMask
    syn match markdownH2		contained "^\s*/// \{,3}.\+\r\=\n\s*/// \{,3}-\+\s*$" contains=@markdownInline,markdownHeadingRule,markdownAutomaticLink,javaMarkdownCommentMask
    // See https://spec.commonmark.org/0.31.2/#atx-headings.
    syn region markdownH1		contained matchgroup=markdownH1Delimiter start=" \{,3}#\s" end="#*\s*$" keepend contains=@markdownInline,markdownAutomaticLink oneline
    syn region markdownH2		contained matchgroup=markdownH2Delimiter start=" \{,3}##\s" end="#*\s*$" keepend contains=@markdownInline,markdownAutomaticLink oneline
    syn match markdownHeadingRule	contained "^\s*/// \{,3}[=-]\+\s*$" contains=javaMarkdownCommentMask
    // See https://spec.commonmark.org/0.31.2/#thematic-breaks.
    syn match markdownRule		contained "^\s*/// \{,3}\*\s*\*\%(\s*\*\)\+\s*$" contains=javaMarkdownCommentMask
    syn match markdownRule		contained "^\s*/// \{,3}_\s*_\%(\s*_\)\+\s*$" contains=javaMarkdownCommentMask
    syn match markdownRule		contained "^\s*/// \{,3}-\s*-\%(\s*-\)\+\s*$" contains=javaMarkdownCommentMask
    // See https://spec.commonmark.org/0.31.2/#indented-code-blocks.
    syn region markdownCodeBlock	contained start="^\s*///\%( \{4,}\|\t\)" end="^\ze\s*///\%(\s*$\| \{,3}\S\)" keepend contains=javaMarkdownCommentMask
    // See https://spec.commonmark.org/0.31.2/#code-spans.
    syn region markdownCode		contained matchgroup=markdownCodeDelimiter start="\z(`\+\) \=" end=" \=\z1" keepend contains=markdownLineStart,javaMarkdownCommentMask
    // See https://spec.commonmark.org/0.31.2/#fenced-code-blocks.
    syn region markdownCodeBlock	contained start="^\s*/// \{,3}\z(```\+\)\%(.\{-}[^`]`\)\@!" end="^\s*/// \{,3}\z1`*" keepend contains=javaMarkdownCommentMask
    syn region markdownCodeBlock	contained start="^\s*/// \{,3}\z(\~\~\~\+\)" end="^\s*/// \{,3}\z1\~*" keepend contains=javaMarkdownCommentMask
    // See https://spec.commonmark.org/0.31.2/#link-reference-definitions.
    syn region markdownIdDeclaration	contained matchgroup=markdownLinkDelimiter start="^\s*/// \{,3\}!\=\[" end="\]:" keepend contains=javaMarkdownCommentMask nextgroup=markdownUrl oneline skipwhite
    // See https://spec.commonmark.org/0.31.2/#link-label.
    syn region markdownId		contained matchgroup=markdownIdDelimiter start="\[\%([\t ]\]\)\@!" end="\]" contains=javaMarkdownSkipBrackets,javaMarkdownCommentMask
    // Note that escaped brackets can be unbalanced.
    syn match javaMarkdownSkipBrackets	contained transparent "\\\[\|\\\]"
    // See https://spec.commonmark.org/0.31.2/#shortcut-reference-link.
    syn region javaMarkdownShortcutLink	contained matchgroup=markdownLinkTextDelimiter start="!\=\[^\@!\%(\_[^][]*\%(\[\_[^][]*\]\_[^][]*\)*]\%([[(]\)\@!\)\@=" end="\]\%([[(]\)\@!" contains=@markdownInline,markdownLineStart,javaMarkdownSkipBrackets,javaMarkdownCommentMask nextgroup=markdownLink,markdownId skipwhite

    for s:name in ['markdownFootnoteDefinition', 'markdownFootnote']
      if hlexists(s:name)
	exec 'syn clear ' . s:name
      endif
    endfor

    unlet s:name

    // COMBAK: Footnotes are recognised by "markdown.vim", but are not
    // in CommonMark.  See https://pandoc.org/MANUAL.html#footnotes.
//"""syn match markdownFootnoteDefinition contained "^\s*///\s*\[^[^\]]\+\]:" contains=javaMarkdownCommentMask

    hi def link javaMarkdownComment	Comment
    hi def link javaMarkdownCommentMask	javaMarkdownComment
    hi def link javaMarkdownCommentTitle SpecialComment
    hi def link javaMarkdownShortcutLink htmlLink
  endif

  if s:with_html
    syn region javaDocComment	start="/\*\*" end="\*/" keepend contains=javaCommentTitle,@javaHtml,@javaDocTags,javaTodo,javaCommentError,javaSpaceError,@Spell fold
    exec 'syn region javaCommentTitle contained matchgroup=javaDocComment start="/\*\*" matchgroup=javaCommentTitle end="\.$" end="\.[ \t\r]\@=" end="\%(^\s*\**\s*\)\@' . s:ff.PeekFor('javaCommentTitle', 120) . '<=@"me=s-2,he=s-1 end="\*/"me=s-1,he=s-1 contains=@javaHtml,javaCommentStar,javaTodo,javaCommentError,javaSpaceError,@Spell,@javaDocTags'
    syn region javaCommentTitle	contained matchgroup=javaDocComment start="/\*\*\s*\r\=\n\=\s*\**\s*\%({@return\>\)\@=" matchgroup=javaCommentTitle end="}\%(\s*\.*\)*" contains=@javaHtml,javaCommentStar,javaTodo,javaCommentError,javaSpaceError,@Spell,@javaDocTags,javaTitleSkipBlock
    syn region javaCommentTitle	contained matchgroup=javaDocComment start="/\*\*\s*\r\=\n\=\s*\**\s*\%({@summary\>\)\@=" matchgroup=javaCommentTitle end="}" contains=@javaHtml,javaCommentStar,javaTodo,javaCommentError,javaSpaceError,@Spell,@javaDocTags,javaTitleSkipBlock
    hi def link javaDocComment		Comment
    hi def link javaCommentTitle	SpecialComment
  endif

  // The members of javaDocTags are sub-grouped according to the Java
  // version of their introduction, and sub-group members in turn are
  // arranged in alphabetical order, so that future newer members can
  // be pre-sorted and appended without disturbing the current member
  // placement.
  // Since they only have significance in javaCommentTitle, neither
  // javaDocSummaryTag nor javaDocReturnTitleTag are defined.
  syn cluster javaDocTags	contains=javaDocAuthorTag,javaDocDeprecatedTag,javaDocExceptionTag,javaDocParamTag,javaDocReturnTag,javaDocSeeTag,javaDocVersionTag,javaDocSinceTag,javaDocLinkTag,javaDocSerialTag,javaDocSerialDataTag,javaDocSerialFieldTag,javaDocThrowsTag,javaDocDocRootTag,javaDocInheritDocTag,javaDocLinkplainTag,javaDocValueTag,javaDocCodeTag,javaDocLiteralTag,javaDocHiddenTag,javaDocIndexTag,javaDocProvidesTag,javaDocUsesTag,javaDocSystemPropertyTag,javaDocSnippetTag,javaDocSpecTag

  // Anticipate non-standard inline tags in {@return} and {@summary}.
  syn region javaTitleSkipBlock	contained transparent start="{\%(@\%(return\|summary\)\>\)\@!" end="}"
  syn match  javaDocDocRootTag	contained "{@docRoot}"
  syn match  javaDocInheritDocTag contained "{@inheritDoc}"
  syn region javaIndexSkipBlock	contained transparent start="{\%(@index\>\)\@!" end="}" contains=javaIndexSkipBlock,javaDocIndexTag
  syn region javaDocIndexTag	contained start="{@index\>" end="}" contains=javaDocIndexTag,javaIndexSkipBlock
  syn region javaLinkSkipBlock	contained transparent start="{\%(@link\>\)\@!" end="}" contains=javaLinkSkipBlock,javaDocLinkTag
  syn region javaDocLinkTag	contained start="{@link\>" end="}" contains=javaDocLinkTag,javaLinkSkipBlock
  syn region javaLinkplainSkipBlock contained transparent start="{\%(@linkplain\>\)\@!" end="}" contains=javaLinkplainSkipBlock,javaDocLinkplainTag
  syn region javaDocLinkplainTag contained start="{@linkplain\>" end="}" contains=javaDocLinkplainTag,javaLinkplainSkipBlock
  syn region javaLiteralSkipBlock contained transparent start="{\%(@literal\>\)\@!" end="}" contains=javaLiteralSkipBlock,javaDocLiteralTag
  syn region javaDocLiteralTag	contained start="{@literal\>" end="}" contains=javaDocLiteralTag,javaLiteralSkipBlock
  syn region javaSystemPropertySkipBlock contained transparent start="{\%(@systemProperty\>\)\@!" end="}" contains=javaSystemPropertySkipBlock,javaDocSystemPropertyTag
  syn region javaDocSystemPropertyTag contained start="{@systemProperty\>" end="}" contains=javaDocSystemPropertyTag,javaSystemPropertySkipBlock
  syn region javaValueSkipBlock	contained transparent start="{\%(@value\>\)\@!" end="}" contains=javaValueSkipBlock,javaDocValueTag
  syn region javaDocValueTag	contained start="{@value\>" end="}" contains=javaDocValueTag,javaValueSkipBlock

  syn match  javaDocParam	contained "\s\zs\S\+"
  syn match  javaDocExceptionTag contained "@exception\s\+\S\+" contains=javaDocParam
  syn match  javaDocParamTag	contained "@param\s\+\S\+" contains=javaDocParam
  syn match  javaDocSinceTag	contained "@since\s\+\S\+" contains=javaDocParam
  syn match  javaDocThrowsTag	contained "@throws\s\+\S\+" contains=javaDocParam
  syn match  javaDocSpecTag	contained "@spec\_s\+\S\+\ze\_s\+\S\+" contains=javaDocParam

  syn match  javaDocAuthorTag	contained "@author\>"
  syn match  javaDocDeprecatedTag contained "@deprecated\>"
  syn match  javaDocHiddenTag	contained "@hidden\>"
  syn match  javaDocReturnTag	contained "@return\>"
  syn match  javaDocSerialTag	contained "@serial\>"
  syn match  javaDocSerialDataTag contained "@serialData\>"
  syn match  javaDocSerialFieldTag contained "@serialField\>"
  syn match  javaDocVersionTag	contained "@version\>"

  syn match javaDocSeeTag contained "@see\>\s*" nextgroup=javaDocSeeTag1,javaDocSeeTag2,javaDocSeeTag3,javaDocSeeTag4,javaDocSeeTagStar,javaDocSeeTagSlash skipwhite skipempty

  if s:with_html
    syn match  javaDocSeeTagStar contained "^\s*\*\+\%(\s*{\=@\|/\|$\)\@!" nextgroup=javaDocSeeTag1,javaDocSeeTag2,javaDocSeeTag3,javaDocSeeTag4 skipwhite skipempty
    hi def link javaDocSeeTagStar javaDocComment
  endif

  if s:with_markdown
    syn match  javaDocSeeTagSlash contained "^\s*///\%(\s*{\=@\|$\)\@!" nextgroup=javaDocSeeTag1,javaDocSeeTag2,javaDocSeeTag3,javaDocSeeTag4 skipwhite skipempty
    hi def link javaDocSeeTagSlash javaMarkdownComment
  endif

  syn match  javaDocSeeTag1	contained @"\_[^"]\+"@
  syn match  javaDocSeeTag2	contained @<a\s\+\_.\{-}</a>@ contains=@javaHtml extend
  exec 'syn match javaDocSeeTag3 contained @[' . s:ff.WithMarkdown('[', '') . '"< \t]\@!\%(\k\|[/.]\)*\%(##\=\k\+\%((\_[^)]*)\)\=\)\=@ nextgroup=javaDocSeeTag3Label skipwhite skipempty'
  syn match  javaDocSeeTag3Label contained @\k\%(\k\+\s*\)*$@

  // COMBAK: No support for type javaDocSeeTag2 in Markdown.
  //if s:with_markdown
  //  syn match  javaDocSeeTag4	contained @\[.\+\]\s\=\%(\[.\+\]\|(.\+)\)@ contains=@javaMarkdown extend
  //  hi def link javaDocSeeTag4	Special
  //endif

  syn region javaCodeSkipBlock	contained transparent start="{\%(@code\>\)\@!" end="}" contains=javaCodeSkipBlock,javaDocCodeTag
  syn region javaDocCodeTag	contained start="{@code\>" end="}" contains=javaDocCodeTag,javaCodeSkipBlock

  exec 'syn region javaDocSnippetTagAttr contained transparent matchgroup=javaHtmlArg start=/\<\%(class\|file\|id\|lang\|region\)\%(\s*=\)\@=/ matchgroup=javaHtmlString end=/:$/ end=/\%(=\s*\)\@' . s:ff.PeekFor('javaDocSnippetTagAttr', 80) . '<=\%("[^"]\+"\|' . "\x27[^\x27]\\+\x27" . '\|\%([.\\/-]\|\k\)\+\)/ nextgroup=javaDocSnippetTagAttr skipwhite skipnl'
  syn region javaSnippetSkipBlock contained transparent start="{\%(@snippet\>\)\@!" end="}" contains=javaSnippetSkipBlock,javaDocSnippetTag,javaCommentMarkupTag
  syn region javaDocSnippetTag	contained start="{@snippet\>" end="}" contains=javaDocSnippetTag,javaSnippetSkipBlock,javaDocSnippetTagAttr,javaCommentMarkupTag

  syntax case match
  hi def link javaDocParam		Function

  hi def link javaDocAuthorTag		Special
  hi def link javaDocCodeTag		Special
  hi def link javaDocDeprecatedTag	Special
  hi def link javaDocDocRootTag		Special
  hi def link javaDocExceptionTag	Special
  hi def link javaDocHiddenTag		Special
  hi def link javaDocIndexTag		Special
  hi def link javaDocInheritDocTag	Special
  hi def link javaDocLinkTag		Special
  hi def link javaDocLinkplainTag	Special
  hi def link javaDocLiteralTag		Special
  hi def link javaDocParamTag		Special
  hi def link javaDocReturnTag		Special
  hi def link javaDocSeeTag		Special
  hi def link javaDocSeeTag1		String
  hi def link javaDocSeeTag2		Special
  hi def link javaDocSeeTag3		Function
  hi def link javaDocSerialTag		Special
  hi def link javaDocSerialDataTag	Special
  hi def link javaDocSerialFieldTag	Special
  hi def link javaDocSinceTag		Special
  hi def link javaDocSnippetTag		Special
  hi def link javaDocSpecTag		Special
  hi def link javaDocSystemPropertyTag	Special
  hi def link javaDocThrowsTag		Special
  hi def link javaDocValueTag		Special
  hi def link javaDocVersionTag		Special
endif

// match the special comment /**/
syn match   javaComment		"/\*\*/"

// Strings and constants
syn match   javaSpecialError	contained "\\."
syn match   javaSpecialCharError contained "[^']"
// Escape Sequences (JLS-17, §3.10.7):
syn match   javaSpecialChar	contained "\\\%(u\x\x\x\x\|[0-3]\o\o\|\o\o\=\|[bstnfr"'\\]\)"
syn region  javaString		start=+"+ end=+"+ end=+$+ contains=javaSpecialChar,javaSpecialError,@Spell
syn region  javaString		start=+"""[ \t\x0c\r]*$+hs=e+1 end=+"""+he=s-1 contains=javaSpecialChar,javaSpecialError,javaTextBlockError,@Spell
syn match   javaTextBlockError	+"""\s*"""+

if s:ff.IsAnyRequestedPreviewFeatureOf([430])
  syn region javaStrTemplEmbExp	contained matchgroup=javaStrTempl start="\\{" end="}" contains=TOP
  exec 'syn region javaStrTempl start=+\%(\.[[:space:]\n]*\)\@' . s:ff.PeekFor('javaStrTempl', 80) . '<="+ end=+"+ contains=javaStrTemplEmbExp,javaSpecialChar,javaSpecialError,@Spell'
  exec 'syn region javaStrTempl start=+\%(\.[[:space:]\n]*\)\@' . s:ff.PeekFor('javaStrTempl', 80) . '<="""[ \t\x0c\r]*$+hs=e+1 end=+"""+he=s-1 contains=javaStrTemplEmbExp,javaSpecialChar,javaSpecialError,javaTextBlockError,@Spell'
  hi def link javaStrTempl	Macro
endif

syn match   javaCharacter	"'[^']*'" contains=javaSpecialChar,javaSpecialCharError
syn match   javaCharacter	"'\\''" contains=javaSpecialChar
syn match   javaCharacter	"'[^\\]'"
// Integer literals (JLS-17, §3.10.1):
syn keyword javaNumber		0 0l 0L
syn match   javaNumber		"\<\%(0\%([xX]\x\%(_*\x\)*\|_*\o\%(_*\o\)*\|[bB][01]\%(_*[01]\)*\)\|[1-9]\%(_*\d\)*\)[lL]\=\>"
// Decimal floating-point literals (JLS-17, §3.10.2):
// Against "\<\d\+\>\.":
syn match   javaNumber		"\<\d\%(_*\d\)*\."
syn match   javaNumber		"\%(\<\d\%(_*\d\)*\.\%(\d\%(_*\d\)*\)\=\|\.\d\%(_*\d\)*\)\%([eE][-+]\=\d\%(_*\d\)*\)\=[fFdD]\=\>"
syn match   javaNumber		"\<\d\%(_*\d\)*[eE][-+]\=\d\%(_*\d\)*[fFdD]\=\>"
syn match   javaNumber		"\<\d\%(_*\d\)*\%([eE][-+]\=\d\%(_*\d\)*\)\=[fFdD]\>"
// Hexadecimal floating-point literals (JLS-17, §3.10.2):
syn match   javaNumber		"\<0[xX]\%(\x\%(_*\x\)*\.\=\|\%(\x\%(_*\x\)*\)\=\.\x\%(_*\x\)*\)[pP][-+]\=\d\%(_*\d\)*[fFdD]\=\>"

// Unicode characters
syn match   javaSpecial "\\u\x\x\x\x"

// Method declarations (JLS-17, §8.4.3, §8.4.4, §9.4).
if exists("g:java_highlight_functions")
  syn cluster javaFuncParams contains=javaAnnotation,@javaClasses,javaGenerics,javaType,javaVarArg,javaComment,javaLineComment

  if exists("g:java_highlight_signature")
    syn cluster javaFuncParams add=javaParamModifier
    hi def link javaFuncDefStart javaFuncDef
  else
    syn cluster javaFuncParams add=javaScopeDecl,javaConceptKind,javaStorageClass,javaExternal,javaTypeParamSection
  endif

  if g:java_highlight_functions =~# '^indent[1-8]\=$'
    let s:last = g:java_highlight_functions[-1 :]
    let s:indent = s:last != 't' ? repeat("\x20", s:last) : "\t"
    // Try to not match other type members, initialiser blocks, enum
    // constants (JLS-17, §8.9.1), and constructors (JLS-17, §8.1.7):
    // at any _conventional_ indentation, skip over all fields with
    // "[^=]*", all records with "\<record\s", and let the "*Skip*"
    // definitions take care of constructor declarations and enum
    // constants (with no support for @Foo(value = "bar")).  Also,
    // reject inlined declarations with "[^{]" for signature.
    exec 'syn region javaFuncDef ' . s:ff.GroupArgs('transparent matchgroup=javaFuncDefStart', '') . ' start="' . s:ff.PeekTo('\%(', '') . '^' . s:indent . '\%(<\%(/\*.\{-}\*/\|[^(){}>]\|\n\)\+>\+\s\+\|\%(\%(@\%(\K\k*\.\)*\K\k*\>\)\s\+\)\+\)\=\%(\<\K\k*\>\.\)*\K\k*\>[^={]*\%(\<record\)\@' . s:ff.Peek('6', '') . '<!\s' . s:ff.PeekFrom('\)\@' . s:ff.PeekFor('javaFuncDef', 120) . '<=', '') . '\K\k*\s*(" end=")" contains=@javaFuncParams'
    " As long as package-private constructors cannot be matched with
    " javaFuncDef, do not look with javaConstructorSkipDeclarator for
    " them.  (Approximate "javaTypeParamSection" if necessary.)
    exec 'syn match javaConstructorSkipDeclarator transparent "^' . s:indent . '\%(\%(@\%(\K\k*\.\)*\K\k*\>\)\s\+\)*p\%(ublic\|rotected\|rivate\)\s\+\%(<\%(/\*.\{-}\*/\|[^(){}>]\|\n\)\+>\+\s\+\)\=\K\k*\s*(\@=" contains=javaAnnotation,javaScopeDecl,javaTypeParamSection,javaClassDecl,javaTypedef,javaType,@javaClasses,javaGenerics,javaComment,javaLineComment'
    " With a zero-width span for signature applicable on demand to
    " javaFuncDef, make related adjustments:
    " (1) Claim all enum constants of a line as a unit.
    exec 'syn match javaEnumSkipConstant contained transparent /^' . s:indent . '\%(\%(\%(@\%(\K\k*\.\)*\K\k*\>\)\s\+\)*\K\k*\s*\%((.*)\)\=\s*[,;({]\s*\)\+/ contains=@javaEnumConstants'
    " (2) Define a syntax group for top level enumerations and tell
    " apart their constants from method declarations.
    exec 'syn region javaTopEnumDeclaration transparent start=/\%(^\%(\%(@\%(\K\k*\.\)*\K\k*\>\)\s\+\)*\%(p\%(ublic\|rotected\|rivate\)\s\+\)\=\%(strictfp\s\+\)\=\<enum\_s\+\)\@' . s:ff.PeekFor('javaTopEnumDeclaration', 80) . '<=\K\k*\%(\_s\+implements\_s.\+\)\=\_s*{/ end=/}/ contains=@javaTop,javaEnumSkipConstant'
    " (3) Define a base variant of javaParenT without using @javaTop
    " in order to not include javaFuncDef.
    syn region javaParenE transparent matchgroup=javaParen start="(" end=")" contains=@javaEnumConstants,javaInParen
    syn region javaParenE transparent matchgroup=javaParen start="\[" end="\]" contains=@javaEnumConstants
    syn cluster javaEnumConstants contains=TOP,javaTopEnumDeclaration,javaFuncDef,javaParenT
    unlet s:indent s:last
  else
    " This is the "style" variant (:help ft-java-syntax).

    " Match arbitrarily indented camelCasedName method declarations.
    " Match: [@ɐ] [abstract] [<α, β>] Τʬ[<γ>][[][]] μʭʭ(/* ... */);
    exec 'syn region javaFuncDef ' . s:ff.GroupArgs('transparent matchgroup=javaFuncDefStart', '') . ' start=/' . s:ff.Engine('\%#=2', '') . s:ff.PeekTo('\%(', '') . '^\s\+\%(\%(@\%(\K\k*\.\)*\K\k*\>\)\s\+\)*\%(p\%(ublic\|rotected\|rivate\)\s\+\)\=\%(\%(abstract\|default\)\s\+\|\%(\%(final\|\%(native\|strictfp\)\|s\%(tatic\|ynchronized\)\)\s\+\)*\)\=\%(<\%([^(){}]\|\n\)\+[[:space:]-]\@' . s:ff.Peek('1', '') . '<!>\s\+\)\=\%(void\|\%(b\%(oolean\|yte\)\|char\|short\|int\|long\|float\|double\|\%(\<\K\k*\>\.\)*\<' . s:ff.UpperCase('[$_[:upper:]]', '[^a-z0-9]') . '\k*\>\%(<\%([^(){}]\|\n\)\+[[:space:]-]\@' . s:ff.Peek('1', '') . '<!>\)\=\)\%(\[\]\)*\)\s\+' . s:ff.PeekFrom('\)\@' . s:ff.PeekFor('javaFuncDef', 120) . '<=', '') . '\<' . s:ff.LowerCase('[$_[:lower:]]', '[^A-Z0-9]') . '\k*\>\s*(/ end=/)/ skip=/\/\*.\{-}\*\/\|\/\/.*$/ contains=@javaFuncParams'
  endif
endif

if exists("g:java_highlight_debug")
  " Strings and constants
  syn match   javaDebugSpecial		contained "\\\%(u\x\x\x\x\|[0-3]\o\o\|\o\o\=\|[bstnfr"'\\]\)"
  syn region  javaDebugString		contained start=+"+ end=+"+ contains=javaDebugSpecial
  syn region  javaDebugString		contained start=+"""[ \t\x0c\r]*$+hs=e+1 end=+"""+he=s-1 contains=javaDebugSpecial,javaDebugTextBlockError

  if s:ff.IsAnyRequestedPreviewFeatureOf([430])
    " The highlight groups of java{StrTempl,Debug{,Paren,StrTempl}}\,
    " share one colour by default. Do not conflate unrelated parens.
    syn region javaDebugStrTemplEmbExp	contained matchgroup=javaDebugStrTempl start="\\{" end="}" contains=javaComment,javaLineComment,javaDebug\%(Paren\)\@!.*
    exec 'syn region javaDebugStrTempl contained start=+\%(\.[[:space:]\n]*\)\@' . s:ff.PeekFor('javaDebugStrTempl', 80) . '<="+ end=+"+ contains=javaDebugStrTemplEmbExp,javaDebugSpecial'
    exec 'syn region javaDebugStrTempl contained start=+\%(\.[[:space:]\n]*\)\@' . s:ff.PeekFor('javaDebugStrTempl', 80) . '<="""[ \t\x0c\r]*$+hs=e+1 end=+"""+he=s-1 contains=javaDebugStrTemplEmbExp,javaDebugSpecial,javaDebugTextBlockError'
    hi def link javaDebugStrTempl	Macro
  endif

  syn match   javaDebugTextBlockError	contained +"""\s*"""+
  syn match   javaDebugCharacter	contained "'[^\\]'"
  syn match   javaDebugSpecialCharacter contained "'\\.'"
  syn match   javaDebugSpecialCharacter contained "'\\''"
  syn keyword javaDebugNumber		contained 0 0l 0L
  syn match   javaDebugNumber		contained "\<\d\%(_*\d\)*\."
  syn match   javaDebugNumber		contained "\<\%(0\%([xX]\x\%(_*\x\)*\|_*\o\%(_*\o\)*\|[bB][01]\%(_*[01]\)*\)\|[1-9]\%(_*\d\)*\)[lL]\=\>"
  syn match   javaDebugNumber		contained "\%(\<\d\%(_*\d\)*\.\%(\d\%(_*\d\)*\)\=\|\.\d\%(_*\d\)*\)\%([eE][-+]\=\d\%(_*\d\)*\)\=[fFdD]\=\>"
  syn match   javaDebugNumber		contained "\<\d\%(_*\d\)*[eE][-+]\=\d\%(_*\d\)*[fFdD]\=\>"
  syn match   javaDebugNumber		contained "\<\d\%(_*\d\)*\%([eE][-+]\=\d\%(_*\d\)*\)\=[fFdD]\>"
  syn match   javaDebugNumber		contained "\<0[xX]\%(\x\%(_*\x\)*\.\=\|\%(\x\%(_*\x\)*\)\=\.\x\%(_*\x\)*\)[pP][-+]\=\d\%(_*\d\)*[fFdD]\=\>"
  syn keyword javaDebugBoolean		contained true false
  syn keyword javaDebugType		contained null this super
  syn region  javaDebugParen		contained start=+(+ end=+)+ contains=javaDebug.*,javaDebugParen

  " To make this work, define the highlighting for these groups.
  syn match javaDebug "\<System\.\%(out\|err\)\.print\%(ln\)\=\s*("me=e-1 contains=javaDebug.* nextgroup=javaDebugParen
// FIXME: What API does "p" belong to?
// syn match javaDebug "\<p\s*("me=e-1 contains=javaDebug.* nextgroup=javaDebugParen
  syn match javaDebug "\<\K\k*\.printStackTrace\s*("me=e-1 contains=javaDebug.* nextgroup=javaDebugParen
// FIXME: What API do "trace*" belong to?
// syn match javaDebug "\<trace[SL]\=\s*("me=e-1 contains=javaDebug.* nextgroup=javaDebugParen

  hi def link javaDebug			Debug
  hi def link javaDebugString		DebugString
  hi def link javaDebugTextBlockError	Error
  hi def link javaDebugType		DebugType
  hi def link javaDebugBoolean		DebugBoolean
  hi def link javaDebugNumber		Debug
  hi def link javaDebugSpecial		DebugSpecial
  hi def link javaDebugSpecialCharacter	DebugSpecial
  hi def link javaDebugCharacter	DebugString
  hi def link javaDebugParen		Debug

  hi def link DebugString		String
  hi def link DebugSpecial		Special
  hi def link DebugBoolean		Boolean
  hi def link DebugType			Type
endif

// Complement javaBlock and javaInParen for highlighting.
syn region javaBlockOther transparent matchgroup=javaBlockOtherStart start="{" end="}"

// Try not to fold top-level-type bodies under assumption that there is
// but one such body.
exec 'syn region javaBlock transparent matchgroup=javaBlockStart start="\%(^\|^\S[^:]\+\)\@' . s:ff.PeekFor('javaBlock', 120) . '<!{" end="}" fold'

// See "D.2.1 Anonymous Classes" at
// https://web.archive.org/web/20010821025330/java.sun.com/docs/books/jls/first_edition/html/1.1Update.html#12959.
if exists("g:java_mark_braces_in_parens_as_errors")
  syn match javaInParen contained "[{}]"
  hi def link javaInParen javaError
endif

// catch errors caused by wrong parenthesis
syn region  javaParenT	transparent matchgroup=javaParen start="(" end=")" contains=@javaTop,javaInParen,javaParenT1
syn region  javaParenT1 contained transparent matchgroup=javaParen1 start="(" end=")" contains=@javaTop,javaInParen,javaParenT2
syn region  javaParenT2 contained transparent matchgroup=javaParen2 start="(" end=")" contains=@javaTop,javaInParen,javaParenT
syn match   javaParenError ")"
// catch errors caused by wrong square parenthesis
syn region  javaParenT	transparent matchgroup=javaParen start="\[" end="\]" contains=@javaTop,javaParenT1
syn region  javaParenT1 contained transparent matchgroup=javaParen1 start="\[" end="\]" contains=@javaTop,javaParenT2
syn region  javaParenT2 contained transparent matchgroup=javaParen2 start="\[" end="\]" contains=@javaTop,javaParenT
syn match   javaParenError "\]"

// Lambda expressions (JLS-17, §15.27) and method reference expressions
// (JLS-17, §15.13).
if exists("g:java_highlight_functions")
  syn match javaMethodRef ":::\@!"

  if exists("g:java_highlight_signature")
    let s:ff.LambdaDef = s:ff.LeftConstant
  else
    let s:ff.LambdaDef = s:ff.RightConstant
  endif

  " Make ()-matching definitions after the parenthesis error catcher.
  "
  " Note that here and elsewhere a single-line token is used for \z,
  " with other tokens repeated as necessary, to overcome the lack of
  " support for multi-line matching with \z.
  "
  " Match: ([@A [@B ...] final] var a[, var b, ...]) ->
  "	| ([@A [@B ...] final] T[<α>][[][]] a[, T b, ...]) ->
  " Expressions interspersed with comments are not recognised.
  exec 'syn ' . s:ff.LambdaDef('region javaLambdaDef transparent matchgroup=javaLambdaDefStart start=/', 'match javaLambdaDef "') . '\k\@' . s:ff.Peek('4', '') . '<!(' . s:ff.LambdaDef('\%(', '') . '[[:space:]\n]*\%(\%(@\%(\K\k*\.\)*\K\k*\>\%((\_.\{-1,})\)\{-,1}[[:space:]\n]\+\)*\%(final[[:space:]\n]\+\)\=\%(\<\K\k*\>\.\)*\<\K\k*\>\%(<\%([^(){}]\|\n\)\+[[:space:]-]\@' . s:ff.Peek('1', '') . '<!>\)\=\%(\%(\%(\[\]\)\+\|\.\.\.\)\)\=[[:space:]\n]\+\<\K\k*\>\%(\[\]\)*\%(,[[:space:]\n]*\)\=\)\+)[[:space:]\n]*' . s:ff.LambdaDef('\z(->\)\)\@=/ end=/)[[:space:]\n]*\z1/', '->"') . ' contains=javaAnnotation,javaParamModifier,javaLambdaVarType,javaType,@javaClasses,javaGenerics,javaVarArg'
  " Match: () ->
  "	| (a[, b, ...]) ->
  exec 'syn ' . s:ff.LambdaDef('region javaLambdaDef transparent matchgroup=javaLambdaDefStart start=/', 'match javaLambdaDef "') . '\k\@' . s:ff.Peek('4', '') . '<!(' . s:ff.LambdaDef('\%(', '') . '[[:space:]\n]*\%(\<\K\k*\>\%(,[[:space:]\n]*\)\=\)*)[[:space:]\n]*' . s:ff.LambdaDef('\z(->\)\)\@=/ end=/)[[:space:]\n]*\z1/', '->"')
  " Match: a ->
  exec 'syn ' . s:ff.LambdaDef('region javaLambdaDef transparent start=/', 'match javaLambdaDef "') . '\<\K\k*\>\%(\<default\>\)\@' . s:ff.Peek('7', '') . '<!' . s:ff.LambdaDef('\%([[:space:]\n]*\z(->\)\)\@=/ matchgroup=javaLambdaDefStart end=/\z1/', '[[:space:]\n]*->"')

  syn keyword javaParamModifier contained final
  syn keyword javaLambdaVarType contained var
  hi def link javaParamModifier		javaConceptKind
  hi def link javaLambdaVarType		javaOperator
  hi def link javaLambdaDef		javaFuncDef
  hi def link javaLambdaDefStart	javaFuncDef
  hi def link javaMethodRef		javaFuncDef
  hi def link javaFuncDef		Function
endif

// The @javaTop cluster comprises non-contained Java syntax groups.
// Note that the syntax file "aidl.vim" relies on its availability.
syn cluster javaTop contains=TOP,javaTopEnumDeclaration

if !exists("g:java_minlines")
  let g:java_minlines = 10
endif

// Note that variations of a /*/ balanced comment, e.g., /*/*/, /*//*/,
// /* /*/, /*  /*/, etc., may have their rightmost /*/ part accepted
// as a comment start by ':syntax sync ccomment'; consider alternatives
// to make synchronisation start further towards file's beginning by
// bumping up g:java_minlines or issuing ':syntax sync fromstart' or
// preferring &foldmethod set to 'syntax'.
exec "syn sync ccomment javaComment minlines=" . g:java_minlines

// The default highlighting.
hi def link javaVarArg			Function
hi def link javaBranch			Conditional
hi def link javaConditional		Conditional
hi def link javaRepeat			Repeat
hi def link javaExceptions		Exception
hi def link javaAssert			Statement
hi def link javaStorageClass		StorageClass
hi def link javaMethodDecl		javaStorageClass
hi def link javaClassDecl		javaStorageClass
hi def link javaScopeDecl		javaStorageClass
hi def link javaConceptKind		javaStorageClass

hi def link javaBoolean			Boolean
hi def link javaSpecial			Special
hi def link javaSpecialError		Error
hi def link javaSpecialCharError	Error
hi def link javaString			String
hi def link javaCharacter		Character
hi def link javaSpecialChar		SpecialChar
hi def link javaNumber			Number
hi def link javaError			Error
hi def link javaError2			javaError
hi def link javaTextBlockError		Error
hi def link javaParenError		javaError
hi def link javaStatement		Statement
hi def link javaOperator		Operator
hi def link javaConstant		Constant
hi def link javaTypedef			Typedef
hi def link javaTodo			Todo
hi def link javaAnnotation		PreProc
hi def link javaAnnotationStart		javaAnnotation
hi def link javaType			Type
hi def link javaExternal		Include

hi def link javaUserLabel		Label
hi def link javaUserLabelRef		javaUserLabel
hi def link javaLabel			Label
hi def link javaLabelDefault		javaLabel
hi def link javaLabelVarType		javaOperator

hi def link javaComment			Comment
hi def link javaCommentStar		javaComment
hi def link javaLineComment		Comment
hi def link javaCommentMarkupTagAttr	javaHtmlArg
hi def link javaCommentString		javaString
hi def link javaComment2String		javaString
hi def link javaCommentCharacter	javaCharacter
hi def link javaCommentError		javaError
hi def link javaCommentStart		javaComment

hi def link javaHtmlArg			Type
hi def link javaHtmlString		String

let b:currentSyntax = "java"

if g:main_syntax == 'java'
  unlet g:main_syntax
endif

if exists("s:clear_java_ignore_html")
  unlet! s:clear_java_ignore_html g:java_ignore_html
endif

if exists("s:clear_java_ignore_markdown")
  unlet! s:clear_java_ignore_markdown g:java_ignore_markdown
endif

let b:spell_options = "contained"

 s:ff s:with_html s:with_markdown

// See ":help vim9-mix".
if !has("vim9script")
  finish
endif

if exists("g:java_foldtext_show_first_or_second_line")
  def! s:LazyPrefix(prefix: string, dashes: string, count: number): string
    return empty(prefix)
      ? printf('+-%s%3d lines: ', dashes, count)
      : prefix
  enddef

  def! s:JavaSyntaxFoldTextExpr(): string
    # Piggyback on NGETTEXT.
    const summary: string = foldtext()
    return getline(v:foldstart) !~ '/\*\+\s*$'
      ? summary
      : LazyPrefix(matchstr(summary, '^+-\+\s*\d\+\s.\{-1,}:\s'),
			v:folddashes,
			(v:foldend - v:foldstart + 1)) ..
	  getline(v:foldstart + 1)
  enddef

  setlocal foldtext=s:JavaSyntaxFoldTextExpr()
  delfunction! g:JavaSyntaxFoldTextExpr
endif
// vim: fdm=syntax sw=2 ts=8 noet sta
//}}}
