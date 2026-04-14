# Contributing to Eegl

Patches are welcome in whatever form.
Discussions about patches happen on the [vim-dev][0] mailing list.
If you create a pull request on GitHub it will be
forwarded to the vim-dev mailing list. You can also send your patch there
directly (but please note, the initial posting is subject to moderation).
In that case an attachment with a unified diff format is preferred.
Information about the mailing list can be found [on the Vim website][0]

A pull request has the advantage that it will trigger the Continuous
Integration tests, you will be warned of problems (you can ignore the coverage
warning, it's noisy).

Please consider adding a test. All new functionality should be tested and bug
fixes should be tested for regressions: the test should fail before the fix and
pass after the fix. Look through recent patches for examples and find help
with ":help testing". The tests are located under "src/testdir".

Contributions will be distributed with Vim under the Vim license. Providing a
change to be included implies that you agree with this and your contribution
does not cause us trouble with trademarks or patents. There is no CLA to sign.


## Source code layout

Entrypoint is in main.c.

Actions (the keys you enter in normal and visual modes) are declared in actions.h and implemented
mostly in normal.c.

Commands (the things you enter into the command line like `:vimgrep`) are declared in commands.h
and implemented mostly in normal.c

The expression-evaluating core is in eval.c

Processing keys from user is done mostly in getchar.c

Code folds are defined in fold.c

Quickfix and location lists are defined in quickfix.c

Low-level memory work is in memory.c

Options like `runtimepath` are declared in optiondefs.h

Helpful macros are defined in eegl.h searchable via @@@macros

Global structs are defined in eegl.h searchable via "@@@structs"

Generic types are defined in eegl.h searchable via @@@generics

Generic functions are defined in generic.h


## Source file descriptions (regenerate with ```grep '//##' -h *.c```)

autocmd.c: autocommand-related functions
buffer.c: functions for dealing with the buffer structure (i.e. open files)
channel.c: implements communication through a socket or any file handle, plus logging
clipboard.c: Functions to handle the clipboard and copy-and-paste registers
data.c: core data structures
diff.c: code for diffing two, three or four buffers.
do.c: functions for executing Commands
draw.c: drawing text lines to the screen 
eval.c: evaluation of functions
fileio.c: read from and write to a file
fold.c: code for text folding
getchar.c: Code related to getting a character from the user or scripts, redo & stuff buffers
hilite.c: hiliting text
indent.c: indentation-related functions
insert.c: functions for Insert mode (manual input of text)
location.c: functions for location lists (searches, errors from compilation, help greps etc)
main.c: the entrypoint of Em
map.c: Code for mappings and abbreviations.
mark.c: functions for setting marks (`ma`) and jumping to them (`'a`)
memory.c: low-level functions for managing memory, including the text
message.c: functions for displaying messages on the command line
mouse.c: mouse-handling functions
normal.c: code for actions in Normal and Visual modes. Communicates with ops.c
ops.c: low-level operations and operators for changing text
option.c: code controlling user options 
popup.c: implementation of popup portals.  See ":help popup".
portal.c: portals (views) into text for user interface
regexp.c: Handling of regular expressions: compileRegexp(), vim_regexec(), vim_regsub()
scriptfile.c: functions for dealing with the runtime directories/files and running scripts
search.c: code for normal mode searching commands and hiliting matches
session.c: session related functions. Saving and restoring IDE state to files
spell.c: spell checking 
strings.c: utility functions for string manipulation 
syntax.c: code for syntax highlighting
tag.c: Code to handle tags and the tag stack
term.c: terminal and pseudo-teletype functions
testing.c: Support for tests.
ui.c: terminal-based user interface
usercomm.c: user command line, its completion and user-defined functions
var.c: the tagged data accessible from scripts
window.c: functions for displaying the window in X11 or in Wayland

## Jumping around ##

Here are a few hints for finding your way around the source code.


First of all, use `:make tags` to generate a tags file, so that you can jump
around in the source code.

To jump to a function or variable definition, move the cursor on the name and
use the `CTRL-]` command.  Use `CTRL-T` or `CTRL-O` to jump back.

To jump to a file, move the cursor on its name and use the `gf` command.

You might also want to read
[`:help development`](http://vimdoc.sourceforge.net/htmldoc/develop.html#development).


## Debugging ##

If you have a reasonable recent version of gdb, you can use the `:Termdebug`
command to debug Vim.  See  `:help :Termdebug`.

When something is time critical or stepping through code is a hassle, use the
channel logging to create a time-stamped log file.  Add lines to the code like
this:

	ch_log(NULL, "Value is now %02x", value);

After compiling and starting Vim, do:

	:call ch_logfile('debuglog', 'w')

And edit `debuglog` to see what happens.  The channel functions already have
`ch_log()` calls, thus you always see that in the log.


## Important Variables ##

The current mode is stored in `State`.  The values it can have are `NORMAL`,
`INSERT`, `CMDLINE`, and a few others.

The current window is `curwin`.  The current buffer is `curbuf`.  These point
to structures with the cursor position in the window, option values, the file
name, etc.  These are defined in
[`structs.h`](https://github.com/vim/vim/blob/master/src/structs.h).

All the global variables are declared in
[`globals.h`](https://github.com/vim/vim/blob/master/src/globals.h).


## The main loop ##

This is conveniently called `main_loop()`.  It updates a few things and then
calls `normal_cmd()` to process a command.  This returns when the command is
finished.

The basic idea is that Vim waits for the user to type a character and
processes it until another character is needed.  Thus there are several places
where Vim waits for a character to be typed.  The `vgetc()` function is used
for this.  It also handles mapping.

Updating the screen is mostly postponed until a command or a sequence of
commands has finished.  The work is done by `update_screen()`, which calls
`win_update()` for every window, which calls `win_line()` for every line.
See the start of
[`screen.c`](https://github.com/vim/vim/blob/master/src/screen.c)
for more explanations.


## Command-line mode ##

When typing a `:`, `normal_cmd()` will call `getcmdline()` to obtain a line
with an Ex command.  `getcmdline()` contains a loop that will handle each typed
character.  It returns when hitting `CR` or `Esc` or some other character that
ends the command line mode.


## Commands ##

Commands are handled by the function `do_cmdline()`.  It does the generic
parsing of the `:` command line and calls `do_one_cmd()` for each separate
command.  It also takes care of while loops.

`do_one_cmd()` parses the range and generic arguments and puts them in the
`exarg_t` and passes it to the function that handles the command.

The `:` commands are listed in `ex_cmds.h`.  The third entry of each item is
the name of the function that handles the command.  The last entry are the
flags that are used for the command.


## Normal mode actions ##

The Normal mode actions are handled by the `normalAction()` function.  It also
handles the optional count and an extra character for some commands.  These
are passed in an `Invocation` to the function that handles the command.

There is a table `nv_cmds` in
[`normal.c`](https://github.com/vim/vim/blob/master/src/normal.c)
which lists the first character of every command.  The second entry of each
item is the name of the function that handles the command.


## Insert mode commands ##

When doing an `i` or `a` command, `normalAction()` will call the `edit()`
function. It contains a loop that waits for the next character and handles it.
It returns when leaving Insert mode.


## Options ##

There is a list with all option names in
[`option.c`](https://github.com/vim/vim/blob/master/src/option.c),
called `options[]`.


## The TUI ##

Most of the TUI code is implemented like it was a clever terminal.  Typing a
character, moving a scrollbar, clicking the mouse, etc. are all translated
into events which are written in the input buffer.  These are read by the
main code, just like reading from a terminal.  The code for this is scattered
through [`gui.c`](https://github.com/vim/vim/blob/master/src/gui.c).
For example, `gui_send_mouse_event()` for a mouse click and `gui_menu_cb()` for
a menu action.  Key hits are handled by the system-specific GUI code, which
calls `add_to_input_buf()` to send the key code.

Updating the GUI window is done by writing codes in the output buffer, just
like writing to a terminal.  When the buffer gets full or is flushed,
`gui_write()` will parse the codes and draw the appropriate items.  Finally the
system-specific GUI code will be called to do the work.


## Debugging the GUI ##

Remember to prevent that gvim forks and the debugger thinks Vim has exited,
add the `-f` argument.  In gdb: `run -f -g`.

When stepping through display updating code, the focus event is triggered
when going from the debugger to Vim and back.  To avoid this, recompile with
some code in `gui_focus_change()` disabled.


## Contributing ##

If you would like to help making Vim better, see the
[`CONTRIBUTING.md`](https://github.com/vim/vim/blob/master/CONTRIBUTING.md)
file.


This is `README.md` for version 9.1 of the Vim source code.
## Variable naming scheme

f_.. | API functions (i.e. the things which can be called from scripts)
c_.. | commands (i.e. the things which can be called from the commline like `:echo`)
globOpt...  | global variables holding current values of global options (i.e. things settable by 
   the user from the commline like `:set foldmethod=...`)
...G | internal global variables
...S | internal static variables (i.e. global within a single code module)


## Signing-off commits

While not required, it's recommended to use **Signed-off commits** to ensure
transparency, accountability, and compliance with open-source best practices.
Signed-off commits follow the [Developer Certificate of Origin (DCO)][15],
which confirms that contributors have the right to submit their changes under
the project's license. This process adds a `Signed-off-by` line to commit
messages, verifying that the contributor agrees to the project's licensing
terms. To sign off a commit, simply use the -s flag when committing:

```sh
git commit -s
```

This ensures that every contribution is properly documented and traceable,
aligning with industry standards used in projects like the Linux Kernel or
the git project. By making Signed-off commits a standard practice, we help
maintain a legally compliant and well-governed codebase while fostering trust
within our contributor community.

When merging PRs into Vim, the current maintainer @chrisbra usually adds missing
`Signed-off-by` trailers for the author user name and email address as well for
anybody that explicitly *ACK*s a pull request as a statement that those
approvers are happy with that particular change.


# Reporting issues

We use GitHub [issues][17], but that is not a requirement. Writing to the Vim
mailing list is also fine.

Please use the GitHub issues only for actual issues. If you are not 100% sure
that your problem is a Vim issue, please first discuss this on the Vim user
mailing list. Try reproducing the problem without any of your plugins or settings:

    vim --clean

If you report an issue, please describe exactly how to reproduce it.
For example, don't say "insert some text" but say what you did exactly:
`ahere is some text<Esc>`.
Ideally, the steps you list can be used to write a test to verify the problem
is fixed.

Feel free to report even the smallest problem, also typos in the documentation.

You can find known issues in the todo file: `:help todo`.
Or open [the todo file][todo list] on GitHub to see the latest version.


# Syntax, indent and other runtime files

The latest version of these files can be obtained from the repository.
They are usually not updated with numbered patches. However, they may
or may not work with older Vim releases (since they may depend on new
features).

If you find a problem with one of these files or have a suggestion for
improvement, please first try to contact the maintainer directly.
Look in the header of the file for the name, email address, github handle and/or
upstream repository. You may also check the [MAINTAINERS][11] file.

The maintainer will take care of issues and send updates to the Vim project for
distribution with Vim.

If the maintainer does not respond, contact the [vim-dev][0] mailing list or
open an [issue][17] here.

Note: Whether or not to use Vim9 script is up to the maintainer. For runtime
files maintained here, we aim to preserve compatibility with Neovim if
possible. Please wrap Vim9 script with a guard like this:
```vim
if has('vim9script')
   " use Vim9 script implementation
   [...]
endif
```

## Contributing new runtime files

If you want to contribute new runtime files for Vim or Neovim, please create a
PR with your changes against this repository here. For new filetypes, do not forget:

- to add a new [filetype test][12] (keep it similar to the other filetype tests).
- all configuration switches should be documented
  (check [filetype.txt][13] and/or [syntax.txt][14] for filetype and syntax plugins)
- add yourself as Maintainer to the top of file (again, keep the header similar to
  other runtime files)
- add yourself to the [MAINTAINERS][11] file.
- add a guard `if has('vim9script')` if you like to maintain Neovim
  compatibility but want to use Vim9 script (or restrict yourself to legacy Vim
  script)


# Translations

Translating messages and runtime files is very much appreciated! These things
can be translated:

- Messages in Vim, see [src/po/README.txt][1]
  Also used for the desktop icons.
- Menus, see [runtime/lang/README.txt][2]
- Vim tutor, see [runtime/tutor/README.txt][3]
- Manual pages, see [runtime/doc/\*.1][4] for examples
- Installer, see [nsis/lang/README.txt][5]

The help files can be translated and made available separately.
See https://www.vim.org/translations.php for examples.


# How do I contribute to the project?

Please have a look at the following [discussion][6], which should give you some
ideas. Please also check the [develop.txt][7] helpfile for the recommended
coding style. Often it's also beneficial to check the surrounding code for the style
being used.

For the recommended documentation style, please check [helphelp.txt][16].


# I have a question

If you have some question on the style guide, please contact the [vim-dev][0]
mailing list. For other questions please use the [Vi Stack Exchange][8] website, the
[vim-use][9] mailing list or make use of the [discussion][10] feature here at github.

[todo list]: https://github.com/vim/vim/blob/master/runtime/doc/todo.txt
[0]: http://www.vim.org/maillist.php#vim-dev
[1]: https://github.com/vim/vim/blob/master/src/po/README.txt
[2]: https://github.com/vim/vim/blob/master/runtime/lang/README.txt
[3]: https://github.com/vim/vim/blob/master/runtime/tutor/README.txt
[4]: https://github.com/vim/vim/blob/master/runtime/doc/vim.1
[5]: https://github.com/vim/vim/blob/master/nsis/lang/README.txt
[6]: https://github.com/vim/vim/discussions/13087
[7]: https://github.com/vim/vim/blob/master/runtime/doc/develop.txt
[8]: https://vi.stackexchange.com
[9]: http://www.vim.org/maillist.php#vim-use
[10]: https://github.com/vim/vim/discussions
[11]: https://github.com/vim/vim/blob/master/.github/MAINTAINERS
[12]: https://github.com/vim/vim/blob/master/src/testdir/test_filetype.vim
[13]: https://github.com/vim/vim/blob/master/runtime/doc/filetype.txt
[14]: https://github.com/vim/vim/blob/master/runtime/doc/syntax.txt
[15]: https://en.wikipedia.org/wiki/Developer_Certificate_of_Origin
[16]: https://github.com/vim/vim/blob/master/runtime/doc/helphelp.txt
[17]: https://github.com/vim/vim/issues
