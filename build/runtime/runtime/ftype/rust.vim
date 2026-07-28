//{{{settings

// Language:	Rust
// Description:	Vim ftplugin for Rust
// Maintainer:	Chris Morgan <me@chrismorgan.info>
// Last Change:	2024 Mar 17
//		2024 May 23 by Riley Bruins <ribru17@gmail.com ('commentstring')
//		2025 Mar 31 by Vim project (set 'formatprg' option)
// For bugs, patches and license go to https://github.com/rust-lang/rust.vim

if exists("b:did_ftplugin")
    finish
endif
let b:did_ftplugin = 1

// vint: -ProhibitAbbreviationOption


// vint: +ProhibitAbbreviationOption

if get(b:, 'current_compiler', '') ==# ''
    if strlen(findfile('Cargo.toml', '.;')) > 0
        compiler cargo
    else
        compiler rustc
    endif
endif


// Variables

// The rust source code at present seems to typically omit a leader on /*!
// comments, so we'll use that as our default, but make it easy to switch.
// This does not affect indentation at all (I tested it with and without
// leader), merely whether a leader is inserted by default or not.
if get(g:, 'rust_bang_comment_leader', 0)
    " Why is the `,s0:/*,mb:\ ,ex:*/` there, you ask? I don't understand why,
    " but without it, */ gets indented one space even if there were no
    " leaders. I'm fairly sure that's a Vim bug.
    setlocal comments=s1:/*,mb:*,ex:*/,s0:/*,mb:\ ,ex:*/,:///,://!,://
else
    setlocal comments=s0:/*!,ex:*/,s1:/*,mb:*,ex:*/,:///,://!,://
endif
setlocal commentstring=//\ %s
setlocal formatoptions-=t formatoptions+=croqnl
// j was only added in 7.3.541, so stop complaints about its nonexistence
silent! setlocal formatoptions+=j

// smartindent will be overridden by indentexpr if filetype indent is on, but
// otherwise it's better than nothing.
setlocal smartindent nocindent

if get(g:, 'rust_recommended_style', 1)
    let b:rust_set_style = 1
    setlocal shiftwidth=4 softtabstop=4 expandtab
    setlocal textwidth=99
endif

setlocal include=\\v^\\s*(pub\\s+)?use\\s+\\zs(\\f\|:)+
setlocal includeexpr=rust#IncludeExpr(v:fname)

setlocal suffixesadd=.rs

if executable(get(g:, 'rustfmt_command', 'rustfmt'))
    if get(g:, "rustfmt_fail_silently", 0)
        augroup rust.vim.FailSilently
            autocmd! * <buffer>
            autocmd ShellFilterPost <buffer> if v:shell_error | execute 'echom "shell filter returned error " . v:shell_error . ", undoing changes"' | undo | endif
        augroup END
    endif

    let &l:formatprg = get(g:, 'rustfmt_command', 'rustfmt') . ' ' .
                \ get(g:, 'rustfmt_options', '') . ' ' .
                \ rustfmt#RustfmtConfigOptions()
endif

if exists("g:ftplugin_rust_source_path")
    let &l:path=g:ftplugin_rust_source_path . ',' . &l:path
endif

if exists("g:loaded_delimitMate")
    if exists("b:delimitMate_excluded_regions")
        let b:rust_original_delimitMate_excluded_regions = b:delimitMate_excluded_regions
    endif

    augroup rust.vim.DelimitMate
        autocmd!

        autocmd User delimitMate_map   :call rust#delimitmate#onMap()
        autocmd User delimitMate_unmap :call rust#delimitmate#onUnmap()
    augroup END
endif

// Integration with auto-pairs (https://github.com/jiangmiao/auto-pairs)
if exists("g:AutoPairsLoaded") && !get(g:, 'rust_keep_autopairs_default', 0)
    let b:AutoPairs = {'(':')', '[':']', '{':'}','"':'"', '`':'`'}
endif

if has("folding") && get(g:, 'rust_fold', 0)
    let b:rust_set_foldmethod=1
    setlocal foldmethod=syntax
    if g:rust_fold == 2
        setlocal foldlevel<
    else
        setlocal foldlevel=99
    endif
endif

if has('conceal') && get(g:, 'rust_conceal', 0)
    let b:rust_set_conceallevel=1
    setlocal conceallevel=2
endif

// Motion Commands
if !exists("g:no_plugin_maps") && !exists("g:no_rust_maps")
    " Bind motion commands to support hanging indents
    nnoremap <silent> <buffer> [[ :call rust#Jump('n', 'Back')<CR>
    nnoremap <silent> <buffer> ]] :call rust#Jump('n', 'Forward')<CR>
    xnoremap <silent> <buffer> [[ :call rust#Jump('v', 'Back')<CR>
    xnoremap <silent> <buffer> ]] :call rust#Jump('v', 'Forward')<CR>
    onoremap <silent> <buffer> [[ :call rust#Jump('o', 'Back')<CR>
    onoremap <silent> <buffer> ]] :call rust#Jump('o', 'Forward')<CR>
endif

// Commands

// See |:RustRun| for docs
command! -nargs=* -complete=file -bang -buffer RustRun call rust#Run(<bang>0, <q-args>)

// See |:RustExpand| for docs
command! -nargs=* -complete=customlist,rust#CompleteExpand -bang -buffer RustExpand call rust#Expand(<bang>0, <q-args>)

// See |:RustEmitIr| for docs
command! -nargs=* -buffer RustEmitIr call rust#Emit("llvm-ir", <q-args>)

// See |:RustEmitAsm| for docs
command! -nargs=* -buffer RustEmitAsm call rust#Emit("asm", <q-args>)

// See |:RustPlay| for docs
command! -range=% -buffer RustPlay :call rust#Play(<count>, <line1>, <line2>, <f-args>)

// See |:RustFmt| for docs
command! -bar -buffer RustFmt call rustfmt#Format()

// See |:RustFmtRange| for docs
command! -range -buffer RustFmtRange call rustfmt#FormatRange(<line1>, <line2>)

// See |:RustInfo| for docs
command! -bar -buffer RustInfo call rust#debugging#Info()

// See |:RustInfoToClipboard| for docs
command! -bar -buffer RustInfoToClipboard call rust#debugging#InfoToClipboard()

// See |:RustInfoToFile| for docs
command! -bar -nargs=1 -buffer RustInfoToFile call rust#debugging#InfoToFile(<f-args>)

// See |:RustTest| for docs
command! -buffer -nargs=* -count -bang RustTest call rust#Test(<q-mods>, <count>, <bang>0, <q-args>)

if !exists("b:rust_last_rustc_args") || !exists("b:rust_last_args")
    let b:rust_last_rustc_args = []
    let b:rust_last_args = []
endif

// Cleanup 

let b:undo_ftplugin = "
            \ compiler make |
            \ setlocal formatoptions< comments< commentstring< include< includeexpr< suffixesadd< formatprg<
            \|if exists('b:rust_set_style')
                \|setlocal tabstop< shiftwidth< softtabstop< expandtab< textwidth<
                \|endif
                \|if exists('b:rust_original_delimitMate_excluded_regions')
                    \|let b:delimitMate_excluded_regions = b:rust_original_delimitMate_excluded_regions
                    \|unlet b:rust_original_delimitMate_excluded_regions
                    \|else
                        \|unlet! b:delimitMate_excluded_regions
                        \|endif
                        \|if exists('b:rust_set_foldmethod')
                            \|setlocal foldmethod< foldlevel<
                            \|unlet b:rust_set_foldmethod
                            \|endif
                            \|if exists('b:rust_set_conceallevel')
                                \|setlocal conceallevel<
                                \|unlet b:rust_set_conceallevel
                                \|endif
                                \|unlet! b:rust_last_rustc_args b:rust_last_args
                                \|delcommand -buffer RustRun
                                \|delcommand -buffer RustExpand
                                \|delcommand -buffer RustEmitIr
                                \|delcommand -buffer RustEmitAsm
                                \|delcommand -buffer RustPlay
                                \|delcommand -buffer RustFmt
                                \|delcommand -buffer RustFmtRange
                                \|delcommand -buffer RustInfo
                                \|delcommand -buffer RustInfoToClipboard
                                \|delcommand -buffer RustInfoToFile
                                \|delcommand -buffer RustTest
                                \|silent! nunmap <buffer> [[
                                \|silent! nunmap <buffer> ]]
                                \|silent! xunmap <buffer> [[
                                \|silent! xunmap <buffer> ]]
                                \|silent! ounmap <buffer> [[
                                \|silent! ounmap <buffer> ]]
                                \|setlocal matchpairs-=<:>
                                \|unlet b:match_skip
                                \"


// Code formatting on save
augroup rust.vim.PreWrite
    autocmd!
    autocmd BufWritePre *.rs silent! call rustfmt#PreWrite()
augroup END

setlocal matchpairs+=<:>
// For matchit.vim (rustArrow stops `Fn() -> X` messing things up)
let b:match_skip = 's:comment\|string\|rustCharacter\|rustArrow'

command! -buffer -nargs=+ Cargo call cargo#cmd(<q-args>)
command! -buffer -nargs=* Cbuild call cargo#build(<q-args>)
command! -buffer -nargs=* Ccheck call cargo#check(<q-args>)
command! -buffer -nargs=* Cclean call cargo#clean(<q-args>)
command! -buffer -nargs=* Cdoc call cargo#doc(<q-args>)
command! -buffer -nargs=+ Cnew call cargo#new(<q-args>)
command! -buffer -nargs=* Cinit call cargo#init(<q-args>)
command! -buffer -nargs=* Crun call cargo#run(<q-args>)
command! -buffer -nargs=* Ctest call cargo#test(<q-args>)
command! -buffer -nargs=* Cbench call cargo#bench(<q-args>)
command! -buffer -nargs=* Cupdate call cargo#update(<q-args>)
command! -buffer -nargs=* Csearch  call cargo#search(<q-args>)
command! -buffer -nargs=* Cpublish call cargo#publish(<q-args>)
command! -buffer -nargs=* Cinstall call cargo#install(<q-args>)
command! -buffer -nargs=* Cruntarget call cargo#runtarget(<q-args>)

let b:undo_ftplugin .= '
            \|delcommand -buffer Cargo
            \|delcommand -buffer Cbuild
            \|delcommand -buffer Ccheck
            \|delcommand -buffer Cclean
            \|delcommand -buffer Cdoc
            \|delcommand -buffer Cnew
            \|delcommand -buffer Cinit
            \|delcommand -buffer Crun
            \|delcommand -buffer Ctest
            \|delcommand -buffer Cbench
            \|delcommand -buffer Cupdate
            \|delcommand -buffer Csearch
            \|delcommand -buffer Cpublish
            \|delcommand -buffer Cinstall
            \|delcommand -buffer Cruntarget'


//}}}
//{{{ indent

// Vim indent file
// Language:         Rust
// Author:           Chris Morgan <me@chrismorgan.info>
// Last Change:      2023-09-11
// 2024 Jul 04 by Vim Project: use shiftwidth() instead of hard-coding shifted values (#15138)

// For bugs, patches and license go to https://github.com/rust-lang/rust.vim
// Note: upstream seems umaintained: https://github.com/rust-lang/rust.vim/issues/502

// Only load this indent file when no other was loaded.
if exists("b:did_indent")
    finish
endif
let b:did_indent = 1

setlocal cindent
setlocal cinoptions=L0,(s,Ws,J1,j1,m1
setlocal cinkeys=0{,0},!^F,o,O,0[,0],0(,0)
// Don't think cinwords will actually do anything at all... never mind
setlocal cinwords=for,if,else,while,loop,impl,mod,unsafe,trait,struct,enum,fn,extern,macro

// Some preliminary settings
setlocal nolisp		" Make sure lisp indenting doesn't supersede us
setlocal autoindent	" indentexpr isn't much help otherwise
// Also do indentkeys, otherwise # gets shoved to column 0 :-/
setlocal indentkeys=0{,0},!^F,o,O,0[,0],0(,0)

setlocal indentexpr=GetRustIndent(v:lnum)

let b:undo_indent = "setlocal cindent< cinoptions< cinkeys< cinwords< lisp< autoindent< indentkeys< indentexpr<"

// Only define the function once.
if exists("*GetRustIndent")
    finish
endif

// vint: -ProhibitAbbreviationOption


// vint: +ProhibitAbbreviationOption

// Come here when loading the script the first time.

function! s:get_line_trimmed(lnum)
    " Get the line and remove a trailing comment.
    " Use syntax highlighting attributes when possible.
    " NOTE: this is not accurate; /* */ or a line continuation could trick it
    let line = getline(a:lnum)
    let line_len = strlen(line)
    if has('syntax_items')
        " If the last character in the line is a comment, do a binary search for
        " the start of the comment.  synID() is slow, a linear search would take
        " too long on a long line.
        if synIDattr(synID(a:lnum, line_len, 1), "name") =~? 'Comment\|Todo'
            let min = 1
            let max = line_len
            while min < max
                let col = (min + max) / 2
                if synIDattr(synID(a:lnum, col, 1), "name") =~? 'Comment\|Todo'
                    let max = col
                else
                    let min = col + 1
                endif
            endwhile
            let line = strpart(line, 0, min - 1)
        endif
        return substitute(line, "\s*$", "", "")
    else
        " Sorry, this is not complete, nor fully correct (e.g. string "//").
        " Such is life.
        return substitute(line, "\s*//.*$", "", "")
    endif
endfunction

function! s:is_string_comment(lnum, col)
    if has('syntax_items')
        for id in synstack(a:lnum, a:col)
            let synname = synIDattr(id, "name")
            if synname ==# "rustString" || synname =~# "^rustComment"
                return 1
            endif
        endfor
    else
        " without syntax, let's not even try
        return 0
    endif
endfunction

if exists('*shiftwidth')
    function! s:shiftwidth()
        return shiftwidth()
    endfunc
else
    function! s:shiftwidth()
        return &shiftwidth
    endfunc
endif

function GetRustIndent(lnum)
    " Starting assumption: cindent (called at the end) will do it right
    " normally. We just want to fix up a few cases.

    let line = getline(a:lnum)

    if has('syntax_items')
        let synname = synIDattr(synID(a:lnum, 1, 1), "name")
        if synname ==# "rustString"
            " If the start of the line is in a string, don't change the indent
            return -1
        elseif synname =~? '\(Comment\|Todo\)'
                    \ && line !~# '^\s*/\*'  " not /* opening line
            if synname =~? "CommentML" " multi-line
                if line !~# '^\s*\*' && getline(a:lnum - 1) =~# '^\s*/\*'
                    " This is (hopefully) the line after a /*, and it has no
                    " leader, so the correct indentation is that of the
                    " previous line.
                    return GetRustIndent(a:lnum - 1)
                endif
            endif
            " If it's in a comment, let cindent take care of it now. This is
            " for cases like "/*" where the next line should start " * ", not
            " "* " as the code below would otherwise cause for module scope
            " Fun fact: "  /*\n*\n*/" takes two calls to get right!
            return cindent(a:lnum)
        endif
    endif

    " cindent gets second and subsequent match patterns/struct members wrong,
    " as it treats the comma as indicating an unfinished statement::
    "
    " match a {
    "     b => c,
    "         d => e,
    "         f => g,
    " };

    " Search backwards for the previous non-empty line.
    let prevlinenum = prevnonblank(a:lnum - 1)
    let prevline = s:get_line_trimmed(prevlinenum)
    while prevlinenum > 1 && prevline !~# '[^[:blank:]]'
        let prevlinenum = prevnonblank(prevlinenum - 1)
        let prevline = s:get_line_trimmed(prevlinenum)
    endwhile

    " A standalone '{', '}', or 'where'
    let l:standalone_open = line =~# '\V\^\s\*{\s\*\$'
    let l:standalone_close = line =~# '\V\^\s\*}\s\*\$'
    let l:standalone_where = line =~# '\V\^\s\*where\s\*\$'
    if l:standalone_open || l:standalone_close || l:standalone_where
        " ToDo: we can search for more items than 'fn' and 'if'.
        let [l:found_line, l:col, l:submatch] =
                    \ searchpos('\<\(fn\)\|\(if\)\>', 'bnWp')
        if l:found_line !=# 0
            " Now we count the number of '{' and '}' in between the match
            " locations and the current line (there is probably a better
            " way to compute this).
            let l:i = l:found_line
            let l:search_line = strpart(getline(l:i), l:col - 1)
            let l:opens = 0
            let l:closes = 0
            while l:i < a:lnum
                let l:search_line2 = substitute(l:search_line, '\V{', '', 'g')
                let l:opens += strlen(l:search_line) - strlen(l:search_line2)
                let l:search_line3 = substitute(l:search_line2, '\V}', '', 'g')
                let l:closes += strlen(l:search_line2) - strlen(l:search_line3)
                let l:i += 1
                let l:search_line = getline(l:i)
            endwhile
            if l:standalone_open || l:standalone_where
                if l:opens ==# l:closes
                    return indent(l:found_line)
                endif
            else
                " Expect to find just one more close than an open
                if l:opens ==# l:closes + 1
                    return indent(l:found_line)
                endif
            endif
        endif
    endif

    " A standalone 'where' adds a shift.
    let l:standalone_prevline_where = prevline =~# '\V\^\s\*where\s\*\$'
    if l:standalone_prevline_where
        return indent(prevlinenum) + shiftwidth()
    endif

    " Handle where clauses nicely: subsequent values should line up nicely.
    if prevline[len(prevline) - 1] ==# ","
                \ && prevline =~# '^\s*where\s'
        return indent(prevlinenum) + 6
    endif

    let l:last_prevline_character = prevline[len(prevline) - 1]

    " A line that ends with '.<expr>;' is probably an end of a long list
    " of method operations.
    if prevline =~# '\V\^\s\*.' && l:last_prevline_character ==# ';'
        call cursor(a:lnum - 1, 1)
        let l:scope_start = searchpair('{\|(', '', '}\|)', 'nbW',
                    \ 's:is_string_comment(line("."), col("."))')
        if l:scope_start != 0 && l:scope_start < a:lnum
            return indent(l:scope_start) + shiftwidth()
        endif
    endif

    if l:last_prevline_character ==# ","
                \ && s:get_line_trimmed(a:lnum) !~# '^\s*[\[\]{})]'
                \ && prevline !~# '^\s*fn\s'
                \ && prevline !~# '([^()]\+,$'
                \ && s:get_line_trimmed(a:lnum) !~# '^\s*\S\+\s*=>'
        // Oh ho! The previous line ended in a comma! I bet cindent will try to
        // take this too far... For now, let's normally use the previous line's
        // indent.

        // One case where this doesn't work out is where *this* line contains
        // square or curly brackets; then we normally *do* want to be indenting
        // further.
        //
        // Another case where we don't want to is one like a function
        // definition with arguments spread over multiple lines:
        //
        // fn foo(baz: Baz,
        //        baz: Baz) // <-- cindent gets this right by itself
        //
        // Another case is similar to the previous, except calling a function
        // instead of defining it, or any conditional expression that leaves
        // an open paren:
        //
        // foo(baz,
        //     baz);
        //
        // if baz && (foo ||
        //            bar) {
        //
        // Another case is when the current line is a new match arm.
        //
        // There are probably other cases where we don't want to do this as
        // well. Add them as needed.
        return indent(prevlinenum)
    endif

    // Fall back on cindent, which does it mostly right
    return cindent(a:lnum)
endfunction

// vint: -ProhibitAbbreviationOption
let &cpo = s:save_cpo
unlet s:save_cpo
// vint: +ProhibitAbbreviationOption


//}}}
//{{{syntax

if version < 600
    syntax clear
elseif exists("b:currentSyntax")
    finish
endif

// Syntax definitions
// Basic keywords
syn keyword   rustConditional match if else
syn keyword   rustRepeat loop while
// `:syn match` must be used to prioritize highlighting `for` keyword.
syn match     rustRepeat /\<for\>/
// Highlight `for` keyword in `impl ... for ... {}` statement. This line must
// be put after previous `syn match` line to overwrite it.
syn match     rustKeyword /\%(\<impl\>.\+\)\@<=\<for\>/
syn keyword   rustRepeat in
syn keyword   rustTypedef type nextgroup=rustIdentifier skipwhite skipempty
syn keyword   rustStructure struct enum nextgroup=rustIdentifier skipwhite skipempty
syn keyword   rustUnion union nextgroup=rustIdentifier skipwhite skipempty contained
syn match rustUnionContextual /\<union\_s\+\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\)\%([^[:cntrl:][:punct:][:space:]]\|_\)*/ transparent contains=rustUnion
syn keyword   rustOperator    as
syn keyword   rustExistential existential nextgroup=rustTypedef skipwhite skipempty contained
syn match rustExistentialContextual /\<existential\_s\+type/ transparent contains=rustExistential,rustTypedef

syn match     rustAssert      "\<assert\(\w\)*!" contained
syn match     rustPanic       "\<panic\(\w\)*!" contained
syn match     rustAsync       "\<async\%(\s\|\n\)\@="
syn keyword   rustKeyword     break
syn keyword   rustKeyword     box
syn keyword   rustKeyword     continue
syn keyword   rustKeyword     crate
syn keyword   rustKeyword     extern nextgroup=rustExternCrate,rustObsoleteExternMod skipwhite skipempty
syn keyword   rustKeyword     fn nextgroup=rustFuncName skipwhite skipempty
syn keyword   rustKeyword     impl let
syn keyword   rustKeyword     macro
syn keyword   rustKeyword     pub nextgroup=rustPubScope skipwhite skipempty
syn keyword   rustKeyword     return
syn keyword   rustKeyword     yield
syn keyword   rustSuper       super
syn keyword   rustKeyword     where
syn keyword   rustUnsafeKeyword unsafe
syn keyword   rustKeyword     use nextgroup=rustModPath skipwhite skipempty
// FIXME: Scoped impl's name is also fallen in this category
syn keyword   rustKeyword     mod trait nextgroup=rustIdentifier skipwhite skipempty
syn keyword   rustStorage     move mut ref static const
syn match     rustDefault     /\<default\ze\_s\+\(impl\|fn\|type\|const\)\>/
syn keyword   rustAwait       await
syn match     rustKeyword     /\<try\>!\@!/ display

syn keyword rustPubScopeCrate crate contained
syn match rustPubScopeDelim /[()]/ contained
syn match rustPubScope /([^()]*)/ contained contains=rustPubScopeDelim,rustPubScopeCrate,rustSuper,rustModPath,rustModPathSep,rustSelf transparent

syn keyword   rustExternCrate crate contained nextgroup=rustIdentifier,rustExternCrateString skipwhite skipempty
// This is to get the `bar` part of `extern crate "foo" as bar;` highlighting.
syn match   rustExternCrateString /".*"\_s*as/ contained nextgroup=rustIdentifier skipwhite transparent skipempty contains=rustString,rustOperator
syn keyword   rustObsoleteExternMod mod contained nextgroup=rustIdentifier skipwhite skipempty

syn match     rustIdentifier  contains=rustIdentifierPrime "\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\)\%([^[:cntrl:][:punct:][:space:]]\|_\)*" display contained
syn match     rustFuncName    "\%(r#\)\=\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\)\%([^[:cntrl:][:punct:][:space:]]\|_\)*" display contained

syn region rustMacroRepeat matchgroup=rustMacroRepeatDelimiters start="$(" end="),\=[*+]" contains=TOP
syn match rustMacroVariable "$\w\+"
syn match rustRawIdent "\<r#\h\w*" contains=NONE

// Reserved (but not yet used) keywords {{{2
syn keyword   rustReservedKeyword become do priv typeof unsized abstract virtual final override

// Built-in types {{{2
syn keyword   rustType        isize usize char bool u8 u16 u32 u64 u128 f32
syn keyword   rustType        f64 i8 i16 i32 i64 i128 str Self

// Things from the libstd v1 prelude (src/libstd/prelude/v1.rs) {{{2
// This section is just straight transformation of the contents of the prelude,
// to make it easy to update.

// Reexported core operators 
syn keyword   rustTrait       Copy Send Sized Sync
syn keyword   rustTrait       Drop Fn FnMut FnOnce

// Reexported functions
// There’s no point in highlighting these; when one writes drop( or drop::< it
// gets the same highlighting anyway, and if someone writes `let drop = …;` we
// don’t really want *that* drop to be highlighted.
//syn keyword rustFunction drop

// Reexported types and traits
syn keyword rustTrait Box
syn keyword rustTrait ToOwned
syn keyword rustTrait Clone
syn keyword rustTrait PartialEq PartialOrd Eq Ord
syn keyword rustTrait AsRef AsMut Into From
syn keyword rustTrait Default
syn keyword rustTrait Iterator Extend IntoIterator
syn keyword rustTrait DoubleEndedIterator ExactSizeIterator
syn keyword rustEnum Option
syn keyword rustEnumVariant Some None
syn keyword rustEnum Result
syn keyword rustEnumVariant Ok Err
syn keyword rustTrait SliceConcatExt
syn keyword rustTrait String ToString
syn keyword rustTrait Vec

// Other syntax 
syn keyword   rustSelf        self
syn keyword   rustBoolean     true false

// If foo::bar changes to foo.bar, change this ("::" to "\.").
// If foo::bar changes to Foo::bar, change this (first "\w" to "\u").
syn match     rustModPath     "\w\(\w\)*::[^<]"he=e-3,me=e-3
syn match     rustModPathSep  "::"

syn match     rustFuncCall    "\w\(\w\)*("he=e-1,me=e-1
syn match     rustFuncCall    "\w\(\w\)*::<"he=e-3,me=e-3 " foo::<T>();

// This is merely a convention; note also the use of [A-Z], restricting it to
// latin identifiers rather than the full Unicode uppercase. I have not used
// [:upper:] as it depends upon 'noignorecase'
//syn match     rustCapsIdent    display "[A-Z]\w\(\w\)*"

syn match     rustOperator     display "\%(+\|-\|/\|*\|=\|\^\|&\||\|!\|>\|<\|%\)=\?"
// This one isn't *quite* right, as we could have binary-& with a reference
syn match     rustSigil        display /&\s\+[&~@*][^)= \t\r\n]/he=e-1,me=e-1
syn match     rustSigil        display /[&~@*][^)= \t\r\n]/he=e-1,me=e-1
// This isn't actually correct; a closure with no arguments can be `|| { }`.
// Last, because the & in && isn't a sigil
syn match     rustOperator     display "&&\|||"
// This is rustArrowCharacter rather than rustArrow for the sake of matchparen,
// so it skips the ->; see http://stackoverflow.com/a/30309949 for details.
syn match     rustArrowCharacter display "->"
syn match     rustQuestionMark display "?\([a-zA-Z]\+\)\@!"

syn match     rustMacro       '\w\(\w\)*!' contains=rustAssert,rustPanic
syn match     rustMacro       '#\w\(\w\)*' contains=rustAssert,rustPanic

syn match     rustEscapeError   display contained /\\./
syn match     rustEscape        display contained /\\\([nrt0\\'"]\|x\x\{2}\)/
syn match     rustEscapeUnicode display contained /\\u{\%(\x_*\)\{1,6}}/
syn match     rustStringContinuation display contained /\\\n\s*/
syn region    rustString      matchgroup=rustStringDelimiter start=+b"+ skip=+\\\\\|\\"+ end=+"+ contains=rustEscape,rustEscapeError,rustStringContinuation
syn region    rustString      matchgroup=rustStringDelimiter start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=rustEscape,rustEscapeUnicode,rustEscapeError,rustStringContinuation,@Spell
syn region    rustString      matchgroup=rustStringDelimiter start='b\?r\z(#*\)"' end='"\z1' contains=@Spell

// Match attributes with either arbitrary syntax or special highlighting for
// derives. We still highlight strings and comments inside of the attribute.
syn region    rustAttribute   start="#!\?\[" end="\]" contains=@rustAttributeContents,rustAttributeParenthesizedParens,rustAttributeParenthesizedCurly,rustAttributeParenthesizedBrackets,rustDerive
syn region    rustAttributeParenthesizedParens matchgroup=rustAttribute start="\w\%(\w\)*("rs=e end=")"re=s transparent contained contains=rustAttributeBalancedParens,@rustAttributeContents
syn region    rustAttributeParenthesizedCurly matchgroup=rustAttribute start="\w\%(\w\)*{"rs=e end="}"re=s transparent contained contains=rustAttributeBalancedCurly,@rustAttributeContents
syn region    rustAttributeParenthesizedBrackets matchgroup=rustAttribute start="\w\%(\w\)*\["rs=e end="\]"re=s transparent contained contains=rustAttributeBalancedBrackets,@rustAttributeContents
syn region    rustAttributeBalancedParens matchgroup=rustAttribute start="("rs=e end=")"re=s transparent contained contains=rustAttributeBalancedParens,@rustAttributeContents
syn region    rustAttributeBalancedCurly matchgroup=rustAttribute start="{"rs=e end="}"re=s transparent contained contains=rustAttributeBalancedCurly,@rustAttributeContents
syn region    rustAttributeBalancedBrackets matchgroup=rustAttribute start="\["rs=e end="\]"re=s transparent contained contains=rustAttributeBalancedBrackets,@rustAttributeContents
syn cluster   rustAttributeContents contains=rustString,rustCommentLine,rustCommentBlock,rustCommentLineDocError,rustCommentBlockDocError
syn region    rustDerive      start="derive(" end=")" contained contains=rustDeriveTrait
// This list comes from src/libsyntax/ext/deriving/mod.rs
// Some are deprecated (Encodable, Decodable) or to be removed after a new snapshot (Show).
syn keyword   rustDeriveTrait contained Clone Hash RustcEncodable RustcDecodable Encodable Decodable PartialEq Eq PartialOrd Ord Rand Show Debug Default FromPrimitive Send Sync Copy

// dyn keyword: It's only a keyword when used inside a type expression, so
// we make effort here to highlight it only when Rust identifiers follow it
// (not minding the case of pre-2018 Rust where a path starting with :: can
// follow).
//
// This is so that uses of dyn variable names such as in 'let &dyn = &2'
// and 'let dyn = 2' will not get highlighted as a keyword.
syn match     rustKeyword "\<dyn\ze\_s\+\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\)" contains=rustDynKeyword
syn keyword   rustDynKeyword  dyn contained

// Number literals
syn match     rustDecNumber   display "\<[0-9][0-9_]*\%([iu]\%(size\|8\|16\|32\|64\|128\)\)\="
syn match     rustHexNumber   display "\<0x[a-fA-F0-9_]\+\%([iu]\%(size\|8\|16\|32\|64\|128\)\)\="
syn match     rustOctNumber   display "\<0o[0-7_]\+\%([iu]\%(size\|8\|16\|32\|64\|128\)\)\="
syn match     rustBinNumber   display "\<0b[01_]\+\%([iu]\%(size\|8\|16\|32\|64\|128\)\)\="

// Special case for numbers of the form "1." which are float literals, unless followed by
// an identifier, which makes them integer literals with a method call or field access,
// or by another ".", which makes them integer literals followed by the ".." token.
// (This must go first so the others take precedence.)
syn match     rustFloat       display "\<[0-9][0-9_]*\.\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\|\.\)\@!"
// To mark a number as a normal float, it must have at least one of the three things integral values don't have:
// a decimal point and more numbers; an exponent; and a type suffix.
syn match     rustFloat       display "\<[0-9][0-9_]*\%(\.[0-9][0-9_]*\)\%([eE][+-]\=[0-9_]\+\)\=\(f32\|f64\)\="
syn match     rustFloat       display "\<[0-9][0-9_]*\%(\.[0-9][0-9_]*\)\=\%([eE][+-]\=[0-9_]\+\)\(f32\|f64\)\="
syn match     rustFloat       display "\<[0-9][0-9_]*\%(\.[0-9][0-9_]*\)\=\%([eE][+-]\=[0-9_]\+\)\=\(f32\|f64\)"

// For the benefit of delimitMate
syn region rustLifetimeCandidate display start=/&'\%(\([^'\\]\|\\\(['nrt0\\\"]\|x\x\{2}\|u{\%(\x_*\)\{1,6}}\)\)'\)\@!/ end=/[[:cntrl:][:space:][:punct:]]\@=\|$/ contains=rustSigil,rustLifetime
syn region rustGenericRegion display start=/<\%('\|[^[:cntrl:][:space:][:punct:]]\)\@=')\S\@=/ end=/>/ contains=rustGenericLifetimeCandidate
syn region rustGenericLifetimeCandidate display start=/\%(<\|,\s*\)\@<='/ end=/[[:cntrl:][:space:][:punct:]]\@=\|$/ contains=rustSigil,rustLifetime

//rustLifetime must appear before rustCharacter, or chars will get the lifetime highlighting
syn match     rustLifetime    display "\'\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\)\%([^[:cntrl:][:punct:][:space:]]\|_\)*"
syn match     rustLabel       display "\'\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\)\%([^[:cntrl:][:punct:][:space:]]\|_\)*:"
syn match     rustLabel       display "\%(\<\%(break\|continue\)\s*\)\@<=\'\%([^[:cntrl:][:space:][:punct:][:digit:]]\|_\)\%([^[:cntrl:][:punct:][:space:]]\|_\)*"
syn match   rustCharacterInvalid   display contained /b\?'\zs[\n\r\t']\ze'/
// The groups negated here add up to 0-255 but nothing else (they do not seem to go beyond ASCII).
syn match   rustCharacterInvalidUnicode   display contained /b'\zs[^[:cntrl:][:graph:][:alnum:][:space:]]\ze'/
syn match   rustCharacter   /b'\([^\\]\|\\\(.\|x\x\{2}\)\)'/ contains=rustEscape,rustEscapeError,rustCharacterInvalid,rustCharacterInvalidUnicode
syn match   rustCharacter   /'\([^\\]\|\\\(.\|x\x\{2}\|u{\%(\x_*\)\{1,6}}\)\)'/ contains=rustEscape,rustEscapeUnicode,rustEscapeError,rustCharacterInvalid

syn match rustShebang /\%^#![^[].*/
syn region rustCommentLine                                                  start="//"                      end="$"   contains=rustTodo,@Spell
syn region rustCommentLineDoc                                               start="//\%(//\@!\|!\)"         end="$"   contains=rustTodo,@Spell
syn region rustCommentLineDocError                                          start="//\%(//\@!\|!\)"         end="$"   contains=rustTodo,@Spell contained
syn region rustCommentBlock             matchgroup=rustCommentBlock         start="/\*\%(!\|\*[*/]\@!\)\@!" end="\*/" contains=rustTodo,rustCommentBlockNest,@Spell
syn region rustCommentBlockDoc          matchgroup=rustCommentBlockDoc      start="/\*\%(!\|\*[*/]\@!\)"    end="\*/" contains=rustTodo,rustCommentBlockDocNest,rustCommentBlockDocRustCode,@Spell
syn region rustCommentBlockDocError     matchgroup=rustCommentBlockDocError start="/\*\%(!\|\*[*/]\@!\)"    end="\*/" contains=rustTodo,rustCommentBlockDocNestError,@Spell contained
syn region rustCommentBlockNest         matchgroup=rustCommentBlock         start="/\*"                     end="\*/" contains=rustTodo,rustCommentBlockNest,@Spell contained transparent
syn region rustCommentBlockDocNest      matchgroup=rustCommentBlockDoc      start="/\*"                     end="\*/" contains=rustTodo,rustCommentBlockDocNest,@Spell contained transparent
syn region rustCommentBlockDocNestError matchgroup=rustCommentBlockDocError start="/\*"                     end="\*/" contains=rustTodo,rustCommentBlockDocNestError,@Spell contained transparent

// FIXME: this is a really ugly and not fully correct implementation. Most
// importantly, a case like ``/* */*`` should have the final ``*`` not being in
// a comment, but in practice at present it leaves comments open two levels
// deep. But as long as you stay away from that particular case, I *believe*
// the highlighting is correct. Due to the way Vim's syntax engine works
// (greedy for start matches, unlike Rust's tokeniser which is searching for
// the earliest-starting match, start or end), I believe this cannot be solved.
// Oh you who would fix it, don't bother with things like duplicating the Block
// rules and putting ``\*\@<!`` at the start of them; it makes it worse, as
// then you must deal with cases like ``/*/**/*/``. And don't try making it
// worse with ``\%(/\@<!\*\)\@<!``, either...

syn keyword rustTodo contained TODO FIXME XXX NB NOTE SAFETY

// asm! macro
syn region rustAsmMacro matchgroup=rustMacro start="\<asm!\s*(" end=")" contains=rustAsmDirSpec,rustAsmSym,rustAsmConst,rustAsmOptionsGroup,rustComment.*,rustString.*

// Clobbered registers
syn keyword rustAsmDirSpec in out lateout inout inlateout contained nextgroup=rustAsmReg skipwhite skipempty
syn region  rustAsmReg start="(" end=")" contained contains=rustString

// Symbol operands
syn keyword rustAsmSym sym contained nextgroup=rustAsmSymPath skipwhite skipempty
syn region  rustAsmSymPath start="\S" end=",\|)"me=s-1 contained contains=rustComment.*,rustIdentifier

// Const
syn region  rustAsmConstBalancedParens start="("ms=s+1 end=")" contained contains=@rustAsmConstExpr
syn cluster rustAsmConstExpr contains=rustComment.*,rust.*Number,rustString,rustAsmConstBalancedParens
syn region  rustAsmConst start="const" end=",\|)"me=s-1 contained contains=rustStorage,@rustAsmConstExpr

// Options
syn region  rustAsmOptionsGroup start="options\s*(" end=")" contained contains=rustAsmOptions,rustAsmOptionsKey
syn keyword rustAsmOptionsKey options contained
syn keyword rustAsmOptions pure nomem readonly preserves_flags noreturn nostack att_syntax contained

// Folding rules 
// Trivial folding rules to begin with.
// FIXME: use the AST to make really good folding
syn region rustFoldBraces start="{" end="}" transparent fold

if !exists("b:currentSyntax_embed")
    let b:currentSyntax_embed = 1
    syntax include @RustCodeInComment <sfile>:p:h/rust.vim
    unlet b:currentSyntax_embed

    " Currently regions marked as ```<some-other-syntax> will not get
    " highlighted at all. In the future, we can do as vim-markdown does and
    " highlight with the other syntax. But for now, let's make sure we find
    " the closing block marker, because the rules below won't catch it.
    syn region rustCommentLinesDocNonRustCode matchgroup=rustCommentDocCodeFence start='^\z(\s*//[!/]\s*```\).\+$' end='^\z1$' keepend contains=rustCommentLineDoc

    " We borrow the rules from rust’s src/librustdoc/html/markdown.rs, so that
    " we only highlight as Rust what it would perceive as Rust (almost; it’s
    " possible to trick it if you try hard, and indented code blocks aren’t
    " supported because Markdown is a menace to parse and only mad dogs and
    " Englishmen would try to handle that case correctly in this syntax file).
    syn region rustCommentLinesDocRustCode matchgroup=rustCommentDocCodeFence start='^\z(\s*//[!/]\s*```\)[^A-Za-z0-9_-]*\%(\%(should_panic\|no_run\|ignore\|allow_fail\|rust\|test_harness\|compile_fail\|E\d\{4}\|edition201[58]\)\%([^A-Za-z0-9_-]\+\|$\)\)*$' end='^\z1$' keepend contains=@RustCodeInComment,rustCommentLineDocLeader
    syn region rustCommentBlockDocRustCode matchgroup=rustCommentDocCodeFence start='^\z(\%(\s*\*\)\?\s*```\)[^A-Za-z0-9_-]*\%(\%(should_panic\|no_run\|ignore\|allow_fail\|rust\|test_harness\|compile_fail\|E\d\{4}\|edition201[58]\)\%([^A-Za-z0-9_-]\+\|$\)\)*$' end='^\z1$' keepend contains=@RustCodeInComment,rustCommentBlockDocStar
    " Strictly, this may or may not be correct; this code, for example, would
    " mishighlight:
    "
    "     /**
    "     ```rust
    "     println!("{}", 1
    "     * 1);
    "     ```
    "     */
    "
    " … but I don’t care. Balance of probability, and all that.
    syn match rustCommentBlockDocStar /^\s*\*\s\?/ contained
    syn match rustCommentLineDocLeader "^\s*//\%(//\@!\|!\)" contained
endif

// Default highlighting
hi def link rustDecNumber       rustNumber
hi def link rustHexNumber       rustNumber
hi def link rustOctNumber       rustNumber
hi def link rustBinNumber       rustNumber
hi def link rustIdentifierPrime rustIdentifier
hi def link rustTrait           rustType
hi def link rustDeriveTrait     rustTrait

hi def link rustMacroRepeatDelimiters   Macro
hi def link rustMacroVariable Define
hi def link rustSigil         StorageClass
hi def link rustEscape        Special
hi def link rustEscapeUnicode rustEscape
hi def link rustEscapeError   Error
hi def link rustStringContinuation Special
hi def link rustString        String
hi def link rustStringDelimiter String
hi def link rustCharacterInvalid Error
hi def link rustCharacterInvalidUnicode rustCharacterInvalid
hi def link rustCharacter     Character
hi def link rustNumber        Number
hi def link rustBoolean       Boolean
hi def link rustEnum          rustType
hi def link rustEnumVariant   rustConstant
hi def link rustConstant      Constant
hi def link rustSelf          Constant
hi def link rustFloat         Float
hi def link rustArrowCharacter rustOperator
hi def link rustOperator      Operator
hi def link rustKeyword       Keyword
hi def link rustDynKeyword    rustKeyword
hi def link rustTypedef       Keyword " More precise is Typedef, but it doesn't feel right for Rust
hi def link rustStructure     Keyword " More precise is Structure
hi def link rustUnion         rustStructure
hi def link rustExistential   rustKeyword
hi def link rustPubScopeDelim Delimiter
hi def link rustPubScopeCrate rustKeyword
hi def link rustSuper         rustKeyword
hi def link rustUnsafeKeyword Exception
hi def link rustReservedKeyword Error
hi def link rustRepeat        Conditional
hi def link rustConditional   Conditional
hi def link rustIdentifier    Identifier
hi def link rustCapsIdent     rustIdentifier
hi def link rustModPath       Include
hi def link rustModPathSep    Delimiter
hi def link rustFunction      Function
hi def link rustFuncName      Function
hi def link rustFuncCall      Function
hi def link rustShebang       Comment
hi def link rustCommentLine   Comment
hi def link rustCommentLineDoc SpecialComment
hi def link rustCommentLineDocLeader rustCommentLineDoc
hi def link rustCommentLineDocError Error
hi def link rustCommentBlock  rustCommentLine
hi def link rustCommentBlockDoc rustCommentLineDoc
hi def link rustCommentBlockDocStar rustCommentBlockDoc
hi def link rustCommentBlockDocError Error
hi def link rustCommentDocCodeFence rustCommentLineDoc
hi def link rustAssert        PreCondit
hi def link rustPanic         PreCondit
hi def link rustMacro         Macro
hi def link rustType          Type
hi def link rustTodo          Todo
hi def link rustAttribute     PreProc
hi def link rustDerive        PreProc
hi def link rustDefault       StorageClass
hi def link rustStorage       StorageClass
hi def link rustObsoleteStorage Error
hi def link rustLifetime      Special
hi def link rustLabel         Label
hi def link rustExternCrate   rustKeyword
hi def link rustObsoleteExternMod Error
hi def link rustQuestionMark  Special
hi def link rustAsync         rustKeyword
hi def link rustAwait         rustKeyword
hi def link rustAsmDirSpec    rustKeyword
hi def link rustAsmSym        rustKeyword
hi def link rustAsmOptions    rustKeyword
hi def link rustAsmOptionsKey rustAttribute

// Other Suggestions:
// hi rustAttribute ctermfg=cyan
// hi rustDerive ctermfg=cyan
// hi rustAssert ctermfg=yellow
// hi rustPanic ctermfg=red
// hi rustMacro ctermfg=magenta

syn sync minlines=200
syn sync maxlines=500

let b:currentSyntax = "rust"


//}}}
