" Vim compiler file
" Compiler:	JSHint
" Maintainer:	Doug Kearns <dougkearns@gmail.com>
" Last Change:	2024 Apr 03

if exists("current_compiler")
  finish
endif
let current_compiler = "jshint"




" CompilerSet makeprg=npx\ jshint\ --verbose

CompilerSet makeprg=jshint\ --verbose
CompilerSet errorformat=%f:\ line\ %l\\,\ col\ %c\\,\ %m\ (%t%n),
		       \%-G%.%#

let &cpo = s:cpo_save
unlet s:cpo_save
