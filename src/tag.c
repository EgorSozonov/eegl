//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## tag.c: Code to handle tags and the tag stack

#include "eegl.h"

//Pointers to various items in a tag line.
typedef struct tag_pointers {
   // filled in by parse_tag_line():
   CS tagname;   // start of tag name (skip "file:")
   CS tagname_end;   // char after tag name
   CS fname;      // first char of file name
   CS fname_end;   // char after file name
   CS command;   // first char of command filled in by parse_match():
   CS command_end;   // first char after command
   CS tag_fname;   // file name of the tags file. This is used when 'tr' is set.
   CS tagkind;   // "kind:" value
   CS tagkind_end;   // end of tagkind
   CS user_data;   // user_data string
   CS user_data_end;   // end of user_data
   LineNr c;   // "line:" value
} Tagline;

//Return values used when reading lines from a tags file.
typedef enum {
   TAGS_READ_SUCCESS = 1,
   TAGS_READ_EOF,
   TAGS_READ_IGNORE,
} tags_read_status_T;

//States used during a tags search
typedef enum {
   TS_START,      // at start of file
   TS_LINEAR,      // linear searching forward, till EOF
   TS_BINARY,      // binary searching
   TS_SKIP_BACK,   // skipping backwards
   TS_STEP_FORWARD   // stepping forwards
} tagsearch_state_T;   // Current search state

//Binary search file offsets in a tags file
typedef struct {
   FileSize   low_offset;   // offset for first char of first line that could match
   FileSize   high_offset;   // offset of char after last line that could match
   FileSize   curr_offset;   // Current file offset in search range
   FileSize   curr_offset_used; // curr_offset used when skipping back
   FileSize   match_offset;   // Where the binary search found a tag
   int   low_char;      // first char at low_offset
   int   high_char;      // first char at high_offset
} TagSearchInfo;

//Return values used when matching tags against a pattern.
typedef enum {
   TAG_MATCH_SUCCESS = 1,
   TAG_MATCH_FAIL,
   TAG_MATCH_STOP,
   TAG_MATCH_NEXT
} tagmatch_status_T;

//Arguments used for matching tags read from a tags file against a pattern.
typedef struct {
   int   matchoff;      // tag match offset
   int   match_re;      // TRUE if the tag matches a regexp
   int   match_no_ic;      // TRUE if the tag matches with case
   int   has_re;         // regular expression used
   int   sortic;         // tags file sorted ignoring case (foldcase)
   int   sort_error;      // tags file not sorted
} FindTagsMatchArgs;

//The matching tags are first stored in one of the hash tables.  In
//which one depends on the priority of the match.
//ht_match[] is used to find duplicates, ga_match[] to keep them in sequence.
//At the end, all the matches from ga_match[] are concatenated, to make a list sorted on priority.
#define MT_ST_CUR   0  // static match in current file
#define MT_GL_CUR   1  // global match in current file
#define MT_GL_OTH   2  // global match in other file
#define MT_ST_OTH   3  // static match in other file
#define MT_IC_OFF   4  // add for icase match
#define MT_RE_OFF   8  // add for regexp match
#define MT_MASK     7  // mask for printing priority
#define MT_COUNT   16

private char* mt_names[MT_COUNT/2] = {"FSC", "F C", "F  ", "FS ", " SC", "  C", "   ", " S "};

#define NOTAGFILE   99      // return value for jumpto_tag
private Byte   *nofile_fname = NULL;   // fname for NOTAGFILE error

private void taglen_advance(int l);

private int jumpto_tag(CS lbuf, int forceit, int keep_help);
private int parse_tag_line(CS lbuf, Tagline *tagp);
private int test_for_static(Tagline *);
private int parse_match(CS lbuf, Tagline *tagp);
private CS tag_full_fname(Tagline *tagp);
private CS expand_tag_fname(CS fname, CS tag_fname, int expand);
private int test_for_current(CS, CS, CS, CS);
private int find_extra(OUT CS* pp);
private void print_tag_list(int new_tag, int use_tagstack, ExpandMatch matches);
private int add_llist_tags(CS tag, ExpandMatch matches);

private Byte   *tagmatchname = NULL;   // name of last used tag

//Tag for preview window is remembered separately, to avoid messing up the normal tagstack.
private Taggy ptag_entry = {NULL, {{0, 0, 0}, 0}, 0, 0, NULL};

private int  tfu_in_use = FALSE;       // disallow recursive call of tagfunc
private Callback tfu_cb;       // 'tagfunc' callback function

// Used instead of ZERO to separate tag fields in the growarrays.
#define TAG_SEP 0x02

//Read the 'tagfunc' option value and convert that to a callback value.
//Invoked when the 'tagfunc' option is set. The option value can be a name of
//a function (string), or function(<name>) or funcref(<name>) or a lambda.
CS
did_set_tagfunc(OptionChange *cha) {
   evFreeCallback(&tfu_cb);
   evFreeCallback(curBook->o.tagFn);

   if (!curBook->o.tagFn)
      return NULL;

   if (optSetCallback(OUT &tfu_cb, cha->newVal.string) == FAIL)
      return e_invalid_argument;

   evCopyCallback(curBook->o.tagFn, &tfu_cb);

   return NULL;
}

# if defined(EXITFREE) || defined(PROTO)
void
free_tagfunc_option(void) {
   evFreeCallback(&tfu_cb);
}
# endif

//Mark the global 'tagfunc' callback with "copyID" so that it is not garbage collected.
int
set_ref_in_tagfunc(int copyID UNUSED) {
   int abort = memSetRefInCallback(&tfu_cb, copyID);

   return abort;
}

//{{{jump to tag

//Jump to tag; handling of tag commands and tag stack
//
//*tag != ZERO: ":tag {tag}", jump to new tag, add to tag stack
//
//type == DT_TAG:   ":tag [tag]", jump to newer position or same tag again
//type == DT_HELP:   like DT_TAG, but don't use regexp.
//type == DT_POP:   ":pop" or CTRL-T, jump to old position
//type == DT_NEXT:   jump to next match of same tag
//type == DT_PREV:   jump to previous match of same tag
//type == DT_FIRST:   jump to first match of same tag
//type == DT_LAST:   jump to last match of same tag
//type == DT_SELECT:   ":tselect [tag]", select tag from a list of all matches
//type == DT_JUMP:   ":tjump [tag]", jump to tag or select tag from a list
//type == DT_CSCOPE:   use cscope to find the tag
//type == DT_LTAG:   use location list for displaying tag matches
//type == DT_FREE:   free cached matches
//
//for cscope, returns TRUE if we jumped to tag or aborted, FALSE otherwise
int
do_tag(
   CS tag,      // tag (pattern) to jump to
   Unt type,
   int count,
   Boole forceit,   // :ta with !
   Boole verbose   // print "tag not found" message
){
   Taggy* tagstack = curPor->tagStack;
   int tagstackidx = (int)curPor->tagStackInd;
   Unt tagstacklen = curPor->tagStackLen;
   Unt cur_match = 0;
   int cur_fnum = curBook->fiNum;
   int oldtagstackidx = tagstackidx;
   int prevtagstackidx = tagstackidx;
   int new_tag = FALSE;
   int ic;
   int no_regexp = FALSE;
   Unt error_cur_match = 0;
   int save_pos = FALSE;
   FileMark saved_fmark;
   int jumped_to_tag = FALSE;
   int use_tagstack;
   int skip_msg = FALSE;
   CS buf_ffname = curBook->fullFileName;       // name to use for priority computation
   int use_tfu = 1;
   CS tofree = NULL;

   // remember the matches for the last used tag
   static Unt maxMatchCount = 0;  // limit used for match search
   static ExpandMatch matches = {};
   ExpandMatch newMatches = {};
   newMatches.a = createArena();
   static Unt flags;

#ifdef EXITFREE
   if (type == DT_FREE) {
      // remove the list of matches
      deleteArena(matches->a);
      matches->a = createArena();
      cs_free_tags();
      return FALSE;
   }
#endif

   if (tfu_in_use) {
      emsg(_(e_cannot_modify_tag_stack_within_tagfunc));
      return FALSE;
   }

   if (postponed_split == 0 && !portCheckCanSetCurBookForceIt(forceit))
      return FALSE;

   if (type == DT_HELP) {
      type = DT_TAG;
      no_regexp = TRUE;
      use_tfu = 0;
   }

   Unt prev_num_matches = matches.len;
   nofile_fname = NULL;

   CLEAR_POS(&saved_fmark.mark);   // shutup gcc 4.0
   saved_fmark.fnum = 0;

   //Don't add a tag to the tagstack if @tagstack has been reset.
   if ((!p_tgst && *tag != ZERO)) {
      use_tagstack = FALSE;
      new_tag = TRUE;
      if (g_do_tagpreview != 0) {
         tagstack_clear_entry(&ptag_entry);
         if ((ptag_entry.tagname = copyStr(tag)) == NULL)
            goto end_do_tag;
      }
   } else {
      if (g_do_tagpreview != 0)
         use_tagstack = FALSE;
      else
         use_tagstack = TRUE;

      // new pattern, add to the tag stack
      if (*tag != ZERO
         && (type == DT_TAG || type == DT_SELECT || type == DT_JUMP
             || type == DT_LTAG
             || type == DT_CSCOPE
             )
      ) {
         if (g_do_tagpreview != 0) {
            if (ptag_entry.tagname != NULL && STRCMP(ptag_entry.tagname, tag) == 0) {
               // Jumping to same tag: keep the current match, so that
               // the CursorHold autocommand example works.
               cur_match = ptag_entry.cur_match;
               cur_fnum = ptag_entry.cur_fnum;
            } else {
               tagstack_clear_entry(&ptag_entry);
               if ((ptag_entry.tagname = copyStr(tag)) == NULL)
                  goto end_do_tag;
            }
         } else {
            //If the last used entry is not at the top, delete all tag stack entries above it.
            while ((Unt)tagstackidx < tagstacklen)
               tagstack_clear_entry(&tagstack[--tagstacklen]);

            // if the tagstack is full: remove oldest entry
            if (++tagstacklen > TAGSTACKSIZE) {
               tagstacklen = TAGSTACKSIZE;
               tagstack_clear_entry(&tagstack[0]);
               for (Unt i = 1; i < tagstacklen; ++i)
                  tagstack[i - 1] = tagstack[i];
               tagstack[--tagstackidx].user_data = NULL;
            }

            //put the tag name in the tag stack
            if ((tagstack[tagstackidx].tagname = copyStr(tag)) == NULL) {
               curPor->tagStackLen = tagstacklen - 1;
               goto end_do_tag;
            }
            curPor->tagStackLen = tagstacklen;

            save_pos = TRUE;   // save the cursor position below
         }

         new_tag = TRUE;
      } else {
         if (g_do_tagpreview != 0 ? ptag_entry.tagname == NULL : tagstacklen == 0) {
            // empty stack
            emsg(_(e_tag_stack_empty));
            goto end_do_tag;
         }

         if (type == DT_POP) {     // go to older position
            int   old_KeyTyped = KeyTyped;
            if ((tagstackidx -= count) < 0) {
               emsg(_(e_at_bottom_of_tag_stack));
               if (tagstackidx + count == 0) {
                  // We did [num]^T from the bottom of the stack
                  tagstackidx = 0;
                  goto end_do_tag;
               }
               // We weren't at the bottom of the stack, so jump all the way to the bottom now.
               tagstackidx = 0;
            } ei (tagstackidx >= (int)tagstacklen) {  // count == 0?
                emsg(_(e_at_top_of_tag_stack));
                goto end_do_tag;
            }

            //Make a copy of the fmark, autocommands may invalidate the tagstack before it's used.
            saved_fmark = tagstack[tagstackidx].fmark;
            if (saved_fmark.fnum != curBook->fiNum) {
               //Jump to other file. If this fails (e.g. because the
               //file was changed) keep original position in tag stack.
               if (booklistGetFile(saved_fmark.fnum, saved_fmark.mark.lnum, GETF_SETMARK, forceit)
                   == FAIL
               ) {
                  tagstackidx = oldtagstackidx;  // back to old posn
                  goto end_do_tag;
               }
               //An BufReadPost autocommand may jump to the '" mark, but we don't what that here.
               curPor->cursor.lnum = saved_fmark.mark.lnum;
            } else {
               setpcmark();
               curPor->cursor.lnum = saved_fmark.mark.lnum;
            }
            curPor->cursor.col = saved_fmark.mark.col;
            curPor->setCursWant = TRUE;
            check_cursor();
            if ((p_fdo & FDO_TAG) && old_KeyTyped)
               foldOpenCursor();

            // remove the old list of matches
            deleteArena(matches.a);
            matches.a = createArena();
            cs_free_tags();
            tag_freematch();
            goto end_do_tag;
         }

         if (type == DT_TAG || type == DT_LTAG) {
            if (g_do_tagpreview != 0) {
               cur_match = ptag_entry.cur_match;
               cur_fnum = ptag_entry.cur_fnum;
            } else {
               // ":tag" (no argument): go to newer pattern
               save_pos = TRUE;   // save the cursor position below
               if ((tagstackidx += count - 1) >= (int)tagstacklen) {
                  //Beyond the last one, just give an error message and
                  //go to the last one. Don't store the cursor position.
                  tagstackidx = tagstacklen - 1;
                  emsg(_(e_at_top_of_tag_stack));
                  save_pos = FALSE;
               } ei (tagstackidx < 0) {// must have been count == 0
                  emsg(_(e_at_bottom_of_tag_stack));
                  tagstackidx = 0;
                  goto end_do_tag;
               }
               cur_match = tagstack[tagstackidx].cur_match;
               cur_fnum = tagstack[tagstackidx].cur_fnum;
            }
            new_tag = TRUE;
         } else { // go to other matching tag
            // Save index for when selection is cancelled.
            prevtagstackidx = tagstackidx;

            if (g_do_tagpreview != 0) {
               cur_match = ptag_entry.cur_match;
               cur_fnum = ptag_entry.cur_fnum;
            } else {
               tagstackidx--;
               if (tagstackidx < 0)
                  tagstackidx = 0;
               cur_match = tagstack[tagstackidx].cur_match;
               cur_fnum = tagstack[tagstackidx].cur_fnum;
            }
            switch (type) {
            case DT_FIRST: cur_match = count - 1; break;
            case DT_SELECT:
            case DT_JUMP:
            case DT_CSCOPE:
            case DT_LAST:  cur_match = MAXCOL - 1; break;
            case DT_NEXT:  cur_match += count; break;
            case DT_PREV:  cur_match -= count; break;
            }
            if (cur_match >= MAXCOL)
               cur_match = MAXCOL - 1;
            ei (cur_match == UNT) {
               emsg(_(e_cannot_go_before_first_matching_tag));
               skip_msg = TRUE;
               cur_match = 0;
               cur_fnum = curBook->fiNum;
            }
         }
      }

      if (g_do_tagpreview != 0) {
         if (type != DT_SELECT && type != DT_JUMP) {
            ptag_entry.cur_match = cur_match;
            ptag_entry.cur_fnum = cur_fnum;
         }
      } else {
         //For ":tag [arg]" or ":tselect" remember position before the jump.
         saved_fmark = tagstack[tagstackidx].fmark;
         if (save_pos) {
            tagstack[tagstackidx].fmark.mark = curPor->cursor;
            tagstack[tagstackidx].fmark.fnum = curBook->fiNum;
         }

         // Curwin will change in the call to jumpto_tag() if ":stag" was
         // used or an autocommand jumps to another window; store value of tagstackidx now.
         curPor->tagStackInd = tagstackidx;
         if (type != DT_SELECT && type != DT_JUMP) {
            curPor->tagStack[tagstackidx].cur_match = cur_match;
            curPor->tagStack[tagstackidx].cur_fnum = cur_fnum;
         }
      }
   }

   // When not using the current buffer get the name of buffer "cur_fnum".
   // Makes sure that the tag order doesn't change when using a remembered
   // position for "cur_match".
   if (cur_fnum != curBook->fiNum) {
      Book* book = bookFindFileByBookNr(cur_fnum);
      if (book)
         buf_ffname = book->fullFileName;
   }

   //Repeat searching for tags, when a file has not been found.
   for (;;) {
      int   other_name;
      CS name;

      //When desired match not found yet, try to find it (and others).
      if (use_tagstack) {
         // make a copy, the tagstack may change in 'tagfunc'
         name = copyStr(tagstack[tagstackidx].tagname);
         eeglFree(tofree);
         tofree = name;
      } ei (g_do_tagpreview != 0)
         name = ptag_entry.tagname;
      else
         name = tag;
      other_name = (tagmatchname == NULL || STRCMP(tagmatchname, name) != 0);
      if (new_tag
            || (cur_match >= matches.len && maxMatchCount != MAXCOL)
            || other_name
      ) {
         if (other_name) {
            eeglFree(tagmatchname);
            tagmatchname = copyStr(name);
         }

         if (type == DT_SELECT || type == DT_JUMP || type == DT_LTAG)
            cur_match = MAXCOL - 1;
         if (type == DT_TAG)
            maxMatchCount = MAXCOL;
         else
            maxMatchCount = cur_match + 1;

         // when the argument starts with '/', use it as a regexp
         if (!no_regexp && *name == '/') {
            flags = TAG_REGEXP;
            ++name;
         } else
            flags = TAG_NOIC;

         if (type == DT_CSCOPE)
            flags = TAG_CSCOPE;
         if (verbose)
            flags |= TAG_VERBOSE;

         if (!use_tfu)
            flags |= TAG_NO_TAGFUNC;

         if (find_tags(name, flags, maxMatchCount, buf_ffname, OUT &newMatches) == OK
                && newMatches.len < maxMatchCount
         )
            maxMatchCount = MAXCOL; // If less than maxMatchCount found: all matches found.

         //A tag function may do anything, which may cause various information to become 
         //invalid. At least check for the tagstack to still be the same.
         if (tagstack != curPor->tagStack) {
            emsg(_(e_window_unexpectedly_close_while_searching_for_tags));
            break;
         }

         //If there already were some matches for the same name, move them
         //to the start.  Avoids that the order changes when using
         //":tnext" and jumping to another file.
         if (!new_tag && !other_name) {
            Unt idx = 0;
            Tagline   tagp, tagp2;

            // Find the position of each old match in the new list.  Need
            // to use parse_match() to find the tag line.
            for (Unt j = 0; j < matches.len; ++j) {
               parse_match(matches.c[j], &tagp);
               for (Unt i = idx; i < newMatches.len; ++i) {
                  parse_match(newMatches.c[i], &tagp2);
                  if (STRCMP(tagp.tagname, tagp2.tagname) == 0) {
                     CS p = newMatches.c[i];
                     for (Unt k = i; k > idx; --k)
                        newMatches.c[k] = newMatches.c[k - 1];
                     newMatches.c[idx++] = p;
                     break;
                  }
               }
            }
         }
         deleteArena(matches.a);
         matches = newMatches;
      }

      if (matches.len == 0) {
         if (verbose)
            showErrFmtMsg(_(e_tag_not_found_str), name);
         g_do_tagpreview = 0;
      } else {
         int ask_for_selection = FALSE;

         if (type == DT_CSCOPE && matches.len > 1) {
            cs_print_tags();
            ask_for_selection = TRUE;
         } else
         if (type == DT_TAG && *tag != ZERO)
            //If a count is supplied to the ":tag <name>" command, jump to count'th matching tag
            cur_match = count > 0 ? count - 1 : 0;
         ei (type == DT_SELECT || (type == DT_JUMP && matches.len > 1)) {
            print_tag_list(new_tag, use_tagstack, matches);
            ask_for_selection = TRUE;
         } ei (type == DT_LTAG) {
            if (add_llist_tags(tag, matches) == FAIL)
               goto end_do_tag;
            cur_match = 0;      // Jump to the first tag
         }

         if (ask_for_selection == TRUE) {
            //Ask to select a tag from the list.
            Unt i = prompt_for_number(NULL);
            if (i > matches.len || gotInterruptG) {
               // no valid choice: don't change anything
               if (use_tagstack) {
                  tagstack[tagstackidx].fmark = saved_fmark;
                  tagstackidx = prevtagstackidx;
               }
               cs_free_tags();
               jumped_to_tag = TRUE;
               break;
            }
            cur_match = i - 1;
         }

         if (cur_match >= matches.len) {
            //Avoid giving this error when a file wasn't found and we're
            //looking for a match in another file, which wasn't found.
            //There will be an emsg("file doesn't exist") below then.
            if ((type == DT_NEXT || type == DT_FIRST) && nofile_fname == NULL) {
               if (matches.len == 1)
                  emsg(_(e_there_is_only_one_matching_tag));
               else
                  emsg(_(e_cannot_go_beyond_last_matching_tag));
               skip_msg = TRUE;
            }
            cur_match = matches.len - 1;
         }
         if (use_tagstack) {
            tagstack[tagstackidx].cur_match = cur_match;
            tagstack[tagstackidx].cur_fnum = cur_fnum;

            //store user-provided data originating from tagfunc
            Tagline tagp;
            if (use_tfu && parse_match(matches.c[cur_match], OUT &tagp) == OK && tagp.user_data) {
               EE_CLEAR(tagstack[tagstackidx].user_data);
               tagstack[tagstackidx].user_data = copySubstr(
                  tagp.user_data, tagp.user_data_end - tagp.user_data
               );
            }

            ++tagstackidx;
         } ei (g_do_tagpreview != 0) {
            ptag_entry.cur_match = cur_match;
            ptag_entry.cur_fnum = cur_fnum;
         }

         //Only when going to try the next match, report that the previous
         //file didn't exist.  Otherwise an emsg() is given below.
         if (nofile_fname && error_cur_match != cur_match)
            smsg(_("File \"%s\" does not exist"), nofile_fname);

         ic = (matches.c[cur_match][0] & MT_IC_OFF);
         if (type != DT_TAG && type != DT_SELECT && type != DT_JUMP && type != DT_CSCOPE
            && (matches.len > 1 || ic)
            && !skip_msg
         ) {
            // Give an indication of the number of matching tags
            SPRINTF(IObuff, _("tag %d of %d%s"),
                  cur_match + 1,
                  matches.len,
                  maxMatchCount != MAXCOL ? _(" or more") : Em
            );
            if (ic)
               STRCAT(IObuff, _("  Using tag with different case!"));
            if ((matches.len > prev_num_matches || new_tag) && matches.len > 1) {
               if (ic)
                  msgDeco(IObuff, getDecoFlags(HLF_W));
               else
                  msg(IObuff);
               msg_scroll = TRUE;   // don't overwrite this message
            } else
               give_warning(IObuff, ic);
            if (ic && !msg_scrolled && msg_silent == 0) {
               out_flush();
               ui_delay(1007L, TRUE);
            }
         }

         // Let the SwapExists event know what tag we are jumping to.
         eeSnprintf(IObuff, IOSIZE, ":ta %s\r", name);
         set_EeglVar_string(VV_SWAPCOMMAND, IObuff, -1);

         // Jump to the desired match.
         Unt i = jumpto_tag(matches.c[cur_match], forceit, type != DT_CSCOPE);

         set_EeglVar_string(VV_SWAPCOMMAND, NULL, -1);

         if (i == NOTAGFILE) {
            // File not found: try again with another matching tag
            if ((type == DT_PREV && cur_match > 0)
               || ((type == DT_TAG || type == DT_NEXT || type == DT_FIRST)
                   && (maxMatchCount != MAXCOL || cur_match < matches.len - 1))
            ){
               error_cur_match = cur_match;
               if (use_tagstack)
                  --tagstackidx;
               if (type == DT_PREV)
                  --cur_match;
               else {
                  type = DT_NEXT;
                  ++cur_match;
               }
               continue;
            }
            showErrFmtMsg(_(e_file_str_does_not_exist), nofile_fname);
         } else {
            // We may have jumped to another portal, check that tagstackidx is still valid.
            if (use_tagstack && tagstackidx > (int)curPor->tagStackLen)
               tagstackidx = curPor->tagStackInd;
            jumped_to_tag = TRUE;
         }
      }
      break;
   }

end_do_tag:
   deleteArena(newMatches.a);
   // Only store the new index when using the tagstack and it's valid.
   if (use_tagstack && tagstackidx <= (int)curPor->tagStackLen)
      curPor->tagStackInd = tagstackidx;
   postponed_split = 0;   //don't split next time
   g_do_tagpreview = 0;   //don't do tag preview next time

   eeglFree(tofree);
   return jumped_to_tag;
}

// List all the matching tags.
private void
print_tag_list(int new_tag, int use_tagstack, ExpandMatch matches) {
   Taggy   *tagstack = curPor->tagStack;
   int      tagstackidx = curPor->tagStackInd;
   CS command_end;
   Tagline   tagp;
   int      taglen;
   int      attr;

   //Assume that the first match indicates how long the tags can be, and align file names to that
   parse_match(matches.c[0], &tagp);
   taglen = (int)(tagp.tagname_end - tagp.tagname + 2);
   if (taglen < 18)
      taglen = 18;
   if (taglen > visibleColsG - 25)
      taglen = MAXCOL;
   if (msgColG == 0)
      msg_didout = FALSE;   // overwrite previous message
   msg_start();
   msgPutsDeco(_("  # pri kind tag"), getDecoFlags(HLF_T));
   msg_clr_eos();
   taglen_advance(taglen);
   msgPutsDeco(_("file\n"), getDecoFlags(HLF_T));

   for (Unt i = 0; i < matches.len && !gotInterruptG; ++i) {
      parse_match(matches.c[i], OUT &tagp);
      if (!new_tag && (
             (g_do_tagpreview != 0 && i == ptag_entry.cur_match) 
             || (use_tagstack && i == tagstack[tagstackidx].cur_match))
      )
         *IObuff = '>';
      else
         *IObuff = ' ';
      eeSnprintf(IObuff + 1, IOSIZE - 1,
         "%2d %s ", i + 1, mt_names[matches.c[i][0] & MT_MASK]);
      msg_puts(IObuff);
      if (tagp.tagkind)
         msgTranslatedSlice((Text){tagp.tagkind, (int)(tagp.tagkind_end - tagp.tagkind)});
      msg_advance(13);
      msgOuttransLenDeco(
         (Text){tagp.tagname, (int)(tagp.tagname_end - tagp.tagname)}, getDecoFlags(HLF_T)
      );
      msg_putchar(' ');
      taglen_advance(taglen);

      //Find out the actual file name. If it is long, truncate it and put "..." in the middle
      CS p = tag_full_fname(&tagp);
      if (p) {
         outputShortenedToALine(text(p), getDecoFlags(HLF_D));
         eeglFree(p);
      }
      if (msgColG > 0)
         msg_putchar('\n');
      if (gotInterruptG)
         break;
      msg_advance(15);

      // print any extra fields
      command_end = tagp.command_end;
      if (command_end) {
         p = command_end + 3;
         while (*p && *p != '\r' && *p != '\n') {
            while (*p == TAB)
               ++p;

            // skip "file:" without a value (static tag)
            if (STRNCMP(p, "file:", 5) == 0 && isSpace(p[5])) {
               p += 5;
               continue;
            }
            // skip "kind:<kind>" and "<kind>"
            if (p == tagp.tagkind || (p + 5 == tagp.tagkind && STRNCMP(p, "kind:", 5) == 0)) {
               p = tagp.tagkind_end;
               continue;
            }
            // print all other extra fields
            attr = getDecoFlags(HLF_CM);
            while (*p && *p != '\r' && *p != '\n') {
               if (msgColG + ptr2cells(p) >= visibleColsG) {
                  msg_putchar('\n');
                  if (gotInterruptG)
                      break;
                  msg_advance(15);
               }
               p = msgOneChar(p, attr);
               if (*p == TAB) {
                  msgPutsDeco(S" ", attr);
                  break;
               }
               if (*p == ':')
                  attr = 0;
            }
         }
         if (msgColG > 15) {
            msg_putchar('\n');
            if (gotInterruptG)
                break;
            msg_advance(15);
         }
      } else {
          for (p = tagp.command;
                  *p && *p != '\r' && *p != '\n'; ++p)
             {}
          command_end = p;
      }

      // Put the info (in several lines) at column 15. Don't display "/^" and "?^".
      p = tagp.command;
      if (*p == '/' || *p == '?') {
         ++p;
         if (*p == '^')
            ++p;
      }
      // Remove leading whitespace from pattern
      while (p != command_end && isSpace(*p))
         ++p;

      while (p != command_end) {
         if (msgColG + (*p == TAB ? 1 : ptr2cells(p)) > visibleColsG)
            msg_putchar('\n');
         if (gotInterruptG)
            break;
         msg_advance(15);

         // skip backslash used for escaping a command char or a backslash
         if (*p == '\\' && (*(p + 1) == *tagp.command || *(p + 1) == '\\'))
            ++p;

         if (*p == TAB) {
            msg_putchar(' ');
            ++p;
         } else
            p = msgOneChar(p, 0);

         // don't display the "$/;\"" and "$?;\""
         if (p == command_end - 2 && *p == '$' && *(p + 1) == *tagp.command)
            break;
         // don't display matching '/' or '?'
         if (p == command_end - 1 && *p == *tagp.command && (*p == '/' || *p == '?'))
            break;
      }
      if (msgColG)
         msg_putchar('\n');
      ui_breakcheck();
   }
   if (gotInterruptG)
      gotInterruptG = FALSE;   // only stop the listing
}

//}}}

// Add the matching tags to the location list for the current portal.
private int
add_llist_tags(CS tag, ExpandMatch matches) {
   Byte   tag_name[128 + 1];
   Byte   *p;
   Tagline   tagp;

   Byte fname[MAXPATHL + 1];
   CS cmd = alloc(CMDBUFFSIZE + 1);
   List* list = list_alloc();

   for (Unt i = 0; i < matches.len; ++i) {
      int len, cmd_len;
      long lnum;
      Bag* bag;

      parse_match(matches.c[i], &tagp);

      // Save the tag name
      len = (int)(tagp.tagname_end - tagp.tagname);
      if (len > 128)
         len = 128;
      copySubstrToAllocation(tag_name, (Text){tagp.tagname, len});
      tag_name[len] = ZERO;

      // Save the tag file name
      p = tag_full_fname(&tagp);
      if (!p)
         continue;
      copySubstrToAllocation(fname, (Text){p, MAXPATHL});
      eeglFree(p);

      // Get the line number or the search pattern used to locate the tag.
      lnum = 0;
      if (SAFE_isdigit(*tagp.command))
         // Line number is used to locate the tag
         lnum = ATOL(tagp.command);
      else {
         CS cmd_start, cmd_end;

         // Search pattern is used to locate the tag

         // Locate the end of the command
         cmd_start = tagp.command;
         cmd_end = tagp.command_end;
         if (!cmd_end) {
            for (p = tagp.command; *p && *p != '\r' && *p != '\n'; ++p)
               {}
            cmd_end = p;
         }

         //Now, cmd_end points to the character after the
         //command. Adjust it to point to the last character of the command.
         cmd_end--;

         //Skip the '/' and '?' characters at the beginning and end of the search pattern.
         if (*cmd_start == '/' || *cmd_start == '?')
            cmd_start++;

         if (*cmd_end == '/' || *cmd_end == '?')
            cmd_end--;

         len = 0;
         cmd[0] = ZERO;

         // If "^" is present in the tag search pattern, then copy it first.
         if (*cmd_start == '^') {
            STRCPY(cmd, "^");
            cmd_start++;
            len++;
         }

         // Precede the tag pattern with \V to make it very nomagic.
         STRCAT(cmd, "\\V");
         len += 2;

         cmd_len = (int)(cmd_end - cmd_start + 1);
         if (cmd_len > (CMDBUFFSIZE - 5))
            cmd_len = CMDBUFFSIZE - 5;
         STRNCAT(cmd, cmd_start, cmd_len);
         len += cmd_len;

         if (cmd[len - 1] == '$') {
            //Replace '$' at the end of the search pattern with '\$'
            cmd[len - 1] = '\\';
            cmd[len] = '$';
            len++;
         }

         cmd[len] = ZERO;
      }

      bag = allocBag();
      if (listAppendBag(list, bag) == FAIL) {
         eeglFree(bag);
         continue;
      }

      bagAddString(bag, S"text", tag_name);
      bagAddString(bag, S"filename", fname);
      bagAddNumber(bag, S"lnum", lnum);
      if (lnum == 0)
         bagAddString(bag, S"pattern", cmd);
    }

   eeSnprintf(IObuff, IOSIZE, "ltag %s", tag);
   setLocationList(getLocationStack(LOC_LIST_TAGS), list, LL_ACTION_NEW, IObuff, NULL);
   list_free(list);
   eeglFree(cmd);

   return OK;
}

//Free cached tags.
void
tag_freematch(void) {
   EE_CLEAR(tagmatchname);
}

private void
taglen_advance(int l) {
   if (l == MAXCOL) {
      msg_putchar('\n');
      msg_advance(24);
   } else
      msg_advance(13 + l);
}

// Print the tag stack
void
do_tags(Invocation *eap UNUSED) {
   int      i;
   CS name;
   Taggy   *tagstack = curPor->tagStack;
   int      tagstackidx = curPor->tagStackInd;
   int      tagstacklen = curPor->tagStackLen;

   // Highlight title
   msg_puts_title(_("\n  # TO tag         FROM line  in file/text"));
   for (i = 0; i < tagstacklen; ++i) {
      if (tagstack[i].tagname != NULL) {
         name = fm_getname(&(tagstack[i].fmark), 30);
         if (name == NULL)       // file name not available
            continue;

         msg_putchar('\n');
         eeSnprintf(IObuff, IOSIZE, "%c%2d %2d %-15s %5ld  ",
         i == tagstackidx ? '>' : ' ',
         i + 1,
         tagstack[i].cur_match + 1,
         tagstack[i].tagname,
         tagstack[i].fmark.mark.lnum);
         msg_outtrans(IObuff);
         msgOuttransDeco(name, tagstack[i].fmark.fnum == curBook->fiNum ? getDecoFlags(HLF_D) : 0);
         eeglFree(name);
      }
      out_flush();          // show one line at a time
   }
   if (tagstackidx == tagstacklen)   // idx at top of stack
      msg_puts(S"\n>");
}

//Compare two strings, for length "len", ignoring case the ASCII way.
//return 0 for match, < 0 for smaller, > 0 for bigger
//Make sure case is folded to uppercase in comparison (like for 'sort -f')
private int
tag_strnicmp(CS s1, CS s2, Unt len) {
   int      i;
   while (len > 0) {
      i = (int)TOUPPER_ASC(*s1) - (int)TOUPPER_ASC(*s2);
      if (i != 0)
          return i;         // this character different
      if (*s1 == ZERO)
          break;         // strings match until ZERO
      ++s1;
      ++s2;
      --len;
   }
   return 0;            // strings match
}

//Info about the tag pattern being used.
typedef struct {
   CS pat;      // the pattern
   int      len;      // length of pat[]
   CS head;      // start of pattern head
   int      headlen;   // length of head[]
   RegMatch   regmatch;   // regexp program, may be NULL
} TagPattern;

//Extract info from the tag search pattern "pats->pat".
private void
prepare_pats(TagPattern *pats, int has_re) {
   pats->head = pats->pat;
   pats->headlen = pats->len;
   if (has_re) {
      // When the pattern starts with '^' or "\\<", binary searching can be used (much faster).
      if (pats->pat[0] == '^')
          pats->head = pats->pat + 1;
      ei (pats->pat[0] == '\\' && pats->pat[1] == '<')
          pats->head = pats->pat + 2;
      if (pats->head == pats->pat)
          pats->headlen = 0;
      else
          for (pats->headlen = 0; pats->head[pats->headlen] != ZERO;
                              ++pats->headlen)
         if (firstOccurrence((CS)(".[~*\\$"),
                     pats->head[pats->headlen]) != NULL)
             break;
   }

   if (has_re)
      pats->regmatch.regprog = compileRegexp(pats->pat, RE_MAGIC);
   else
      pats->regmatch.regprog = NULL;
}

//Call the user-defined function to generate a list of tags used by find_tags().
//
//Return OK if at least 1 tag has been successfully found,
//NOTDONE if the function returns v:null, and FAIL otherwise.
private int
find_tagfunc_tags(
   Byte   *pat,      // pattern supplied to the user-defined function
   ArrayList   *ga,      // the tags will be placed here
   int      *match_count,   // here the number of tags found will be placed
   int      flags,      // flags from find_tags (TAG_*)
   Byte   *buf_ffname)   // name of buffer for priority
{
   Pos       save_pos;
   List      *taglist;
   ListItem  *item;
   int      ntags = 0;
   int      result = FAIL;
   Var   args[4];
   Var   returnVar;
   Byte      flagString[4];
   Bag   *d;
   Taggy   *tag = &curPor->tagStack[curPor->tagStackInd];

   if (!curBook->o.tagFn || !curBook->o.tagFn->name || *curBook->o.tagFn->name == ZERO)
      return FAIL;

   args[0].tag = VAR_STRING;
   args[0].string = pat;
   args[1].tag = VAR_STRING;
   args[1].string = flagString;

   // create 'info' dict argument
   d = allocBag_lock(VAR_FIXED);
   if (tag->user_data)
      bagAddString(d, S"user_data", tag->user_data);
   if (buf_ffname)
      bagAddString(d, S"buf_ffname", buf_ffname);

    ++d->refcount;
    args[2].tag = VAR_BAG;
    args[2].bag = d;

    args[3].tag = VAR_UNKNOWN;

    eeSnprintf(flagString, sizeof(flagString),
       "%s%s%s",
       g_tag_at_cursor      ? "c": "",
       flags & TAG_INS_COMP ? "i": "",
       flags & TAG_REGEXP   ? "r": "");

    save_pos = curPor->cursor;
    result = call_callback(curBook->o.tagFn, 0, &returnVar, 3, args);
    curPor->cursor = save_pos;   // restore the cursor position
    check_cursor();         // make sure cursor position is valid
    --d->refcount;

   if (result == FAIL)
      return FAIL;
   if (returnVar.tag == VAR_SPECIAL && returnVar.number == VVAL_NULL) {
      clearVar(&returnVar);
      return NOTDONE;
   }
   if (returnVar.tag != VAR_LIST || !returnVar.list) {
      clearVar(&returnVar);
      emsg(_(e_invalid_return_value_from_tagfunc));
      return FAIL;
   }
   taglist = returnVar.list;

   FOR_ALL_LIST_ITEMS(taglist, item) {
      Byte      *mfp;
      Byte      *rname, *res_fname, *res_cmd, *res_kind;
      int      len;
      DictIterator   iter;
      Byte      *dict_key;
      Var   *tv;
      int      has_extra = 0;
      int      name_only = flags & TAG_NAMES;

      if (item->c.tag != VAR_BAG) {
         emsg(_(e_invalid_return_value_from_tagfunc));
         break;
      }
      len = 2;
      rname = NULL;
      res_fname = NULL;
      res_cmd = NULL;
      res_kind = NULL;

      bagIterateStart(&item->c, &iter);
      while (NULL != (dict_key = bagIterateNext(&iter, &tv))) {
         if (tv->tag != VAR_STRING || tv->string == NULL)
            continue;

         len += (int)STRLEN(tv->string) + 1;   // Space for "\tVALUE"
         if (!STRCMP(dict_key, "name")) {
            rname = tv->string;
            continue;
         }
         if (!STRCMP(dict_key, "filename")) {
            res_fname = tv->string;
            continue;
         }
         if (!STRCMP(dict_key, "cmd")) {
            res_cmd = tv->string;
            continue;
         }
         has_extra = 1;
         if (!STRCMP(dict_key, "kind")) {
            res_kind = tv->string;
            continue;
         }
         // Other elements will be stored as "\tKEY:VALUE"
         // Allocate space for the key and the colon
         len += (int)STRLEN(dict_key) + 1;
      }

      if (has_extra)
         len += 2;   // need space for ;"

      if (!rname || !res_fname || !res_cmd) {
         emsg(_(e_invalid_return_value_from_tagfunc));
         break;
      }

      if (name_only)
         mfp = copyStr(rname);
      else
         mfp = alloc(sizeof(Byte) + len + 1);

      if (!name_only) {
         CS p = mfp;

         *p++ = MT_GL_OTH + 1;   // mtt
         *p++ = TAG_SEP;       // no tag file name

         STRCPY(p, rname);
         p += STRLEN(p);

         *p++ = TAB;
         STRCPY(p, res_fname);
         p += STRLEN(p);

         *p++ = TAB;
         STRCPY(p, res_cmd);
         p += STRLEN(p);

         if (has_extra) {
            STRCPY(p, ";\"");
            p += STRLEN(p);

            if (res_kind) {
               *p++ = TAB;
               STRCPY(p, res_kind);
               p += STRLEN(p);
            }

            bagIterateStart(&item->c, &iter);
            while (NULL != (dict_key = bagIterateNext(&iter, &tv))) {
                if (tv->tag != VAR_STRING || tv->string == NULL)
               continue;

                if (!STRCMP(dict_key, "name"))
               continue;
                if (!STRCMP(dict_key, "filename"))
               continue;
                if (!STRCMP(dict_key, "cmd"))
               continue;
                if (!STRCMP(dict_key, "kind"))
               continue;

                *p++ = TAB;
                STRCPY(p, dict_key);
                p += STRLEN(p);
                STRCPY(p, ":");
                p += STRLEN(p);
                STRCPY(p, tv->string);
                p += STRLEN(p);
            }
          }
      }

      // Add all matches because tagfunc should do filtering.
      if (ga_grow(ga, 1) == OK) {
         ((Byte **)(ga->c))[ga->len++] = mfp;
         ++ntags;
         result = OK;
      } else {
         eeglFree(mfp);
         break;
      }
   }

   clearVar(&returnVar);

   *match_count = ntags;
   return result;
}

// State information used during a tag search
typedef struct {
   tagsearch_state_T   state;      // tag search state
   int      stop_searching;      // stop when match found or error
   TagPattern   *orgpat;      // holds unconverted pattern info
   Byte     *lbuf;         // line buffer
   int      lbuf_size;      // length of lbuf
   CS tag_fname;      // name of the tag file
   FILE* fp;         // current tags file pointer
   int flags;         // flags used for tag search
   int tag_file_sorted;   // !_TAG_FILE_SORTED value
   int get_searchpat;      // used for 'showfulltag'
   int help_only;      // only search for help tags
   int did_open;      // did open a tag file
   int mincount;      // MAXCOL: find all matches
               // other: minimal number of matches
   int linear;         // do a linear search
   Byte help_lang[3];      // lang of current tags file
   int      help_pri;      // help language priority
   CS help_lang_find;   // lang to be found
   int      is_txt;         // flag of file extension
   int      match_count;      // number of matches found
   ArrayList   ga_match[MT_COUNT];   // stores matches in sequence
   EeSet   ht_match[MT_COUNT];   // stores matches by key
} FindTags;

// Initialize the state used by find_tags(). Returns OK on success and FAIL on memory allocation 
// failure.
private int
findtags_state_init(FindTags* st, CS pat, Unt flags, int mincount) {
   int      mtt;

   st->tag_fname = alloc(MAXPATHL + 1);
   st->fp = NULL;
   st->orgpat = ALLOC_ONE(TagPattern);
   st->orgpat->pat = pat;
   st->orgpat->len = (int)STRLEN(pat);
   st->orgpat->regmatch.regprog = NULL;
   st->flags = flags;
   st->tag_file_sorted = ZERO;
   st->help_only = (flags & TAG_HELP);
   st->get_searchpat = FALSE;
   st->help_lang[0] = ZERO;
   st->help_pri = 0;
   st->help_lang_find = NULL;
   st->is_txt = FALSE;
   st->did_open = FALSE;
   st->mincount = mincount;
   st->lbuf_size = LSIZE;
   st->lbuf = alloc(st->lbuf_size);
   st->match_count = 0;
   st->stop_searching = FALSE;

   for (mtt = 0; mtt < MT_COUNT; ++mtt) {
      ga_init2(&st->ga_match[mtt], sizeof(CS), 100);
      hash_init(&st->ht_match[mtt]);
   }

   return OK;
}

//Free the state used by find_tags()
private void
findtags_state_free(FindTags *st) {
   eeglFree(st->tag_fname);
   eeglFree(st->lbuf);
   eeRegFree(st->orgpat->regmatch.regprog);
   eeglFree(st->orgpat);
}

//Initialize the language and priority used for searching tags in an Eegl help file.
//Return TRUE to process the help file for tags and FALSE to skip the file.
private int
findtags_in_help_init(FindTags *st) {
   int      i;

   // Keep "en" as the language if the file extension is ".txt"
   if (st->is_txt)
      STRCPY(st->help_lang, "en");
   else {
      // Prefer help tags according to 'helplang'.  Put the two-letter language name in help_lang[]
      i = (int)STRLEN(st->tag_fname);
      if (i > 3 && st->tag_fname[i - 3] == '-')
         copySubstrToAllocation(st->help_lang, (Text){st->tag_fname + i - 2, 2});
      else
         STRCPY(st->help_lang, "en");
   }
   // When searching for a specific language skip tags files for other languages.
   if (st->help_lang_find != NULL
          && caseInsensitiveCompare(st->help_lang, st->help_lang_find) != 0)
      return FALSE;

   // For CTRL-] in a help file prefer a match with the same language.
   if ((st->flags & TAG_KEEP_LANG)
         && st->help_lang_find == NULL
         && curBook->currFileName != NULL
         && (i = (int)STRLEN(curBook->currFileName)) > 4
         && curBook->currFileName[i - 1] == 'x'
         && curBook->currFileName[i - 4] == '.'
         && STRNICMP(curBook->currFileName + i - 3, st->help_lang, 2) == 0)
      st->help_pri = 0;
   else {
      // search for the language in 'helplang'
      st->help_pri = 1;
      CS s;
      for (s = p_hlg; *s != ZERO; ++s) {
         if (STRNICMP(s, st->help_lang, 2) == 0)
            break;
         ++st->help_pri;
         if ((s = firstOccurrence(s, ',')) == NULL)
            break;
      }
      if (s == NULL || *s == ZERO) {
         // Language not in 'helplang': use last, prefer English, unless found already.
         ++st->help_pri;
         if (caseInsensitiveCompare(st->help_lang, "en") != 0)
            ++st->help_pri;
      }
   }

   return TRUE;
}

//Use the function set in 'tagfunc' (if configured and enabled) to get the tags.
//Return OK if at least 1 tag has been successfully found, NOTDONE if the
//'tagfunc' is not used, still executing or the 'tagfunc' returned v:null and FAIL otherwise.
private int
findtags_apply_tfu(FindTags *st, CS pat, CS buf_ffname) {
   int      use_tfu = ((st->flags & TAG_NO_TAGFUNC) == 0);
   int      retval;

   if (!use_tfu || tfu_in_use || !curBook->o.tagFn)
      return NOTDONE;

   tfu_in_use = TRUE;
   retval = find_tagfunc_tags(pat, st->ga_match, &st->match_count, st->flags, buf_ffname);
   tfu_in_use = FALSE;

   return retval;
}


//Read the next line from a tags file.
//Returns TAGS_READ_SUCCESS if a tags line is successfully read and should be processed.
//Returns TAGS_READ_EOF if the end of file is reached.
//Returns TAGS_READ_IGNORE if the current line should be ignored (used when
//reached end of a emacs included tags file)
private tags_read_status_T
findtags_get_next_line(FindTags *st, TagSearchInfo* sinfo_p) {
   int      eof;
   FileSize   offset;

   // For binary search: compute the next offset to use.
   if (st->state == TS_BINARY) {
      offset = sinfo_p->low_offset + ((sinfo_p->high_offset - sinfo_p->low_offset) / 2);
      if (offset == sinfo_p->curr_offset)
         return TAGS_READ_EOF; // End the binary search without a match.
      else
         sinfo_p->curr_offset = offset;
   }

   // Skipping back (after a match during binary search).
   ei (st->state == TS_SKIP_BACK) {
      sinfo_p->curr_offset -= st->lbuf_size * 2;
      if (sinfo_p->curr_offset < 0) {
         sinfo_p->curr_offset = 0;
         rewind(st->fp);
         st->state = TS_STEP_FORWARD;
      }
   }

   // When jumping around in the file, first read a line to find the
   // start of the next line.
   if (st->state == TS_BINARY || st->state == TS_SKIP_BACK) {
   // Adjust the search file offset to the correct position
   sinfo_p->curr_offset_used = sinfo_p->curr_offset;
   (void)fseeko(st->fp, sinfo_p->curr_offset, SEEK_SET);
   eof = eeFgets(st->lbuf, st->lbuf_size, st->fp);
   if (!eof && sinfo_p->curr_offset != 0) {
      sinfo_p->curr_offset = ftello(st->fp);
      if (sinfo_p->curr_offset == sinfo_p->high_offset) {
         // oops, gone a bit too far; try from low offset
         (void)fseeko(st->fp, sinfo_p->low_offset, SEEK_SET);
         sinfo_p->curr_offset = sinfo_p->low_offset;
      }
      eof = eeFgets(st->lbuf, st->lbuf_size, st->fp);
   }
   // skip empty and blank lines
   while (!eof && eeIsBlankLine(st->lbuf)) {
      sinfo_p->curr_offset = ftello(st->fp);
      eof = eeFgets(st->lbuf, st->lbuf_size, st->fp);
   }
   if (eof) {
       // Hit end of file.  Skip backwards.
       st->state = TS_SKIP_BACK;
       sinfo_p->match_offset = ftello(st->fp);
       sinfo_p->curr_offset = sinfo_p->curr_offset_used;
       return TAGS_READ_IGNORE;
   }
    }
    // Not jumping around in the file: Read the next line.
    else {
   // skip empty and blank lines
   do {
       if (st->flags & TAG_CSCOPE)
      eof = cs_fgets(st->lbuf, st->lbuf_size);
       else
      eof = eeFgets(st->lbuf, st->lbuf_size, st->fp);
   } while (!eof && eeIsBlankLine(st->lbuf));

   if (eof) {
       return TAGS_READ_EOF;
   }
   }

   return TAGS_READ_SUCCESS;
}

//Parse a tags file header line in "st->lbuf".
//Returns TRUE if the current line in st->lbuf is not a tags header line and
//should be parsed as a regular tag line. Returns FALSE if the line is a
//header line and the next header line should be read.
private int
findtags_hdr_parse(FindTags *st) {
   Byte   *p;

    // Header lines in a tags file start with "!_TAG_"
    if (STRNCMP(st->lbuf, "!_TAG_", 6) != 0)
   // Non-header item before the header, e.g. "!" itself.
   return TRUE;

   // Process the header line.
   if (STRNCMP(st->lbuf, "!_TAG_FILE_SORTED\t", 18) == 0)
      st->tag_file_sorted = st->lbuf[18];
   if (STRNCMP(st->lbuf, "!_TAG_FILE_ENCODING\t", 20) == 0) {
      // Prepare to convert every line from the specified encoding to 'encoding'.
      for (p = st->lbuf + 20; *p > ' ' && *p < 127; ++p)
          ;
      *p = ZERO;
   }

   // Read the next line.  Unrecognized flags are ignored.
   return FALSE;
}

//Handler to initialize the state when starting to process a new tags file.
//Called in the TS_START state when finding tags from a tags file.
//Returns TRUE if the line read from the tags file should be parsed and
//FALSE if the line should be ignored.
private int
findtags_start_state_handler(
   FindTags   *st,
   int         *sortic,
   TagSearchInfo   *sinfo_p)
{
   int      use_cscope = (st->flags & TAG_CSCOPE);
   int      noic = (st->flags & TAG_NOIC);
   FileSize   filesize;

   // The header ends when the line sorts below "!_TAG_".  When case is
   // folded lower case letters sort before "_".
   if (STRNCMP(st->lbuf, "!_TAG_", 6) <= 0
          || (st->lbuf[0] == '!' && ASCII_ISLOWER(st->lbuf[1])))
      return findtags_hdr_parse(st);

   // Headers ends.

   // When there is no tag head, or ignoring case, need to do a linear search.
   // When no "!_TAG_" is found, default to binary search.  If the tag file isn't sorted, the 
   // second loop will find it. When "!_TAG_FILE_SORTED" found: start binary search if flag set.
   // For cscope, it's always linear.
   if (st->linear || use_cscope)
      st->state = TS_LINEAR;
   ei (st->tag_file_sorted == ZERO)
      st->state = TS_BINARY;
   ei (st->tag_file_sorted == '1')
      st->state = TS_BINARY;
   ei (st->tag_file_sorted == '2') {
      st->state = TS_BINARY;
      *sortic = TRUE;
      st->orgpat->regmatch.rm_ic = (p_ic || !noic);
   } else
      st->state = TS_LINEAR;

   if (st->state == TS_BINARY && st->orgpat->regmatch.rm_ic && !*sortic) {
      // Binary search won't work for ignoring case, use linear search.
      st->linear = TRUE;
      st->state = TS_LINEAR;
   }

   // When starting a binary search, get the size of the file and
   // compute the first offset.
   if (st->state == TS_BINARY) {
      if (fseeko(st->fp, 0L, SEEK_END) != 0)
          // can't seek, don't use binary search
          st->state = TS_LINEAR;
      else {
          // Get the tag file size (don't use fstat(), it's not portable). 
          filesize = ftello(st->fp);
          (void)fseeko(st->fp, 0L, SEEK_SET);

          // Calculate the first read offset in the file.  Start
          // the search in the middle of the file.
          sinfo_p->low_offset = 0;
          sinfo_p->low_char = 0;
          sinfo_p->high_offset = filesize;
          sinfo_p->curr_offset = 0;
          sinfo_p->high_char = 0xff;
      }
      return FALSE;
   }

   return TRUE;
}

//Parse a tag line read from a tags file.
//Also compares the tag name in "tagpp->tagname" with a search pattern in
//"st->orgpat->head" as a quick check if the tag may match.
//Returns:
//- TAG_MATCH_SUCCESS if the tag may match
//- TAG_MATCH_FAIL if the tag doesn't match
//- TAG_MATCH_NEXT to look for the next matching tag (used in a binary search)
//- TAG_MATCH_STOP if all the tags are processed without a match. Uses the
//  values in "margs" for doing the comparison.
private tagmatch_status_T
findtags_parse_line(
   FindTags      *st,
   Tagline         *tagpp,
   FindTagsMatchArgs   *margs,
   TagSearchInfo      *sinfo_p)
{
   int      status;
   int      i;
   int      cmplen;
   int      tagcmp;

   // Figure out where the different strings are in this line.
   // For "normal" tags: Do a quick check if the tag matches.
   // This speeds up tag searching a lot!
   if (st->orgpat->headlen) {
      CLEAR_FIELD(*tagpp);
      tagpp->tagname = st->lbuf;
      tagpp->tagname_end = firstOccurrence(st->lbuf, TAB);
      if (tagpp->tagname_end == NULL)
         // Corrupted tag line.
         return TAG_MATCH_FAIL;

      // Skip this line if the length of the tag is different and
      // there is no regexp, or the tag is too short.
      cmplen = (int)(tagpp->tagname_end - tagpp->tagname);
      if ((st->flags & TAG_REGEXP) && st->orgpat->headlen < cmplen)
         cmplen = st->orgpat->headlen;
      ei (st->state == TS_LINEAR && st->orgpat->headlen != cmplen)
         return TAG_MATCH_NEXT;

      if (st->state == TS_BINARY) {
         // Simplistic check for unsorted tags file.
         i = (int)tagpp->tagname[0];
         if (margs->sortic)
            i = (int)TOUPPER_ASC(tagpp->tagname[0]);
         if (i < sinfo_p->low_char || i > sinfo_p->high_char)
            margs->sort_error = TRUE;

          // Compare the current tag with the searched tag.
          if (margs->sortic)
         tagcmp = tag_strnicmp(tagpp->tagname, st->orgpat->head,
                        (Unt)cmplen);
          else
         tagcmp = STRNCMP(tagpp->tagname, st->orgpat->head, cmplen);

         // A match with a shorter tag means to search forward.
         // A match with a longer tag means to search backward.
         if (tagcmp == 0) {
            if (cmplen < st->orgpat->headlen)
                tagcmp = -1;
            ei (cmplen > st->orgpat->headlen)
                tagcmp = 1;
         }

         if (tagcmp == 0) {
            // We've located the tag, now skip back and search
            // forward until the first matching tag is found.
            st->state = TS_SKIP_BACK;
            sinfo_p->match_offset = sinfo_p->curr_offset;
            return TAG_MATCH_NEXT;
         }
         if (tagcmp < 0) {
            sinfo_p->curr_offset = ftello(st->fp);
            if (sinfo_p->curr_offset < sinfo_p->high_offset) {
               sinfo_p->low_offset = sinfo_p->curr_offset;
               if (margs->sortic)
                  sinfo_p->low_char = TOUPPER_ASC(tagpp->tagname[0]);
               else
                  sinfo_p->low_char = tagpp->tagname[0];
               return TAG_MATCH_NEXT;
            }
         }
         if (tagcmp > 0 && sinfo_p->curr_offset != sinfo_p->high_offset) {
            sinfo_p->high_offset = sinfo_p->curr_offset;
            if (margs->sortic)
                sinfo_p->high_char = TOUPPER_ASC(tagpp->tagname[0]);
            else
                sinfo_p->high_char = tagpp->tagname[0];
            return TAG_MATCH_NEXT;
         }

          // No match yet and are at the end of the binary search.
          return TAG_MATCH_STOP;
      } ei (st->state == TS_SKIP_BACK) {
         if (MB_STRNICMP(tagpp->tagname, st->orgpat->head, cmplen) != 0)
            st->state = TS_STEP_FORWARD;
         else
            // Have to skip back more.  Restore the curr_offset
            // used, otherwise we get stuck at a long line.
            sinfo_p->curr_offset = sinfo_p->curr_offset_used;
          return TAG_MATCH_NEXT;
      } ei (st->state == TS_STEP_FORWARD) {
          if (MB_STRNICMP(tagpp->tagname, st->orgpat->head, cmplen) != 0) {
         if ((FileSize)ftello(st->fp) > sinfo_p->match_offset)
             return TAG_MATCH_STOP;   // past last match
         else
             return TAG_MATCH_NEXT;   // before first match
          }
      } else
          // skip this match if it can't match
          if (MB_STRNICMP(tagpp->tagname, st->orgpat->head, cmplen) != 0)
         return TAG_MATCH_NEXT;

      // Can be a matching tag, isolate the file name and command.
      tagpp->fname = tagpp->tagname_end + 1;
      tagpp->fname_end = firstOccurrence(tagpp->fname, TAB);
      if (tagpp->fname_end == NULL)
          status = FAIL;
      else {
          tagpp->command = tagpp->fname_end + 1;
          status = OK;
      }
   } else
      status = parse_tag_line(st->lbuf, tagpp);

   if (status == FAIL)
      return TAG_MATCH_FAIL;

   return TAG_MATCH_SUCCESS;
}

//Initialize the structure used for tag matching.
private void
findtags_matchargs_init(FindTagsMatchArgs *margs, int flags) {
   margs->matchoff = 0;         // match offset
   margs->match_re = FALSE;         // match with regexp
   margs->match_no_ic = FALSE;         // matches with case
   margs->has_re = (flags & TAG_REGEXP);   // regexp used
   margs->sortic = FALSE;         // tag file sorted in nocase
   margs->sort_error = FALSE;         // tags file not sorted
}

//Compare the tag name in "tagpp->tagname" with a search pattern in "st->orgpat->pat".
//Return TRUE if the tag matches, FALSE if the tag doesn't match.
//Use the values in "margs" for doing the comparison.
private int
findtags_match_tag(
    FindTags   *st,
    Tagline      *tagpp,
    FindTagsMatchArgs *margs)
{
   int      match = FALSE;
   int      cmplen;

   // First try matching with the pattern literally (also when it is a regexp).
   cmplen = (int)(tagpp->tagname_end - tagpp->tagname);
   // if tag length does not match, don't try comparing
   if (st->orgpat->len != cmplen)
      match = FALSE;
   else {
      if (st->orgpat->regmatch.rm_ic) {
          match =
         (MB_STRNICMP(tagpp->tagname, st->orgpat->pat, cmplen) == 0);
          if (match)
         margs->match_no_ic =
             (STRNCMP(tagpp->tagname, st->orgpat->pat, cmplen) == 0);
      } else
          match = (STRNCMP(tagpp->tagname, st->orgpat->pat, cmplen) == 0);
   }

   // Has a regexp: Also find tags matching regexp.
   margs->match_re = FALSE;
   if (!match && st->orgpat->regmatch.regprog != NULL) {
      int   cc;

      cc = *tagpp->tagname_end;
      *tagpp->tagname_end = ZERO;
      match = eeRegexec(&st->orgpat->regmatch, tagpp->tagname, (ColNr)0);
      if (match) {
         margs->matchoff = (int)(st->orgpat->regmatch.startp[0] - tagpp->tagname);
         if (st->orgpat->regmatch.rm_ic) {
            st->orgpat->regmatch.rm_ic = FALSE;
            margs->match_no_ic = eeRegexec(&st->orgpat->regmatch,
               tagpp->tagname, (ColNr)0);
            st->orgpat->regmatch.rm_ic = TRUE;
         }
      }
      *tagpp->tagname_end = cc;
      margs->match_re = TRUE;
   }

   return match;
}


//Add a matching tag found in a tags file to st->ht_match and st->ga_match.
//Return OK if successfully added the match and FAIL on memory allocation failure.
private int
findtags_add_match(
   FindTags   *st,
   Tagline      *tagpp,
   FindTagsMatchArgs   *margs,
   Byte      *buf_ffname,
   Hash      *hash)
{
   int      use_cscope = (st->flags & TAG_CSCOPE);
   int      name_only = (st->flags & TAG_NAMES);
   int      mtt;
   int      len = 0;
   int      is_current;      // file name matches
   int      is_static;      // current tag line is static
   Byte   *mfp;
   Byte   *p;
   Byte   *s;

   if (use_cscope) {
      // Don't change the ordering, always use the same table.
      mtt = MT_GL_OTH;
   } else {
      // Decide in which array to store this match.
      is_current = test_for_current(
         tagpp->fname, tagpp->fname_end, st->tag_fname, buf_ffname);
          is_static = test_for_static(tagpp);

      // decide in which of the sixteen tables to store this match
      if (is_static) {
         if (is_current)
            mtt = MT_ST_CUR;
         else
            mtt = MT_ST_OTH;
      } else {
         if (is_current)
            mtt = MT_GL_CUR;
         else
            mtt = MT_GL_OTH;
      }
      if (st->orgpat->regmatch.rm_ic && !margs->match_no_ic)
         mtt += MT_IC_OFF;
      if (margs->match_re)
         mtt += MT_RE_OFF;
   }

   // Add the found match in ht_match[mtt] and ga_match[mtt]. Store the info we need later, which 
   // depends on the kind of tags we are dealing with.
   if (st->help_only) {
#define ML_EXTRA 3
      // Append the help-heuristic number after the tagname, for sorting it later. The heuristic 
      // is ignored for detecting duplicates. The format is {tagname}@{lang}ZERO{heuristic}ZERO
      *tagpp->tagname_end = ZERO;
      len = (int)(tagpp->tagname_end - tagpp->tagname);
      mfp = alloc(sizeof(Byte) + len + 10 + ML_EXTRA + 1);

      p = mfp;
      STRCPY(p, tagpp->tagname);
      p[len] = '@';
      STRCPY(p + len + 1, st->help_lang);

      int heuristic = help_heuristic(tagpp->tagname,
           margs->match_re ? margs->matchoff : 0,
           !margs->match_no_ic);
      heuristic += st->help_pri;
      SPRINTF(p + len + 1 + ML_EXTRA, "%06d", heuristic);
      *tagpp->tagname_end = TAB;
   } ei (name_only) {
      if (st->get_searchpat) {
         CS temp_end = tagpp->command;

         if (*temp_end == '/') {
            while (*temp_end && *temp_end != '\r' && *temp_end != '\n' && *temp_end != '$')
                temp_end++;
         } 

         if (tagpp->command + 2 < temp_end) {
            len = (int)(temp_end - tagpp->command - 2);
            mfp = alloc(len + 2);
            copySubstrToAllocation(mfp, (Text){tagpp->command + 2, len});
         } else
            mfp = NULL;
         st->get_searchpat = FALSE;
      } else {
         len = (int)(tagpp->tagname_end - tagpp->tagname);
         mfp = alloc(sizeof(Byte) + len + 1);
         copySubstrToAllocation(mfp, (Text){tagpp->tagname, len});

         // if wanted, re-read line to get long form too
         if (stateG & MODE_INSERT)
            st->get_searchpat = p_sft;
      }
   } else {
      Unt tag_fname_len = STRLEN(st->tag_fname);

      // Save the tag in a buffer.
      // Use 0x02 to separate fields (Can't use ZERO because the hash key is terminated by 
      // ZERO, or Ctrl_A because that is part of some Emacs tag files -- see parse_tag_line).
      // Emacs tag: <mtt><tag_fname><0x02><ebuf><0x02><lbuf><ZERO>
      // other tag: <mtt><tag_fname><0x02><0x02><lbuf><ZERO>
      // without Emacs tags: <mtt><tag_fname><0x02><lbuf><ZERO>
      // Here <mtt> is the "mtt" value plus 1 to avoid ZERO.
      len = (int)tag_fname_len + (int)STRLEN(st->lbuf) + 3;
      mfp = alloc(sizeof(Byte) + len + 1);
      p = mfp;
      p[0] = mtt + 1;
      STRCPY(p + 1, st->tag_fname);
      p[tag_fname_len + 1] = TAG_SEP;
      s = p + 1 + tag_fname_len + 1;
      STRCPY(s, st->lbuf);
   }

   if (mfp) {
      // Don't add identical matches. Add all cscope tags, because they are all listed.
      // "mfp" is used as a hash key, there is a ZERO byte to end
      // the part that matters for comparing, more bytes may
      // follow after it.  E.g. help tags store the priority after the ZERO.
      Text t = mbText(mfp);
      if (use_cscope)
         ++*hash;
      else
         *hash = calcHash(t);
      EeSetItem* hi = hash_lookup(&st->ht_match[mtt], t, *hash);
      if (HASHITEM_EMPTY(hi)) {
         if (hash_add_item(&st->ht_match[mtt], hi, t, *hash) == FAIL
             || ga_grow(&st->ga_match[mtt], 1) == FAIL
         ) {
            // Out of memory! Just forget about the rest.
            st->stop_searching = TRUE;
            return FAIL;
         }

         ((Byte **)(st->ga_match[mtt].c))[st->ga_match[mtt].len++] = mfp;
         st->match_count++;
      } else
         // duplicate tag, drop it
         eeglFree(mfp);
   }

   return OK;
}

//Read and get all the tags from file st->tag_fname.
//Set "st->stop_searching" to TRUE to stop searching for additional tags.
private void
findtags_get_all_tags(FindTags* st, FindTagsMatchArgs* margs, CS buf_ffname) {
   Tagline      tagp;
   TagSearchInfo   search_info;
   int         retval;
   int         use_cscope = (st->flags & TAG_CSCOPE);
   Hash      hash = 0;

   // This is only to avoid a compiler warning for using search_info uninitialized.
   CLEAR_FIELD(search_info);

   // Read and parse the lines in the file one by one
   for (;;) {
      // check for CTRL-C typed, more often when jumping around
      if (st->state == TS_BINARY || st->state == TS_SKIP_BACK)
         line_breakcheck();
      else
         fast_breakcheck();
      if ((st->flags & TAG_INS_COMP))   // Double brackets for gcc
         ins_compl_check_keys(30, FALSE);
      if (gotInterruptG || ins_compl_interrupted()) {
         st->stop_searching = TRUE;
         break;
      }
      // When mincount is TAG_MANY, stop when enough matches have been
      // found (for completion).
      if (st->mincount == TAG_MANY && st->match_count >= TAG_MANY) {
         st->stop_searching = TRUE;
         break;
      }
      if (st->get_searchpat)
         goto line_read_in;

      retval = findtags_get_next_line(st, &search_info);
      if (retval == TAGS_READ_IGNORE)
         continue;
      if (retval == TAGS_READ_EOF)
         break;

   line_read_in:

      // When still at the start of the file, check for Emacs tags file
      // format, and for "not sorted" flag.
      if (st->state == TS_START) {
         if (findtags_start_state_handler(st, &margs->sortic, &search_info) == FALSE)
            continue;
      }

      // When the line is too long the ZERO will not be in the last-but-one byte 
      // (see eeFgets()). Has been reported for Mozilla JS with extremely long names.
      // In that case we need to increase lbuf_size.
      if (st->lbuf[st->lbuf_size - 2] != ZERO && !use_cscope ) {
         st->lbuf_size *= 2;
         eeglFree(st->lbuf);
         st->lbuf = alloc(st->lbuf_size);

         if (st->state == TS_STEP_FORWARD || st->state == TS_LINEAR)
            // Seek to the same position to read the same line again
            (void)fseeko(st->fp, search_info.curr_offset, SEEK_SET);
         // this will try the same thing again, make sure the offset is different
         search_info.curr_offset = 0;
         continue;
      }

      retval = findtags_parse_line(st, &tagp, margs, &search_info);
      if (retval == TAG_MATCH_NEXT)
          continue;
      if (retval == TAG_MATCH_STOP)
          break;
      if (retval == TAG_MATCH_FAIL) {
          showErrFmtMsg(_(e_format_error_in_tags_file_str), st->tag_fname);
          if (!use_cscope)
         showErrFmtMsg(_("Before byte %ld"), (long)ftello(st->fp));
          st->stop_searching = TRUE;
          return;
      }

      // If a match is found, add it to ht_match[] and ga_match[].
      if (findtags_match_tag(st, &tagp, margs)
            && findtags_add_match(st, &tagp, margs, buf_ffname, &hash) == FAIL)
         break;
   } // forever
}

//Search for tags matching "st->orgpat->pat" in the "st->tag_fname" tags file. Information needed 
//to search for the tags is in the "st" state structure. The matching tags are returned in "st". 
//If an error is encountered, then "st->stop_searching" is set to TRUE.
private void
findtags_in_file(FindTags* st, CS buf_ffname) {
   FindTagsMatchArgs margs;
   int      use_cscope = (st->flags & TAG_CSCOPE);

   st->tag_file_sorted = ZERO;
   st->fp = NULL;
   findtags_matchargs_init(&margs, st->flags);

   // A file that doesn't exist is silently ignored.  Only when not a
   // single file is found, an error message is given (further on).
   if (use_cscope)
      st->fp = NULL;       // avoid GCC warning
   else {
      if (curBook->kind == BOOK_HELP && !findtags_in_help_init(st))
         return;

      st->fp = FOPEN(st->tag_fname, "r");
      if (st->fp == NULL)
          return;

      if (p_verbose >= 5) {
          verbose_enter();
          smsg(_("Searching tags file %s"), st->tag_fname);
          verbose_leave();
      }
   }
   st->did_open = TRUE;   // remember that we found at least one file

   st->state = TS_START;   // we're at the start of the file

   // Read and parse the lines in the file one by one
   findtags_get_all_tags(st, &margs, buf_ffname);

   if (st->fp) {
      fclose(st->fp);
      st->fp = NULL;
   }

   if (margs.sort_error)
      showErrFmtMsg(_(e_tags_file_not_sorted_str), st->tag_fname);

   // Stop searching if sufficient tags have been found.
   if (st->match_count >= st->mincount)
      st->stop_searching = TRUE;
}

//Copy the tags found by find_tags() to "matchesp". Return the number of matches copied.
private int
findtags_copy_matches(FindTags* st, OUT ExpandMatch* targetMatches) {
   int      name_only = (st->flags & TAG_NAMES);
   ExpandMatch matches = {};
   int      mtt;
   int      i;
   CS mfp;
   CS p;

   if (st->match_count > 0) {
      matches.c = ALLOC_MULT(CS, st->match_count);
      matches.len = st->match_count;
   } else {
      matches.c = null;
      matches.len = 0;
   } 
   st->match_count = 0;
   for (mtt = 0; mtt < MT_COUNT; ++mtt) {
      for (i = 0; i < st->ga_match[mtt].len; ++i) {
         mfp = ((Byte **)(st->ga_match[mtt].c))[i];
         if (!matches.c)
            eeglFree(mfp);
         else {
            if (!name_only) {
               // Change mtt back to zero-based.
               *mfp = *mfp - 1;

               // change the TAG_SEP back to ZERO
               for (p = mfp + 1; *p != ZERO; ++p) {
                  if (*p == TAG_SEP)
                     *p = ZERO;
               }
            }
            matches.c[st->match_count] = mfp;
            st->match_count++;
         }
      }

      ga_clear(&st->ga_match[mtt]);
      hash_clear(&st->ht_match[mtt]);
   }

   *targetMatches = matches;
   return st->match_count;
}

//find_tags() - search for tags in tags files
//
//Return FAIL if search completely failed (*num_matches will be 0, *matchesp
//will be NULL), OK otherwise.
//
//Priority depending on which type of tag is recognized:
// 6.   A static or global tag with a full matching tag for the current file.
// 5.   A global tag with a full matching tag for another file.
// 4.   A static tag with a full matching tag for another file.
// 3.   A static or global tag with an ignore-case matching tag for the
//  current file.
// 2.   A global tag with an ignore-case matching tag for another file.
// 1.   A static tag with an ignore-case matching tag for another file.
//
//Tags in an emacs-style tags file are always global.
//
//flags:
//TAG_HELP     only search for help tags
//TAG_NAMES     only return name of tag
//TAG_REGEXP     use "pat" as a regexp
//TAG_NOIC     don't always ignore case
//TAG_KEEP_LANG  keep language
//TAG_CSCOPE     use cscope results for tags
//TAG_NO_TAGFUNC do not call the 'tagfunc' function
int
find_tags(
   CS pat,         // pattern to search for
   Unt flags,
   int mincount,      // MAXCOL: find all matches. other: minimal number of matches
   CS buf_ffname,      // name of buffer for priority
   OUT ExpandMatch* matches
){
   FindTags   st;
   TagName   tn;         // info for get_tagfname()
   int      first_file;      // trying first tag file
   int      retval = FAIL;      // return value
   int      round;

   int      save_emsg_off;

   int      i;
   CS saved_pat = NULL;      // copy of pat[]

   int findall = (mincount == MAXCOL || mincount == TAG_MANY);
                  // find all matching tags
   int has_re = (flags & TAG_REGEXP);   // regexp used
   int noic = (flags & TAG_NOIC);
   int use_cscope = (flags & TAG_CSCOPE);
   int verbose = (flags & TAG_VERBOSE);
   int save_p_ic = p_ic;

   //Change the value of @ignorecase according to @tagcase for the duration of this function.
   switch (curBook->o.tagCase) {
   case TC_FOLLOWIC:       break;
   case TC_IGNORE:    p_ic = TRUE;  break;
   case TC_MATCH:     p_ic = FALSE; break;
   case TC_FOLLOWSCS: p_ic = ignorecase(pat); break;
   case TC_SMART:     p_ic = ignorecase_opt(pat, TRUE, TRUE); break;
   }

   Unt kindSave = curBook->kind; // eegl.h/BOOK_ constants

   if (findtags_state_init(&st, pat, flags, mincount) == FAIL)
      goto findtag_end;

   STRCPY(st.tag_fname, "from cscope");   // for error messages

   // Initialize a few variables
   if (st.help_only)            // want tags from help file
      curBook->kind = BOOK_HELP;         // will be restored later
   ei (use_cscope) {
      // Make sure we don't mix help and cscope, confuses Coverity.
      st.help_only = FALSE;
      curBook->kind = BOOK_NORMAL;
   }

   if (curBook->kind == BOOK_HELP) {
      // When "@ab" is specified use only the "ab" language, otherwise
      // search all languages.
      if (st.orgpat->len > 3 && pat[st.orgpat->len - 3] == '@'
               && ASCII_ISALPHA(pat[st.orgpat->len - 2])
               && ASCII_ISALPHA(pat[st.orgpat->len - 1]))
      {
         saved_pat = copySubstr(pat, st.orgpat->len - 3);
         if (saved_pat != NULL) {
            st.help_lang_find = &pat[st.orgpat->len - 2];
            st.orgpat->pat = saved_pat;
            st.orgpat->len -= 3;
         }
      }
   }

   save_emsg_off = emsg_off;
   emsg_off = TRUE;  // don't want error for invalid RE here
   prepare_pats(st.orgpat, has_re);
   emsg_off = save_emsg_off;
   if (has_re && st.orgpat->regmatch.regprog == NULL)
      goto findtag_end;

   retval = findtags_apply_tfu(&st, pat, buf_ffname);
   if (retval != NOTDONE)
      goto findtag_end;

   // re-initialize the default return value
   retval = FAIL;

   // Set a flag if the file extension is .txt
   if ((flags & TAG_KEEP_LANG)
          && !st.help_lang_find
          && curBook->currFileName
          && (i = (int)STRLEN(curBook->currFileName)) > 4
          && caseInsensitiveCompare(curBook->currFileName + i - 4, ".txt") == 0)
      st.is_txt = TRUE;

   //When finding a specified number of matches, first try with matching case, so binary search 
   //can be used, and try ignore-case matches in a second loop.
   //When finding all matches, 'tagbsearch' is off, or there is no fixed string to look for, 
   //ignore case right away to avoid going though the tags files twice.
   //When the tag file is case-fold sorted, it is either one or the other.
   //Only ignore case when TAG_NOIC not used or 'ignorecase' set.
   st.orgpat->regmatch.rm_ic = ((p_ic || !noic)
         && (findall || st.orgpat->headlen == 0 || !p_tbs));
   for (round = 1; round <= 2; ++round) {
      st.linear = (st.orgpat->headlen == 0 || !p_tbs || round == 2);

      //Try tag file names from tags option one by one.
      for (first_file = TRUE;
       use_cscope || get_tagfname(&tn, first_file, st.tag_fname) == OK; first_file = FALSE
      ) {
         findtags_in_file(&st, buf_ffname);
         if (st.stop_searching || use_cscope) {
            retval = OK;
            break;
         }
      } // end of for-each-file loop

      if (!use_cscope)
          tagname_free(&tn);

      // stop searching when already did a linear search, or when TAG_NOIC
      // used, and 'ignorecase' not set or already did case-ignore search
      if (st.stop_searching || st.linear || (!p_ic && noic) 
            || st.orgpat->regmatch.rm_ic
      )
          break;
      if (use_cscope)
          break;

      // try another time while ignoring case
      st.orgpat->regmatch.rm_ic = TRUE;
   }

   if (!st.stop_searching) {
      if (!st.did_open && verbose)   // never opened any tags file
         emsg(_(e_no_tags_file));
      retval = OK;      // It's OK even when no tag found
   }

findtag_end:
    findtags_state_free(&st);

   //Move the matches from the ga_match[] arrays into one list of matches. When retval == FAIL, 
   //free the matches.
   if (retval == FAIL)
      st.match_count = 0;

   matches->len = findtags_copy_matches(&st, matches);

   curBook->kind = kindSave;
   eeglFree(saved_pat);

   p_ic = save_p_ic;

   return retval;
}

private ArrayList tag_fnames = GA_EMPTY;

//Callback for finding all "tags" and "tags-??" files in doc directories.
private void
found_tagfile_cb(CS fname, void* cookie UNUSED) {
   if (ga_grow(&tag_fnames, 1) == FAIL)
      return;

   Byte   *tag_fname = copyStr(fname);

   simplify_filename(tag_fname);
   ((Byte **)(tag_fnames.c))[tag_fnames.len++] = tag_fname;
}

#if defined(EXITFREE) || defined(PROTO)
   void
free_tag_stuff(void) {
   ga_clear_strings(&tag_fnames);
   if (curPor != NULL)
      do_tag(NULL, DT_FREE, 0, 0, 0);
   tag_freematch();

   tagstack_clear_entry(&ptag_entry);
}
#endif

//Get the next name of a tag file from the tag file list. For help files, use "tags" file only.
//Return FAIL if no more tag file names, OK otherwise.
int
get_tagfname(
   TagName   *tnp,   // holds status info
   int      first,   // TRUE when first file name is wanted
   OUT CS buf)   // pointer to buffer of MAXPATHL chars
{
   CS fname = NULL;
   CS r_ptr;
   int i;

   if (first)
      CLEAR_POINTER(tnp);

   if (curBook->kind == BOOK_HELP) {
      //For help files it's done in a completely different way:
      //Find "doc/tags" and "doc/tags-??" in all directories in 'runtimepath'.
      if (first) {
         ga_clear_strings(&tag_fnames);
         ga_init2(&tag_fnames, sizeof(CS), 10);
         do_in_runtimepath((CS) "tags tags-??", DIP_ALL, found_tagfile_cb, NULL);
      }

      if (tnp->tn_hf_idx >= tag_fnames.len) {
         // Not found in 'runtimepath', use 'helpfile', if it exists and
         // wasn't used yet, replacing "help.txt" with "tags".
         if (tnp->tn_hf_idx > tag_fnames.len)
            return FAIL;
         ++tnp->tn_hf_idx;
         STRCPY(buf, MAIN_HELPFILE);
         STRCPY(gettail(buf), "tags");
         simplify_filename(buf);

         for (i = 0; i < tag_fnames.len; ++i) {
            if (STRCMP(buf, ((Byte **)(tag_fnames.c))[i]) == 0)
                return FAIL; // avoid duplicate file names
         } 
      } else
         copySubstrToAllocation(
            buf, (Text){((Byte **)(tag_fnames.c))[ tnp->tn_hf_idx++], MAXPATHL - 1}
         );
      return OK;
   }

   if (first) {
      // Init. We make a copy of 'tags', because autocommands may change
      // the value without notifying us.
      tnp->tn_tags = copyStr(curBook->o.tags);
      tnp->tn_np = tnp->tn_tags;
   }

   //Loop until we have found a file name that can be used. There are two states:
   //tnp->tn_did_filefind_init == FALSE: setup for next part in 'tags'.
   //tnp->tn_did_filefind_init == TRUE: find next file in this part.
   for (;;) {
      if (tnp->tn_did_filefind_init) {
         fname = eeFindFile(tnp->searchCtx);
         if (fname)
            break;

         tnp->tn_did_filefind_init = FALSE;
      } else {
         // Stop when used all parts of 'tags'.
         if (*tnp->tn_np == ZERO) {
            eeFindFile_cleanup(tnp->searchCtx);
            tnp->searchCtx = NULL;
            return FAIL;
         }

         //Copy next file name into buf.
         buf[0] = ZERO;
         (void)copy_option_part(&tnp->tn_np, buf, MAXPATHL - 1, " ,");

         r_ptr = eeFindFile_stopdir(buf);
         // move the filename one char forward and truncate the filepath with a ZERO
         CS filename = gettail(buf);
         if (r_ptr) {
            STRMOVE(r_ptr + 1, r_ptr);
            ++r_ptr;
         }
         STRMOVE(filename + 1, filename);
         *filename++ = ZERO;

         tnp->searchCtx = eeFindFile_init(
             buf, 
             text(filename),
             r_ptr, 100,
             FALSE,      // don't free visited list
             FINDFILE_FILE, // we search for a file
             tnp->searchCtx, TRUE, curBook->fullFileName
         );
         if (tnp->searchCtx)
            tnp->tn_did_filefind_init = TRUE;
      }
   }

   STRCPY(buf, fname);
   eeglFree(fname);
   return OK;
}

//Free the contents of a TagName that was filled by get_tagfname().
void
tagname_free(TagName *tnp) {
   eeglFree(tnp->tn_tags);
   eeFindFile_cleanup(tnp->searchCtx);
   tnp->searchCtx = NULL;
   ga_clear_strings(&tag_fnames);
}


//Parse one line from the tags file. Find start/end of tag name, start/end of
//file name and start of search pattern.
//
//If is_etag is TRUE, tagp->fname and tagp->fname_end are not set.
//
//Return FAIL if there is a format error in this line, OK otherwise.
private int
parse_tag_line(CS lbuf, Tagline* tagp) {
   // Isolate the tagname, from lbuf up to the first white
   tagp->tagname = lbuf;
   CS p = firstOccurrence(lbuf, TAB);
   if (!p)
      return FAIL;
   tagp->tagname_end = p;

   // Isolate file name, from first to second white space
   if (*p != ZERO)
      ++p;
   tagp->fname = p;
   p = firstOccurrence(p, TAB);
   if (p == NULL)
      return FAIL;
   tagp->fname_end = p;

   // find start of search command, after second white space
   if (*p != ZERO)
      ++p;
   if (*p == ZERO)
      return FAIL;
   tagp->command = p;

   return OK;
}

//Check if tagname is a static tag
//
//Static tags produced by the older ctags program have the format:
//  'file:tag  file  /pattern'.
//This is only recognized when both occurrence of 'file' are the same, to
//avoid recognizing "string::string" or ":exit".
//
//Static tags produced by the new ctags program have the format:
//  'tag  file  /pattern/;"<Tab>file:'       "
//
//Return TRUE if it is a static tag and adjust *tagname to the real tag.
//Return FALSE if it is not a static tag.
private int
test_for_static(Tagline* tagp) {
   //Check for new style static tag ":...<Tab>file:[<Tab>...]"
   Byte* p = tagp->command;
   while ((p = firstOccurrence(p, '\t')) != NULL) {
      ++p;
      if (STRNCMP(p, "file:", 5) == 0)
         return TRUE;
   }

   return FALSE;
}

//Return the length of a matching tag line.
private Unt
matching_line_len(CS lbuf) {
   CS p = lbuf + 1;

   // does the same thing as parse_match()
   p += STRLEN(p) + 1;
   return (p - lbuf) + STRLEN(p);
}

//Parse a line from a matching tag.  Does not change the line itself.
//
//The line that we get looks like this:
//Emacs tag: <mtt><tag_fname><ZERO><ebuf><ZERO><lbuf>
//other tag: <mtt><tag_fname><ZERO><ZERO><lbuf>
//without Emacs tags: <mtt><tag_fname><ZERO><lbuf>
//
//Return OK or FAIL.
private int
parse_match(
   CS lbuf,       // input: matching line
   OUT Tagline* tagp)       // output: pointers into the line
{
   int      retval;
   Byte   *pc, *pt;

   tagp->tag_fname = lbuf + 1;
   lbuf += STRLEN(tagp->tag_fname) + 2;
   // Find search pattern and the file name for non-etags.
   retval = parse_tag_line(lbuf, tagp);

   tagp->tagkind = NULL;
   tagp->user_data = NULL;
   tagp->c = 0;
   tagp->command_end = NULL;

   if (retval != OK)
      return retval;

   // Try to find a kind field: "kind:<kind>" or just "<kind>"
   CS p = tagp->command;
   if (find_extra(OUT &p) == OK) {
      if (p > tagp->command && p[-1] == '|')
         tagp->command_end = p - 1;  // drop trailing bar
      else
         tagp->command_end = p;
      p += 2;   // skip ";\""
      if (*p++ == TAB)
         // Accept ASCII alphabetic kind characters and any multi-byte character.
         while (ASCII_ISALPHA(*p) || utfCharLen(p) > 1) {
            if (STRNCMP(p, "kind:", 5) == 0)
               tagp->tagkind = p + 5;
            ei (STRNCMP(p, "user_data:", 10) == 0)
               tagp->user_data = p + 10;
            ei (STRNCMP(p, "line:", 5) == 0)
               tagp->c = ATOI(p + 5);
            if (tagp->tagkind != NULL && tagp->user_data != NULL)
               break;
            pc = firstOccurrence(p, ':');
            pt = firstOccurrence(p, '\t');
            if (pc == NULL || (pt != NULL && pc > pt))
               tagp->tagkind = p;
            if (pt == NULL)
               break;
            p = pt;
            MB_PTR_ADV(p);
         }
   }
   if (tagp->tagkind != NULL) {
      for (p = tagp->tagkind; *p && *p != '\t' && *p != '\r' && *p != '\n'; MB_PTR_ADV(p))
         {} 
      tagp->tagkind_end = p;
   }
   if (tagp->user_data != NULL) {
      for (p = tagp->user_data; *p && *p != '\t' && *p != '\r' && *p != '\n'; MB_PTR_ADV(p))
         {} 
      tagp->user_data_end = p;
   }
   return retval;
}

//Find out the actual file name of a tag.  Concatenate the tags file name
//with the matching tag file name.
//Return an allocated string or NULL (out of memory).
private CS
tag_full_fname(Tagline* tagp) {
   int c = *tagp->fname_end;
   *tagp->fname_end = ZERO;
   CS fullname = expand_tag_fname(tagp->fname, tagp->tag_fname, FALSE);

   *tagp->fname_end = c;
   return fullname;
}

//Jump to a tag that has been found in one of the tag files
//return OK for success, NOTAGFILE when file not found, FAIL otherwise.
private int
jumpto_tag(
   CS lbuf_arg,   // line from the tags file for this tag
   int forceit,   // :ta with !
   int keep_help)   // keep help flag (FALSE for cscope)
{
   int      save_p_scs, save_p_ic;
   LineNr   save_lnum;
   CS pbuf_end;
   int      retval = FAIL;
   int      getfile_result = GETFILE_UNUSED;
   int      search_options;
   Boole      saveHlsearch;
   Portal   *curPor_save = NULL;
   Byte   *full_fname = NULL;
   int      old_KeyTyped = KeyTyped;    // getting the file may reset it

   if (postponed_split == 0 && !portCheckCanSetCurBookForceIt(forceit))
      return FAIL;

   //Make a copy of the line, it can become invalid when an autocommand calls back here recursively
   Unt len = matching_line_len(lbuf_arg) + 1;
   CS lbuf = alloc(len);
   mch_memmove(lbuf, lbuf_arg, len);

   CS pbuf = allocZeroed(LSIZE);

   // parse the match line into the tagp structure
   Tagline tagp;
   if (lbuf == NULL || parse_match(lbuf, &tagp) == FAIL) {
      tagp.fname_end = NULL;
      goto erret;
   }

   // truncate the file name, so it can be used as a string
   *tagp.fname_end = ZERO;
   CS fname = tagp.fname;

   // copy the command to pbuf[], remove trailing CR/NL
   CS str = tagp.command;
   int isdigit = 0;
   if (EE_ISDIGIT(*str)) {
      // need to inject a ':' for a proper Vim9 :nr command
      isdigit = 1;
      pbuf[0] = ':';
   }
   for (pbuf_end = pbuf + isdigit; *str && *str != '\n' && *str != '\r'; ) {
      *pbuf_end++ = *str++;
      if (pbuf_end - pbuf + 1 + isdigit >= LSIZE)
         break;
   }
   *pbuf_end = ZERO;

   //Remove the "<Tab>fieldname:value" stuff; we don't need it here.
   str = pbuf;
   // skip over the ':'
   if (isdigit != 0)
       str++;
   if (find_extra(OUT &str) == OK) {
       pbuf_end = str;
       *pbuf_end = ZERO;
   }

   //Expand file name, when needed (for environment variables).
   //If 'tagrelative' option set, may change file name.
   fname = expand_tag_fname(fname, tagp.tag_fname, TRUE);
   CS tofree_fname = fname;   // free() it later

   //Check if the file with the tag exists before abandoning the current file. Also accept a file
   //name for which there is a matching BufReadCmd autocommand event (e.g., http://sys/file).
   if (mch_getperm(fname) < 0 && !has_autocmd(EVENT_BUFREADCMD, fname, NULL)) {
      retval = NOTAGFILE;
      eeglFree(nofile_fname);
      nofile_fname = copyStr(fname);
      if (nofile_fname == NULL)
         nofile_fname = E;
      goto erret;
   }

   ++isRedrawingDisabledG;

   if (g_do_tagpreview != 0) {
      postponed_split = 0;   // don't split again below
      curPor_save = curPor;   // Save current window

      //If we are reusing a portal, we may change dir when
      //entering it (autocommands) so turn the tag filename into a fullpath
      if (!curPor->isPreview) {
         full_fname = FullName_save(fname, FALSE);
         fname = full_fname;

         //Make the preview window the current window. Open a preview window when needed.
         prepare_tagpreview(TRUE, TRUE, FALSE);
      }
   }

   // If it was a CTRL-W CTRL-] command split window now.  For ":tab tag" open a new tab.
   if (postponed_split && (p_swb & (SWB_USEOPEN | SWB_USETAB)) != 0) {
      Book* existingBook = booklistFindByNameExpandingLinks(fname);

      if (existingBook) {
         // If @switchbook is set, jump to the portal containing "book".
         if (switchBufGotoPortalIntoBuf(existingBook) != NULL)
            // We've switched to the book, the usual loading of the file must be skipped.
            getfile_result = GETFILE_SAME_FILE;
      }
   }
   if (getfile_result == GETFILE_UNUSED && (postponed_split || commModifierG.cmod_tab != 0)) {
      if (splitPortal(postponed_split > 0 ? postponed_split : 0, postponed_split_flags) == FAIL) {
         if (isRedrawingDisabledG > 0)
            --isRedrawingDisabledG;
         goto erret;
      }
      RESET_BINDING(curPor);
   }

   if (keep_help) {
      // A :ta from a help file will keep the kind == BOOK_HELP.  For ":ptag"
      // we need to use the flag from the window where we came from.
      if (g_do_tagpreview != 0)
         keep_help_flag = bookIsHelp(curPor_save->book);
      else
         keep_help_flag = curBook->kind == BOOK_HELP;
   }

   if (getfile_result == GETFILE_UNUSED)
      // Careful: getfile() may trigger autocommands and call jumpto_tag() recursively.
      getfile_result = getfile(0, fname, NULL, TRUE, (LineNr)0, forceit);
   keep_help_flag = FALSE;

   if (GETFILE_SUCCESS(getfile_result)) {  // got to the right file
      curPor->setCursWant = TRUE;
      postponed_split = 0;

      // Save value of hlsearch, jumping to a tag is not a real search
      saveHlsearch = hiliteSearchG;
      // getfile() may have cleared options, apply 'previewpopup' again.

      // the search pattern is not stored
      search_options = SEARCH_KEEP;

      //If the command is a search, try here.
      //
      //Reset @smartcase for the search, since the search pattern was not typed by the user.
      //Only use do_search() when there is a full search command, without anything following.
      str = pbuf;
      if (pbuf[0] == '/' || pbuf[0] == '?')
         str = skip_regexp(pbuf + 1, pbuf[0], FALSE) + 1;
      if (str > pbuf_end - 1) {  // search command with nothing following
         Unt pbuflen = pbuf_end - pbuf;

         save_p_ic = p_ic;
         save_p_scs = p_scs;
         wrapSearchG = true;   // need to wrap for backward searches
         p_ic = FALSE;   // don't ignore case now
         p_scs = FALSE;
         save_lnum = curPor->cursor.lnum;
         if (tagp.c > 0)
            // start search before line from "line:" field
            curPor->cursor.lnum = tagp.c - 1;
         else
            // start search before first line
            curPor->cursor.lnum = 0;
         if (do_search(NULL, pbuf[0], pbuf[0], pbuf + 1, pbuflen - 1, (long)1,
                         search_options, NULL))
            retval = OK;
         else {
            int   found = 1;
            int   cc;

            // try again, ignore case now
            p_ic = TRUE;
            if (!do_search(NULL, pbuf[0], pbuf[0], pbuf + 1, pbuflen - 1, (long)1,
                            search_options, NULL)
            ) {
               //Failed to find pattern, take a guess: "^func  ("
               found = 2;
               (void)test_for_static(&tagp);
               cc = *tagp.tagname_end;
               *tagp.tagname_end = ZERO;
               pbuflen = eeSnprintf(pbuf, LSIZE, "^%s\\s\\*(", tagp.tagname);
               if (!do_search(NULL, '/', '/', pbuf, pbuflen, (long)1, search_options, NULL)) {
                  // Guess again: "^char * \<func  ("
                  pbuflen = eeSnprintf(
                        pbuf, LSIZE, "^\\[#a-zA-Z_]\\.\\*\\<%s\\s\\*(", tagp.tagname
                  );
                  if (!do_search(NULL, '/', '/', pbuf, pbuflen, (long)1, search_options, NULL))
                     found = 0;
               }
               *tagp.tagname_end = cc;
            }
            if (found == 0) {
               emsg(_(e_cannot_find_tag_pattern));
               curPor->cursor.lnum = save_lnum;
            } else {
               //Only give a message when really guessed, not when 'ic'
               //is set and match found while ignoring case.
               if (found == 2 || !save_p_ic) {
                  msg(_(e_couldnt_find_tag_just_guessing));
                  if (!msg_scrolled && msg_silent == 0) {
                     out_flush();
                     ui_delay(1010L, TRUE);
                  }
               }
               retval = OK;
            }
         }
         wrapSearchG = true;
         p_ic = save_p_ic;
         p_scs = save_p_scs;

         // A search command may have positioned the cursor beyond the end
         // of the line. May need to correct that here.
         check_cursor();
      } else {
         curPor->cursor.lnum = 1;      // start command in line 1
         curPor->cursor.col = 0;
         curPor->cursor.coladd = 0;
         executeCommLine(pbuf);
         retval = OK;
      }

      // restore hiliteSearchG when keeping the old search pattern
      if (search_options)
         setHlsearch(saveHlsearch);

      // Return OK if jumped to another file (at least we found the file!).
      if (getfile_result == GETFILE_OPEN_OTHER)
         retval = OK;

      if (retval == OK) {
         //For a help book: Put the cursor line at the top of the portal,
         //the help subject will be below it.
         if (curBook->kind == BOOK_HELP)
            set_topline(curPor, curPor->cursor.lnum);
         if ((p_fdo & FDO_TAG) && old_KeyTyped)
            foldOpenCursor();
      }

      if (g_do_tagpreview != 0 && curPor != curPor_save && portalIsValid(curPor_save)) {
          // Return cursor to where we were
          validate_cursor();
          redraw_later(UPD_VALID);
          enterPortal(curPor_save, TRUE);
      }

      if (isRedrawingDisabledG > 0)
         --isRedrawingDisabledG;
   } else {
      if (isRedrawingDisabledG > 0)
         --isRedrawingDisabledG;
      gotInterruptG = FALSE;  // don't want entering window to fail
      if (postponed_split) {     // close the window
         closePortal(curPor, FALSE);
         postponed_split = 0;
      } ei (PORTAL_IS_POPUP(curPor)) {
         Portal* po = curPor;

         if (portalIsValid(curPor_save))
            enterPortal(curPor_save, TRUE);
         popup_close(po->id, FALSE);
      }
   }
   if (PORTAL_IS_POPUP(curPor))
      // something went wrong, still in popup, but it can't have focus
      enterPortal(firstPor, TRUE);

erret:
   g_do_tagpreview = 0; // For next time
   eeglFree(lbuf);
   eeglFree(pbuf);
   eeglFree(tofree_fname);
   eeglFree(full_fname);

   return retval;
}

//If "expand" is TRUE, expand wildcards in fname.
//If @tagrelative option set, change fname (name of file containing tag)
//according to tag_fname (name of tag file containing fname). Return a pointer to allocated memory
private CS
expand_tag_fname(CS fname, CS tag_fname, int expand) {
   CS p;
   CS retval;
   CS expanded_fname = NULL;
   Expand xpc;

   // Expand file name (for environment variables) when needed.
   if (expand && mch_has_wildcard(fname)) {
      expandInit(&xpc);
      xpc.context = EXPAND_FILES;
      expanded_fname = expandWildcard(
         &xpc, fname, NULL, WILD_LIST_NOTFOUND|WILD_SILENT, WILD_EXPAND_FREE
      );
      if (expanded_fname)
         fname = expanded_fname;
   }

   if (!eeIsAbsName(fname) && (p = gettail(tag_fname)) != tag_fname) {
      retval = alloc(MAXPATHL);
      STRCPY(retval, tag_fname);
      copySubstrToAllocation(
         retval + (p - tag_fname), (Text){fname, MAXPATHL - (p - tag_fname) - 1}
      );
      //Translate names like "src/a/../b/file.c" into "src/b/file.c".
      simplify_filename(retval);
   } else
      retval = copyStr(fname);

   eeglFree(expanded_fname);

   return retval;
}

//Check if we have a tag for the buffer with name "buf_ffname".
//This is a bit slow, because of the full path compare in fullpathcmp().
//Return TRUE if tag for file "fname" if tag file "tag_fname" is for current file.
private int
test_for_current(CS fname, CS fname_end, CS tag_fname, CS buf_ffname) {
   Bool retval = false;

   if (buf_ffname) {   // if the buffer has a name
      Unt c = *fname_end;
      *fname_end = ZERO;
      CS fullname = expand_tag_fname(fname, tag_fname, TRUE);
      retval = (fullpathcmp(fullname, buf_ffname, TRUE, TRUE) & FPC_SAME) != 0 ;
      eeglFree(fullname);
      *fname_end = c;
   }

   return retval;
}

//Find the end of the tag address. Return OK if ";\"" is following, FAIL otherwise.
private int
find_extra(OUT CS* pp) {
   CS str = *pp;
   Byte first_char = **pp;

   // Repeat for addresses separated with ';'
   for (;;) {
      if (EE_ISDIGIT(*str))
          str = skipdigits(str + 1);
      ei (*str == '/' || *str == '?') {
         str = skip_regexp(str + 1, *str, FALSE);
         if (*str != first_char)
            str = NULL;
         else
            ++str;
      } else {
         // not a line number or search string, look for terminator.
         str = STRSTR(str, "|;\"");
         if (str) {
            ++str;
            break;
         }
      }
      if (!str || *str != ';' || !(EE_ISDIGIT(str[1]) || str[1] == '/' || str[1] == '?'))
         break;
      ++str;   // skip ';'
      first_char = *str;
   }

   if (str != NULL && STRNCMP(str, ";\"", 2) == 0) {
      *pp = str;
      return OK;
   }
   return FAIL;
}

//Free a single entry in a tag stack
void
tagstack_clear_entry(Taggy* item) {
   EE_CLEAR(item->tagname);
   EE_CLEAR(item->user_data);
}

int
expand_tags(Boole expandTagNames, CS pat, OUT ExpandMatch* matches) {
   Unt   name_buf_size = 100;
   Tagline   tagline;
   int      ret;

   CS namebuf = alloc(name_buf_size);

   Unt extraFlag = expandTagNames ? TAG_NAMES : 0;
   if (pat[0] == '/')
      ret = find_tags(
         pat + 1, TAG_REGEXP | extraFlag | TAG_VERBOSE | TAG_NO_TAGFUNC,
         TAG_MANY, curBook->fullFileName, OUT matches
      );
   else
      ret = find_tags(
         pat, TAG_REGEXP | extraFlag | TAG_VERBOSE | TAG_NO_TAGFUNC | TAG_NOIC,
         TAG_MANY, curBook->fullFileName, OUT matches);
   if (ret == OK && !expandTagNames) {
      // Reorganize the tags for display and matching as strings of:
      // "<tagname>\0<kind>\0<filename>\0"
      for (Unt i = 0; i < matches->len; i++) {
          parse_match(matches->c[i], &tagline);
          Unt len = tagline.tagname_end - tagline.tagname;
          if (len > name_buf_size - 3) {
             name_buf_size = len + 3;
             CS buf = eeRealloc(namebuf, name_buf_size);
             namebuf = buf;
         }

         mch_memmove(namebuf, tagline.tagname, len);
         namebuf[len++] = 0;
         namebuf[len++] = (tagline.tagkind && *tagline.tagkind) ? *tagline.tagkind : 'f';
         namebuf[len++] = 0;
         mch_memmove(matches->c[i] + len, tagline.fname, tagline.fname_end - tagline.fname);
         matches->c[i][len + (tagline.fname_end - tagline.fname)] = 0;
         mch_memmove(matches->c[i], namebuf, len);
      }
   }

   eeglFree(namebuf);
   return ret;
}

//Add a tag field to the dictionary "dict". Return OK or FAIL.
private int
add_tag_field(
   Bag* dict,
   CS field_name,
   CS start,      // start of the value
   CS end      // after the value; can be NULL
){
   int      len = 0;
   int      retval;

   // check that the field name doesn't exist yet
   if (bagHasKey(dict, mbText(field_name))) {
      if (p_verbose > 0) {
          verbose_enter();
          smsg(_("Duplicate field name: %s"), field_name);
          verbose_leave();
      }
      return FAIL;
   }
   Byte buf[MAXPATHL];
      
   if (start) {
      if (!end) {
         end = start + STRLEN(start);
         while (end > start && (end[-1] == '\r' || end[-1] == '\n'))
            --end;
      }
      len = (int)(end - start);
      if (len > MAXPATHL - 1)
         len = MAXPATHL - 1;
      copySubstrToAllocation(buf, (Text){start, len});
   }
   buf[len] = ZERO;
   retval = bagAddString(dict, field_name, buf);
   return retval;
}

//Add the tags matching the specified pattern "pat" to the list "list"
//as a dictionary. Use "buf_fname" for priority, unless NULL.
int
get_tags(List* list, CS pat, CS buf_fname) {
   CS p;
   CS full_fname;
   Bag* bag;
   Tagline tp;
   long is_static;
   ExpandMatch matches = {};
   matches.a = createArena();

   int ret = find_tags(pat, TAG_REGEXP | TAG_NOIC, (int)MAXCOL, buf_fname, OUT &matches);
   if (ret != OK || matches.len == 0)
      return ret;

   for (Unt i = 0; i < matches.len; ++i) {
      if (parse_match(matches.c[i], &tp) == FAIL) {
         eeglFree(matches.c[i]);
         continue;
      }

      is_static = test_for_static(&tp);

      // Skip pseudo-tag lines.
      if (STRNCMP(tp.tagname, "!_TAG_", 6) == 0) {
         eeglFree(matches.c[i]);
         continue;
      }

      bag = allocBag();
      if (listAppendBag(list, bag) == FAIL)
         ret = FAIL;

      full_fname = tag_full_fname(&tp);
      if (add_tag_field(bag, S"name", tp.tagname, tp.tagname_end) == FAIL
            || add_tag_field(bag, S"filename", full_fname, NULL) == FAIL
            || add_tag_field(bag, S"cmd", tp.command, tp.command_end) == FAIL
            || add_tag_field(bag, S"kind", tp.tagkind, tp.tagkind_end) == FAIL
            || bagAddNumber(bag, S"static", is_static) == FAIL)
         ret = FAIL;

      eeglFree(full_fname);

      if (tp.command_end) {
         for (p = tp.command_end + 3;
             *p != ZERO && *p != '\n' && *p != '\r'; MB_PTR_ADV(p))
         {
            if (p == tp.tagkind || (p + 5 == tp.tagkind && STRNCMP(p, "kind:", 5) == 0))
                // skip "kind:<kind>" and "<kind>"
                p = tp.tagkind_end - 1;
            ei (STRNCMP(p, "file:", 5) == 0)
                // skip "file:" (static tag)
                p += 4;
            ei (!SPACE_OR_TAB(*p)) {
               CS s;

               // Add extra field as a bag entry. Fields are separated by Tabs.
               CS n = p;
               while (*p != ZERO && *p >= ' ' && *p < 127 && *p != ':')
                  ++p;
               int len = (int)(p - n);
               if (*p == ':' && len > 0) {
                  s = ++p;
                  while (*p != ZERO && *p >= ' ')
                     ++p;
                  n[len] = ZERO;
                  if (add_tag_field(bag, n, s, p) == FAIL)
                     ret = FAIL;
                  n[len] = ':';
               } else {
                  // Skip field without colon.
                  while (*p != ZERO && *p >= ' ')
                      ++p;
               } 
               if (*p == ZERO)
                  break;
            }
         }
      }
   }
   deleteArena(matches.a);
   return ret;
}

// Return information about 'tag' in dict 'retBag'.
private void
get_tag_details(Taggy *tag, OUT Bag* retBag) {

   bagAddString(retBag, S"tagname", tag->tagname);
   bagAddNumber(retBag, S"matchnr", tag->cur_match + 1);
   bagAddNumber(retBag, S"bufnr", tag->cur_fnum);
   if (tag->user_data)
      bagAddString(retBag, S"user_data", tag->user_data);

   List* pos;
   if ((pos = list_alloc_id(aid_tagstack_from)) == NULL)
      return;
   bagAddList(retBag, S"from", pos);

   FileMark* fmark = &tag->fmark;
   list_append_number(pos, (Long)(fmark->fnum != -1 ? fmark->fnum : 0));
   list_append_number(pos, (Long)fmark->mark.lnum);
   list_append_number(pos, (Long)(fmark->mark.col == MAXCOL ? MAXCOL : fmark->mark.col + 1));
   list_append_number(pos, (Long)fmark->mark.coladd);
}

//Return the tag stack entries of the specified window 'wp' in dictionary 'retBag'.
void
get_tagstack(Portal* wp, Bag* retBag) {
   bagAddNumber(retBag, S"length", wp->tagStackLen);
   bagAddNumber(retBag, S"curidx", wp->tagStackInd + 1);
   List* l = list_alloc_id(aid_tagstack_items);
   if (!l)
      return;
   bagAddList(retBag, S"items", l);

   for (Unt i = 0; i < wp->tagStackLen; i++) {
      Bag* b = allocBag_id(aid_tagstack_details);
      listAppendBag(l, b);

      get_tag_details(&wp->tagStack[i], b);
   }
}

// Free all the entries in the tag stack of the specified portal
private void
tagstack_clear(Portal* wp) {
   // Free the current tag stack
   for (Unt i = 0; i < wp->tagStackLen; ++i)
      tagstack_clear_entry(&wp->tagStack[i]);
   wp->tagStackLen = 0;
   wp->tagStackInd = 0;
}

//Remove the oldest entry from the tag stack and shift the rest of
//the entries to free up the top of the stack.
private void
tagstack_shift(Portal *wp) {
   Taggy* tagstack = wp->tagStack;

   tagstack_clear_entry(&tagstack[0]);
   for (Unt i = 1; i < wp->tagStackLen; ++i)
      tagstack[i - 1] = tagstack[i];
   wp->tagStackLen--;
}

//Push a new item to the tag stack
private void
tagstack_push_item(
   Portal* wp,
   CS tagname,
   int cur_fnum,
   int cur_match,
   Pos mark,
   int fnum,
   Byte  *user_data
) {
   Taggy   *tagstack = wp->tagStack;
   int      idx = wp->tagStackLen;   // top of the stack

   // if the tagstack is full: remove the oldest entry
   if (idx >= TAGSTACKSIZE) {
      tagstack_shift(wp);
      idx = TAGSTACKSIZE - 1;
   }

   wp->tagStackLen++;
   tagstack[idx].tagname = tagname;
   tagstack[idx].cur_fnum = cur_fnum;
   tagstack[idx].cur_match = cur_match;
   if (tagstack[idx].cur_match == UNT)
      tagstack[idx].cur_match = 0;
   tagstack[idx].fmark.mark = mark;
   tagstack[idx].fmark.fnum = fnum;
   tagstack[idx].user_data = user_data;
}

//Add a list of items to the tag stack in the specified window
private void
tagstack_push_items(Portal* wp, List* l) {
   ListItem   *li;
   DictItem   *di;
   Bag* itemdict;
   Pos   mark;
   int      fnum;

   // Add one entry at a time to the tag stack
   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag != VAR_BAG || li->c.bag == NULL)
         continue;            // Skip non-dict items
      itemdict = li->c.bag;

      // parse 'from' for the cursor position before the tag jump
      if ((di = bagFind(itemdict, tConst("from"))) == NULL)
          continue;
      if (list2fpos(&di->c, &mark, &fnum, NULL, FALSE) != OK)
          continue;
          
      CS  tagname;
      if ((tagname = bagGetString(itemdict, tConst("tagname"), TRUE)) == NULL)
         continue;

      if (mark.col > 0)
          mark.col--;
      tagstack_push_item(wp, tagname,
         (int)bagGetNumber(itemdict, tConst("bufnr")),
         (int)bagGetNumber(itemdict, tConst("matchnr")) - 1,
         mark, fnum,
         bagGetString(itemdict, tConst("user_data"), TRUE));
    }
}

//Set the current index in the tag stack. Valid values are between 0
//and the stack length (inclusive).
private void
tagstack_set_curidx(Portal* po, int curidx) {
   po->tagStackInd = curidx;
   if (po->tagStackInd == UNT)         // sanity check
      po->tagStackInd = 0;
   if (po->tagStackInd > po->tagStackLen)
      po->tagStackInd = po->tagStackLen;
}

//Set the tag stack entries of the specified portal.
//'action' is set to one of:
//  'a' for append
//  'r' for replace
//  't' for truncate
int
set_tagstack(Portal *wp, Bag *d, Unt action) {
   // not allowed to alter the tag stack entries from inside tagfunc
   if (tfu_in_use) {
      emsg(_(e_cannot_modify_tag_stack_within_tagfunc));
      return FAIL;
   }

   List* l = NULL;
   DictItem   *di;
   if ((di = bagFind(d, tConst("items"))) != NULL) {
      if (di->c.tag != VAR_LIST) {
         emsg(_(e_list_required));
         return FAIL;
      }
      l = di->c.list;
   }

   if ((di = bagFind(d, tConst("curidx"))) != NULL)
      tagstack_set_curidx(wp, (int)tv_get_number(&di->c) - 1);

   if (action == 't') {          // truncate the stack
      Taggy   *tagstack = wp->tagStack;
      int   tagstackidx = wp->tagStackInd;
      int   tagstacklen = wp->tagStackLen;

      // delete all the tag stack entries above the current entry
      while (tagstackidx < tagstacklen)
         tagstack_clear_entry(&tagstack[--tagstacklen]);
      wp->tagStackLen = tagstacklen;
   }

   if (l) {
      if (action == 'r')      // replace the stack
         tagstack_clear(wp);

      tagstack_push_items(wp, l);
      // set the current index after the last entry
      wp->tagStackInd = wp->tagStackLen;
   }
   return OK;
}

//{{{Cscope integration

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define CSCOPE_SUCCESS      0
#define CSCOPE_FAILURE      -1

#define   CSCOPE_DBFILE      "cscope.out"
#define   CSCOPE_PROMPT      ">> "

// See ":help cscope-find" for the possible queries.

typedef struct {
   CS name;
   int     (*func)(Invocation* invo);
   CS help;
   CS usage;
   int       cansplit;      // if supports splitting window
} CScopeCommand;

typedef struct csi {
   CS fname;   // cscope db name
   CS ppath;   // path to prepend (the -P option)
   CS flags;   // additional cscope flags/options (e.g, -p2)
   pid_t       pid;   // PID of the connected cscope process.
   dev_t       st_dev;   // ID of dev containing cscope db
   ino_t       st_ino;   // inode number of cscope db

   FILE *       fr_fp;   // from cscope: FILE.
   FILE *       to_fp;   // to cscope: FILE.
} CscopeInfo;

typedef enum { Add, Find, Help, Kill, Reset, Show } csid_e;

typedef enum {
   Store,
   Get,
   Free,
   Print
} Mcmd;

private int       cs_add(Invocation* invo);
private int       cs_add_common(CS, CS, CS);
private int       cs_check_for_connections(void);
private int       cs_check_for_tags(void);
private int       cs_cnt_connections(void);
private int       cs_create_connection(int i);
private void       cs_file_results(FILE *, int *);
private void       cs_fill_results(CS, int , int *, Byte ***, Byte ***, int *);
private int       cs_find(Invocation* invo);
private int       cs_find_common(CS opt, CS pat, Boole, Boole, Boole, CS commline);
private int       cs_help(Invocation* invo);
private int       cs_insert_filelist(CS, CS, CS, FileStat *);
private int       cs_kill(Invocation* invo);
private void       cs_kill_execute(int, CS);
private CScopeCommand *    cs_lookup_cmd(Invocation* invo);
private CS       cs_make_eegl_style_matches(CS, CS, CS, CS);
private CS        cs_manage_matches(Arr(CS), Arr(CS), int, Mcmd);
private void       cs_print_tags_priv(Arr(CS), Arr(CS), int);
private int       cs_read_prompt(int);
private void       cs_release_csp(int, int freefnpp);
private int       cs_reset(Invocation* invo);
private CS cs_resolve_file(int, CS );
private int       cs_show(Invocation* invo);


private CscopeInfo*   csinfo = NULL;
private int       csinfo_size = 0;   // number of items allocated in csinfo[]

private int       eap_arg_len;    // length of invo->arg, set in cs_lookup_cmd()
private CScopeCommand       cs_cmds[] = {
   { S"add",   cs_add,
     S"Add a new database",    S"add file|dir [pre-path] [flags]", 0 },
   { S"find",   cs_find,
     S"Query for a pattern",   S"find a|c|d|e|f|g|i|s|t name", 1 },
   { S"help",   cs_help,
     S"Show this message",     S"help", 0 },
   { S"kill",   cs_kill,
     S"Kill a connection",     S"kill #", 0 },
   { S"reset",   cs_reset,
     S"Reinit all connections", S"reset", 0 },
   { S"show",   cs_show,
     S"Show connections",   S"show", 0 },
   { NULL, NULL, NULL, NULL, 0 }
};

private void
cs_usage_msg(csid_e x) {
   (void)showErrFmtMsg(_(e_usage_cscope_str), cs_cmds[(int)x].usage);
}

private enum {
   EXP_CSCOPE_SUBCMD,   // expand ":cscope" sub-commands
   EXP_SCSCOPE_SUBCMD,   // expand ":scscope" sub-commands
   EXP_CSCOPE_FIND,   // expand ":cscope find" arguments
   EXP_CSCOPE_KILL   // expand ":cscope kill" arguments
} expand_what;

//Function given to expandGeneric() to obtain the cscope command expansion.
CS
get_cscope_name(Expand* xp UNUSED, int idx) {
   int current_idx;
   int i;

   switch (expand_what) {
   case EXP_CSCOPE_SUBCMD:
      // Complete with sub-commands of ":cscope": add, find, help, kill, reset, show
      return (CS)cs_cmds[idx].name;
   case EXP_SCSCOPE_SUBCMD:
      // Complete with sub-commands of ":scscope": same sub-commands as
      // ":cscope" but skip commands which don't support split portals
      for (i = 0, current_idx = 0; cs_cmds[i].name != NULL; i++) {
         if (cs_cmds[i].cansplit && current_idx++ == idx)
            break;
      } 
      return (CS)cs_cmds[i].name;
   case EXP_CSCOPE_FIND: {
      const CS query_type[] = {SMAP((CS), 
            "a", "c", "d", "e", "f", "g", "i", "s", "t", NULL 
      )};

      // Complete with query type of ":cscope find {query_type}".
      // {query_type} can be letters (c, d, ... a) or numbers (0, 1,
      // ..., 9) but only complete with letters, since numbers are redundant.
      return (CS)query_type[idx];
   }
   case EXP_CSCOPE_KILL: {
      static Byte connection[5];

      // ":cscope kill" accepts connection numbers or partial names of
      // the pathname of the cscope database as argument.  Only complete
      // with connection numbers. -1 can also be used to kill all connections.
      for (i = 0, current_idx = 0; i < csinfo_size; i++) {
         if (csinfo[i].fname == NULL)
             continue;
         if (current_idx++ == idx) {
            eeSnprintf(connection, sizeof(connection), "%d", i);
            return (CS)connection;
         }
      }
      return (current_idx == idx && idx > 0) ? (CS)"-1" : NULL;
   }
   default:
      return NULL;
   }
}

//Handle command line completion for :cscope command.
void
set_context_in_cscope_cmd(Expand* xp, CS arg, CommIndex id) {
   // Default: expand subcommands
   xp->context = EXPAND_CSCOPE;
   xp->input = text(arg);
   expand_what = (id == C_scscope) ? EXP_SCSCOPE_SUBCMD : EXP_CSCOPE_SUBCMD;

   if (*arg == ZERO)
      return;

   // (part of) subcommand already typed
   CS p = skiptowhite(arg);
   if (*p == ZERO)
      return;

   // past first word
   xp->input = text(skipwhite(p));
   if (*skiptowhite(xp->input.c) != ZERO)
      xp->context = EXPAND_NOTHING;
   ei (STRNICMP(arg, "add", p - arg) == 0)
      xp->context = EXPAND_FILES;
   ei (STRNICMP(arg, "kill", p - arg) == 0)
      expand_what = EXP_CSCOPE_KILL;
   ei (STRNICMP(arg, "find", p - arg) == 0)
      expand_what = EXP_CSCOPE_FIND;
   else
      xp->context = EXPAND_NOTHING;
}

//Find the command, print help if invalid, and then call the corresponding command function.
private void
do_cscope_general(Invocation* invo, int make_split) { // whether to split window
   CScopeCommand* cmdp;
   if ((cmdp = cs_lookup_cmd(invo)) == NULL) {
      cs_help(invo);
      return;
   }

   if (make_split) {
      if (!cmdp->cansplit) {
         (void)msg_puts(_("This cscope command does not support splitting the window.\n"));
         return;
      }
      postponed_split = -1;
      postponed_split_flags = commModifierG.cmod_split;
      postponed_split_tab = commModifierG.cmod_tab;
   }

    cmdp->func(invo);

    postponed_split_flags = 0;
    postponed_split_tab = 0;
}

//Implementation of ":cscope" and ":lcscope"
void
c_cscope(Invocation* invo) {
   do_cscope_general(invo, FALSE);
}

//Implementation of ":scscope". Same as c_cscope(), but splits window, too.
void
c_scscope(Invocation* invo) {
   do_cscope_general(invo, TRUE);
}

//Implementation of ":cstag"
void
c_cstag(Invocation* invo) {
   int ret = FALSE;

   if (*invo->arg == ZERO) {
      (void)emsg(_(e_usage_cstag_ident));
      return;
   }

   switch (p_csto) {
   case 0 :
      if (cs_check_for_connections()) {
         ret = cs_find_common(S"g", invo->arg, invo->forceit, false, false, *invo->commline);
         if (ret == FALSE) {
            cs_free_tags();
            if (msgColG)
               msg_putchar('\n');
            if (cs_check_for_tags())
               ret = do_tag(invo->arg, DT_JUMP, 0, invo->forceit, FALSE);
         }
      } ei (cs_check_for_tags()) {
         ret = do_tag(invo->arg, DT_JUMP, 0, invo->forceit, FALSE);
      }
      break;
   case 1 :
      if (cs_check_for_tags()) {
         ret = do_tag(invo->arg, DT_JUMP, 0, invo->forceit, FALSE);
         if (ret == FALSE) {
            if (msgColG)
               msg_putchar('\n');

            if (cs_check_for_connections()) {
               ret = cs_find_common(S"g", (invo->arg), invo->forceit, false, false, *invo->commline);
               if (ret == FALSE)
                  cs_free_tags();
            }
         }
      } ei (cs_check_for_connections()) {
         ret = cs_find_common(S"g", (invo->arg), invo->forceit, false, false, *invo->commline);
         if (ret == FALSE)
            cs_free_tags();
      }
      break;
   default :
      break;
   }

   if (!ret) {
      (void)emsg(_(e_cstag_tag_not_founc));
      g_do_tagpreview = 0;
   }

}

//This simulates a eeFgets(), but for cscope, returns the next line
//from the cscope output. should only be called from find_tags()
//
//return TRUE if eof, FALSE otherwise
int
cs_fgets(CS buf, int size) {
   CS p;
   if ((p = cs_manage_matches(NULL, NULL, -1, Get)) == NULL)
      return TRUE;
   copySubstrToAllocation(buf, (Text){p, size - 1});

   return FALSE;
}

// Called only from do_tag(), when popping the tag stack.
void
cs_free_tags(void) {
   cs_manage_matches(NULL, NULL, -1, Free);
}

// Called from do_tag().
void
cs_print_tags(void) {
   cs_manage_matches(NULL, NULL, -1, Print);
}

//"cscope_connection([{num} , {dbpath} [, {prepend}]])" function
//
//     Checks for the existence of a |cscope| connection.  If no
//     parameters are specified, then the function returns:
//
//     0, if cscope was not available (not compiled in), or if there
//     are no cscope connections; or
//     1, if there is at least one cscope connection.
//
//     If parameters are specified, then the value of {num}
//     determines how existence of a cscope connection is checked:
//
//     {num}   Description of existence check
//     -----   ------------------------------
//     0   Same as no parameters (e.g., "cscope_connection()").
//     1   Ignore {prepend}, and use partial string matches for
//        {dbpath}.
//     2   Ignore {prepend}, and use exact string matches for
//        {dbpath}.
//     3   Use {prepend}, use partial string matches for both
//        {dbpath} and {prepend}.
//     4   Use {prepend}, use exact string matches for both
//        {dbpath} and {prepend}.
//
//     Note: All string comparisons are case sensitive!
private int
cs_connection(int num, CS dbpath, CS ppath) {
   int i;

   if (num < 0 || num > 4 || (num > 0 && !dbpath))
      return FALSE;

   for (i = 0; i < csinfo_size; i++) {
      if (!csinfo[i].fname)
         continue;

      if (num == 0)
         return TRUE;

      switch (num) {
      case 1:
         if (STRSTR(csinfo[i].fname, dbpath))
            return TRUE;
         break;
      case 2:
         if (STRCMP(csinfo[i].fname, dbpath) == 0)
            return TRUE;
         break;
      case 3:
         if (STRSTR(csinfo[i].fname, dbpath)
             && ((!ppath && !csinfo[i].ppath)
               || (ppath && csinfo[i].ppath && STRSTR(csinfo[i].ppath, ppath)))
         )
            return TRUE;
         break;
      case 4:
         if ((STRCMP(csinfo[i].fname, dbpath) == 0)
             && ((!ppath && !csinfo[i].ppath)
                  || (ppath && csinfo[i].ppath && (STRCMP(csinfo[i].ppath, ppath) == 0)))
         )
            return TRUE;
         break;
      }
   }

   return FALSE;
}

//PRIVATE functions

//Add cscope database or a directory name (to look for cscope.out)
//to the cscope connection list.
private int
cs_add(Invocation* invo UNUSED) {
   Byte *fname, *ppath, *flags = NULL;

   if ((fname = (CS)strtok((char *)NULL, (const char *)" ")) == NULL) {
      cs_usage_msg(Add);
      return CSCOPE_FAILURE;
   }
   if ((ppath = (CS)strtok((char *)NULL, (const char *)" ")) != NULL)
      flags = (CS)strtok((char *)NULL, (const char *)" ");

   return cs_add_common(fname, ppath, flags);
}

private void
cs_stat_emsg(CS fname) {
   int err = errno;
   (void)showErrFmtMsg(_(e_stat_str_error_nr), fname, err);
}

//The common routine to add a new cscope connection. Called by cs_add() and cs_reset().
private int
cs_add_common(
   CS arg1,       // filename - may contain environment variables
   CS arg2,       // prepend path - may contain environment variables
   CS flags
) {
   FileStat   statbuf;
   int      ret;
   CS fname2 = NULL;
   CS ppath = NULL;
   int      i;
   Unt   usedlen = 0;

   // get the filename (arg1), expand it, and try to stat it
   CS fname = alloc(MAXPATHL + 1);

   Unt len = doExpandEnv(OUT (Text){fname, MAXPATHL}, (CS)arg1);
   CS fbuf = (CS)fname;
   (void)modify_fname((CS)":p", FALSE, &usedlen, (Byte **)&fname, &fbuf, &len);
   if (!fname)
      goto add_err;
   fname = copySubstr((CS)fname, len);
   eeglFree(fbuf);

   ret = STAT(fname, &statbuf);
   if (ret < 0) {
staterr:
      if (p_csverbose)
         cs_stat_emsg(fname);
      goto add_err;
   }

   // get the prepend path (arg2), expand it, and try to stat it
   if (arg2) {
      FileStat statbuf2;

      ppath = alloc(MAXPATHL + 1);

      doExpandEnv(OUT (Text){ppath, MAXPATHL}, (CS)arg2);
      ret = STAT(ppath, &statbuf2);
      if (ret < 0)
         goto staterr;
   }

   // if filename is a directory, append the cscope database name to it
   if (S_ISDIR(statbuf.st_mode)) {
      fname2 = alloc(STRLEN(CSCOPE_DBFILE) + STRLEN(fname) + 2);

      while (fname[STRLEN(fname)-1] == '/') {
         fname[STRLEN(fname)-1] = '\0';
         if (fname[0] == '\0')
            break;
      }
      if (fname[0] == '\0')
         (void)SPRINTF(fname2, "/%s", CSCOPE_DBFILE);
      else
         (void)SPRINTF(fname2, "%s/%s", fname, CSCOPE_DBFILE);

      ret = STAT(fname2, &statbuf);
      if (ret < 0) {
         if (p_csverbose)
            cs_stat_emsg(fname2);
         goto add_err;
      }

      i = cs_insert_filelist(fname2, ppath, flags, &statbuf);
   } ei (S_ISREG(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)) {
      i = cs_insert_filelist(fname, ppath, flags, &statbuf);
   } else {
      if (p_csverbose)
         (void)showErrFmtMsg(_(e_str_is_not_directory_or_valid_cscope_database), fname);
      goto add_err;
   }

   if (i != -1) {
      if (cs_create_connection(i) == CSCOPE_FAILURE || cs_read_prompt(i) == CSCOPE_FAILURE) {
         cs_release_csp(i, TRUE);
         goto add_err;
      }

      if (p_csverbose) {
         msg_clr_eos();
         (void)smsgDeco(getDecoFlags(HLF_R), _("Added cscope database %s"), csinfo[i].fname);
      }
   }

   eeglFree(fname2);
   eeglFree(ppath);
   return CSCOPE_SUCCESS;

add_err:
   eeglFree(fname2);
   eeglFree(fname);
   eeglFree(ppath);
   return CSCOPE_FAILURE;
}

private int
cs_check_for_connections(void) {
   return (cs_cnt_connections() > 0);
}

private int
cs_check_for_tags(void) {
   return (curBook->o.tags != NULL);
}

// Count the number of cscope connections.
private int
cs_cnt_connections(void) {
   int i;
   int cnt = 0;

   for (i = 0; i < csinfo_size; i++) {
      if (csinfo[i].fname != NULL)
          cnt++;
   } 
   return cnt;
}

private void
cs_reading_emsg(int idx) {// connection index
   showErrFmtMsg(_(e_error_reading_cscope_connection_nr), idx);
}

#define   CSREAD_BUFSIZE   2048

//Count the number of matches for a given cscope connection.
private int
cs_cnt_matches(int idx) {
   CS stok;
   int nlines = 0;

   CS buf = alloc(CSREAD_BUFSIZE);
   for (;;) {
      if (!FGETS(buf, CSREAD_BUFSIZE, csinfo[idx].fr_fp)) {
         if (feof(csinfo[idx].fr_fp))
            errno = EIO;

         cs_reading_emsg(idx);
         eeglFree(buf);
         return -1;
      }

      //If the database is out of date, or there's some other problem,
      //cscope will output error messages before the number-of-lines output.
      //Display/discard any output that doesn't match what we want.
      //Accept "\S*cscope: X lines", also matches "mlcscope".
      //Bail out for the "Unable to search" error.
      if (STRSTR(buf, "Unable to search database") != NULL)
         break;
      if ((stok = (CS)strtok((char*)buf, (const char *)" ")) == NULL)
         continue;
      if (STRSTR(stok, "cscope:") == NULL)
         continue;

      if ((stok = (CS)strtok(NULL, (const char *)" ")) == NULL)
         continue;
      nlines = ATOI(stok);
      if (nlines < 0) {
         nlines = 0;
         break;
      }

      if ((stok = (CS)strtok(NULL, (const char *)" ")) == NULL)
         continue;
      if (STRNCMP(stok, "lines", 5))
         continue;

      break;
   }

   eeglFree(buf);
   return nlines;
}


//Create the actual cscope command query from what the user entered.
private CS
cs_create_cmd(CS csoption, CS pattern) {
   CS cmd;
   short search;
   CS pat;

   switch (csoption[0]) {
   case '0' : case 's' :
      search = 0;
      break;
   case '1' : case 'g' :
      search = 1;
      break;
   case '2' : case 'd' :
      search = 2;
      break;
   case '3' : case 'c' :
      search = 3;
      break;
   case '4' : case 't' :
      search = 4;
      break;
   case '6' : case 'e' :
      search = 6;
      break;
   case '7' : case 'f' :
      search = 7;
      break;
   case '8' : case 'i' :
      search = 8;
      break;
   case '9' : case 'a' :
      search = 9;
      break;
   default :
      (void)emsg(_(e_unknown_cscope_search_type));
      cs_usage_msg(Find);
      return NULL;
   }

   // Skip white space before the pattern, except for text and pattern search,
   // they may want to use the leading white space.
   pat = pattern;
   if (search != 4 && search != 6) {
      while SPACE_OR_TAB(*pat)
         ++pat;
   } 

   cmd = alloc(STRLEN(pat) + 2);

   (void)SPRINTF(cmd, "%d%s", search, pat);

   return cmd;
}


//This piece of code was taken/adapted from nvi.  do we need to add the BSD license notice?
private int
cs_create_connection(int i) {
   int      to_cs[2], from_cs[2];
   int      cmdlen;
   int      len;
   Byte   *cmd, *ppath = NULL;
   Unt   proglen;

   //Cscope reads from to_cs[0] and writes to from_cs[1]; vi reads from
   //from_cs[0] and writes to to_cs[1].
   to_cs[0] = to_cs[1] = from_cs[0] = from_cs[1] = -1;
   if (pipe(to_cs) < 0 || pipe(from_cs) < 0) {
      (void)emsg(_(e_could_not_create_cscope_pipes));
   err_closing:
      if (to_cs[0] != -1)
         (void)close(to_cs[0]);
      if (to_cs[1] != -1)
         (void)close(to_cs[1]);
      if (from_cs[0] != -1)
         (void)close(from_cs[0]);
      if (from_cs[1] != -1)
         (void)close(from_cs[1]);
      return CSCOPE_FAILURE;
   }

   CS prog;
   if ((csinfo[i].pid = fork()) == -1) {
      (void)emsg(_(e_could_not_fork_for_cscope));
      goto err_closing;
   } ei (csinfo[i].pid == 0) {  // child: run cscope.
      CS* argv = NULL;
      int argc = 0;

      if (dup2(to_cs[0], STDIN_FILENO) == -1)
         PERROR("cs_create_connection 1");
      if (dup2(from_cs[1], STDOUT_FILENO) == -1)
         PERROR("cs_create_connection 2");
      if (dup2(from_cs[1], STDERR_FILENO) == -1)
         PERROR("cs_create_connection 3");

      // close unused
      (void)close(to_cs[1]);
      (void)close(from_cs[0]);
      // expand the cscope exec for env var's
      prog = alloc(MAXPATHL + 1);
      proglen = doExpandEnv(OUT (Text){prog, MAXPATHL}, p_csprg);

      // alloc space to hold the cscope command
      cmdlen = (int)(proglen + STRLEN(csinfo[i].fname) + 32);
      if (csinfo[i].ppath) {
         // expand the prepend path for env var's
         ppath = alloc(MAXPATHL + 1);
         cmdlen += (int)doExpandEnv(OUT (Text){ppath, MAXPATHL}, (CS)csinfo[i].ppath);
      }

      if (csinfo[i].flags)
         cmdlen += (int)STRLEN(csinfo[i].flags);

      cmd = alloc(cmdlen);

      // run the cscope command
      eeSnprintf(cmd, cmdlen, "/bin/sh -c \"exec %s -dl -f %s", prog, csinfo[i].fname);
      if (csinfo[i].ppath != NULL) {
         len = (int)STRLEN(cmd);
         eeSnprintf(cmd + len, cmdlen - len, " -P%s", csinfo[i].ppath);
      }
      if (csinfo[i].flags != NULL) {
         len = (int)STRLEN(cmd);
         eeSnprintf(cmd + len, cmdlen - len, " %s", csinfo[i].flags);
      }
      // terminate the -c command argument
      STRCAT(cmd, "\"");

      eeglFree(prog);
      eeglFree(ppath);

      // Change our process group to avoid cscope receiving SIGWINCH.
      (void)setsid();
      build_argv_from_string(cmd, OUT &argv, &argc);

      if (execvp((char*)argv[0], (char**)argv) == -1)
         PERROR(_("cs_create_connection exec failed"));

      exit(127);
      // NOTREACHED
   } else {  // parent.
      //Save the file descriptors for later duplication, and reopen as streams.
      if ((csinfo[i].to_fp = fdopen(to_cs[1], "w")) == NULL)
         PERROR(_("cs_create_connection: fdopen for to_fp failed"));
      if ((csinfo[i].fr_fp = fdopen(from_cs[0], "r")) == NULL)
         PERROR(_("cs_create_connection: fdopen for fr_fp failed"));

      // close unused
      (void)close(to_cs[0]);
      (void)close(from_cs[1]);
   }

   return CSCOPE_SUCCESS;
}


//Query cscope using command line interface. Parse the output and use tselect
//to allow choices. Create a pipe to send to/from query/cscope.
//
//return TRUE if we jump to a tag or abort, FALSE if not.
private int
cs_find(Invocation* invo) {
   if (cs_check_for_connections() == FALSE) {
      (void)emsg(_(e_no_cscope_connections));
      return FALSE;
   }

   CS opt;
   if ((opt = (CS)strtok((char *)NULL, (const char *)" ")) == NULL) {
      cs_usage_msg(Find);
      return FALSE;
   }

   CS pat = opt + STRLEN(opt) + 1;
   if (pat >= invo->arg + eap_arg_len) {
      cs_usage_msg(Find);
      return FALSE;
   }

   //Let's replace the ZEROs written by strtok() with spaces - we need the
   //spaces to correctly display the quickfix/location list window's title.
   for (int i = 0; i < eap_arg_len; ++i) {
      if (ZERO == invo->arg[i])
         invo->arg[i] = ' ';
   }

   return cs_find_common(
          opt, pat, invo->forceit, true, invo->id == C_lcscope, *invo->commline
   );
}

// Common code for cscope find, shared by cs_find() and ex_cstag().
private int
cs_find_common(
   CS opt,
   CS pat,
   Boole forceit,
   Boole verbose,
   Boole   use_ll,
   CS commline
) {
   int i;

   if (apply_autocmds(EVENT_QUICKFIXCMDPRE, S"cscope", curBook->currFileName, TRUE, curBook)
      && aborting()
   ){
      return FALSE;
   }

   // create the actual command to send to cscope
   CS cmd = cs_create_cmd(opt, pat);
   if (!cmd)
      return FALSE;

   int* nummatches = ALLOC_MULT(int, csinfo_size);

   // Send query to all open connections, then count the total number
   // of matches so we can alloc all in one swell foop.
   for (i = 0; i < csinfo_size; i++)
      nummatches[i] = 0;
   int totmatches = 0;
   for (i = 0; i < csinfo_size; i++) {
      if (csinfo[i].fname == NULL || csinfo[i].to_fp == NULL)
         continue;

      // send cmd to cscope
      (void)fprintf(csinfo[i].to_fp, "%s\n", cmd);
      (void)fflush(csinfo[i].to_fp);

      nummatches[i] = cs_cnt_matches(i);

      if (nummatches[i] > -1)
         totmatches += nummatches[i];

      if (nummatches[i] == 0)
         (void)cs_read_prompt(i);
   }
   eeglFree(cmd);

   if (totmatches == 0) {
      if (verbose)
         (void)showErrFmtMsg(_(e_no_matches_found_for_cscope_query_str_of_str), opt, pat);
      eeglFree(nummatches);
      return FALSE;
   }

   if (totmatches > 0) {
      // fill error list
      FILE* f;
      Byte* tmp = eeTempName('c', TRUE);
      LocationStack* llStack = NULL;
      Portal* wp = NULL;

      f = FOPEN(tmp, "w");
      if (f == NULL)
         showErrFmtMsg(_(e_cant_open_file_str), tmp);
      else {
         cs_file_results(f, nummatches);
         fclose(f);
         if (use_ll)     // Use location list
            wp = curPor;
         // '-' starts a new error list
         if (llInitFromFile(
                getLocationStack(LOC_LIST_CSCOPE), tmp, (CS)"%f%*\\t%l%*\\t%m", 
                FALSE, commline
             ) > 0
         ) {
            if (postponed_split != 0) {
               (void)splitPortal(postponed_split > 0 ? postponed_split : 0, postponed_split_flags);
               RESET_BINDING(curPor);
               postponed_split = 0;
            }

            apply_autocmds(
               EVENT_QUICKFIXCMDPOST, (CS)"cscope", curBook->currFileName, TRUE, curBook
            );
            if (use_ll) {
               // In the location list portal, use the displayed location list. Otherwise, use the
               // global "cscope" location list.
               llStack = (isLocationListBook(wp->book) && wp->locationStackRef != NULL)
                     ?  wp->locationStackRef : getLocationStack(LOC_LIST_CSCOPE);
            }
            llJump(llStack, 0, 0, forceit);
         }
      }
      mch_remove(tmp);
      eeglFree(tmp);
      eeglFree(nummatches);
      return TRUE;
   } else {
      Arr(CS) matches = NULL;
      Arr(CS) contexts = NULL;
      int matched = 0;

      // read output
      cs_fill_results(pat, totmatches, nummatches, &matches, &contexts, &matched);
      eeglFree(nummatches);
      if (!matches)
         return FALSE;

      (void)cs_manage_matches(matches, contexts, matched, Store);

      return do_tag(pat, DT_CSCOPE, 0, forceit, verbose);
   }
}

//Print help.
private int
cs_help(Invocation* invo UNUSED) {
   CScopeCommand *cmdp = cs_cmds;

   (void)msg_puts(_("cscope commands:\n"));
   while (cmdp->name) {
      CS help = _(cmdp->help);
      int  space_cnt = 30 - eeglStrSize((CS)help);

      // Use %*s rather than %30s to ensure proper alignment in utf-8
      if (space_cnt < 0)
         space_cnt = 0;
      (void)smsg(_("%-5s: %s%*s (Usage: %s)"),
                     cmdp->name,
                     help, space_cnt, " ",
                     cmdp->usage);
      if (STRCMP(cmdp->name, "find") == 0)
          msg_puts(_("\n"
                "       a: Find assignments to this symbol\n"
                "       c: Find functions calling this function\n"
                "       d: Find functions called by this function\n"
                "       e: Find this egrep pattern\n"
                "       f: Find this file\n"
                "       g: Find this definition\n"
                "       i: Find files #including this file\n"
                "       s: Find this C symbol\n"
                "       t: Find this text string\n"));

      cmdp++;
   }

    wait_return(TRUE);
    return 0;
}

private void
clear_csinfo(int i) {
   csinfo[i].fname  = NULL;
   csinfo[i].ppath  = NULL;
   csinfo[i].flags  = NULL;
   csinfo[i].st_dev = (dev_t)0;
   csinfo[i].st_ino = (ino_t)0;
   csinfo[i].pid    = 0;
   csinfo[i].fr_fp  = NULL;
   csinfo[i].to_fp  = NULL;
}

//Insert a new cscope database filename into the filelist.
private int
cs_insert_filelist(CS fname, CS ppath, CS flags, FileStat *sb UNUSED) {
   int       j;

   int i = -1; // can be set to the index of an empty item in csinfo
   for (j = 0; j < csinfo_size; j++) {
      if (csinfo[j].fname != NULL
          && csinfo[j].st_dev == sb->st_dev && csinfo[j].st_ino == sb->st_ino
      ) {
         if (p_csverbose)
            (void)emsg(_(e_duplicate_cscope_database_not_added));
         return -1;
      }

      if (csinfo[j].fname == NULL && i == -1)
          i = j; // remember first empty entry
   }

   if (i == -1) {
      i = csinfo_size;
      if (csinfo_size == 0) {
          // First time allocation: allocate only 1 connection. It should
          // be enough for most users.  If more is needed, csinfo will be reallocated.
          csinfo_size = 1;
          csinfo = ALLOC_CLEAR_ONE(CscopeInfo);
      } else {
         // Reallocate space for more connections.
         csinfo_size *= 2;
         csinfo = eeRealloc(csinfo, sizeof(CscopeInfo)*csinfo_size);
      }
      if (csinfo == NULL)
         return -1;
      for (j = csinfo_size/2; j < csinfo_size; j++)
         clear_csinfo(j);
   }

   csinfo[i].fname = alloc(STRLEN(fname) + 1);

   (void)STRCPY(csinfo[i].fname, fname);

   if (ppath) {
      csinfo[i].ppath = alloc(STRLEN(ppath) + 1);
      (void)STRCPY(csinfo[i].ppath, ppath);
   } else
      csinfo[i].ppath = NULL;

   if (flags) {
      csinfo[i].flags = alloc(STRLEN(flags) + 1);
      (void)STRCPY(csinfo[i].flags, flags);
   } else
      csinfo[i].flags = NULL;

   csinfo[i].st_dev = sb->st_dev;
   csinfo[i].st_ino = sb->st_ino;
   return i;
}

//Find cscope command in command table.
private CScopeCommand *
cs_lookup_cmd(Invocation* invo) {
   CScopeCommand* cmdp;
   CS stok;

   if (invo->arg == NULL)
      return NULL;

   // Store length of invo->arg before it gets modified by strtok().
   eap_arg_len = (int)STRLEN(invo->arg);

   if ((stok = (CS)strtok((char *)(invo->arg), (const char *)" ")) == NULL)
      return NULL;

   Unt len = STRLEN(stok);
   for (cmdp = cs_cmds; cmdp->name != NULL; ++cmdp) {
      if (STRNCMP((stok), cmdp->name, len) == 0)
          return (cmdp);
   }
   return NULL;
}

// Nuke em.
private int
cs_kill(Invocation* invo UNUSED) {
   CS stok;
   int i;

   if ((stok = (CS)strtok((char *)NULL, (const char *)" ")) == NULL) {
      cs_usage_msg(Kill);
      return CSCOPE_FAILURE;
   }

   // only single digit positive and negative integers are allowed
   if ((STRLEN(stok) < 2 && EE_ISDIGIT((int)(stok[0])))
       || (STRLEN(stok) < 3 && stok[0] == '-' && EE_ISDIGIT((int)(stok[1])))
   )
      i = ATOI(stok);
   else {
      // It must be part of a name.  We will try to find a match
      // within all the names in the csinfo data structure
      for (i = 0; i < csinfo_size; i++) {
         if (csinfo[i].fname != NULL && STRSTR(csinfo[i].fname, stok))
            break;
      }
   }

   if ((i != -1) && (i >= csinfo_size || i < -1 || csinfo[i].fname == NULL)) {
      if (p_csverbose)
         (void)showErrFmtMsg(_(e_cscope_connection_str_not_founc), stok);
   } else {
      if (i == -1) {
         for (i = 0; i < csinfo_size; i++) {
            if (csinfo[i].fname)
               cs_kill_execute(i, csinfo[i].fname);
         }
      } else
         cs_kill_execute(i, stok);
   }

   return 0;
}

// Actually kills a specific cscope connection.
private void
cs_kill_execute(int i,                 CS cname) { 
                // cscope table index  // cscope database name
   if (p_csverbose) {
      msg_clr_eos();
      (void)smsgDeco(getDecoFlags(HLF_R) | MSG_HIST, _("cscope connection %s closed"), cname);
   }
   cs_release_csp(i, TRUE);
}

//Convert the cscope output into a ctags style entry (as might be found
//in a ctags tags file).  there's one catch though: cscope doesn't tell you
//the type of the tag you are looking for.  for example, in Darren Hiebert's
//ctags (the one that comes with vim), #define's use a line number to find the
//tag in a file while function definitions use a regexp search pattern.
//
//I'm going to always use the line number because cscope does something
//quirky (and probably other things i don't know about):
//
//    if you have "#  define" in your source file, which is
//    perfectly legal, cscope thinks you have "#define".  this
//    will result in a failed regexp search. :(
//
//Besides, even if this particular case didn't happen, the search pattern
//would still have to be modified to escape all the special regular expression
//characters to comply with ctags formatting.
private CS
cs_make_eegl_style_matches(CS fname, CS slno, CS search, CS tagstr) {
   // Eegl style is ctags:
   //
   //       <tagstr>\t<filename>\t<linenum_or_search>"\t<extra>
   //
   // but as mentioned above, we'll always use the line number and
   // put the search pattern (if one exists) as "extra"
   //
   // buf is used as part of vim's method of handling tags, and
   // (i think) Eegl frees it when you pop your tags and get replaced
   // by new ones on the tag stack.
   CS buf;
   int amt;

   if (search) {
      amt = (int)(STRLEN(fname) + STRLEN(slno) + STRLEN(tagstr) + STRLEN(search)+6);
      buf = alloc(amt);

      (void)SPRINTF(buf, "%s\t%s\t%s;\"\t%s", tagstr, fname, slno, search);
   } else {
      amt = (int)(STRLEN(fname) + STRLEN(slno) + STRLEN(tagstr) + 5);
      buf = alloc(amt);

      (void)SPRINTF(buf, "%s\t%s\t%s;\"", tagstr, fname, slno);
   }

   return buf;
}


//This is kind of hokey, but i don't see an easy way round this.
//
//Store: keep a ptr to the (malloc'd) memory of matches originally
//generated from cs_find().  the matches are originally lines directly
//from cscope output, but transformed to look like something out of a
//ctags.  see cs_make_eegl_style_matches() for more details.
//
//Get: used only from cs_fgets(), this simulates a eeFgets() to return
//the next line from the cscope output.  it basically keeps track of which
//lines have been "used" and returns the next one.
//
//Free: frees up everything and resets
//
//Print: prints the tags
private CS
cs_manage_matches(Arr(CS) matches, Arr(CS) contexts, int totmatches, Mcmd cmd) {
   static Arr(CS) mp = NULL;
   static Arr(CS) cp = NULL;
   static int cnt = -1;
   static int next = -1;
   CS p = NULL;

   switch (cmd) {
   case Store:
      assert(matches != NULL);
      assert(totmatches > 0);
      if (mp || cp)
          (void)cs_manage_matches(NULL, NULL, -1, Free);
      mp = matches;
      cp = contexts;
      cnt = totmatches;
      next = 0;
      break;
   case Get:
      if (next >= cnt)
          return NULL;

      p = mp[next];
      next++;
      break;
   case Free:
      if (mp) {
          if (cnt > 0)
         while (cnt--) {
             eeglFree(mp[cnt]);
             if (cp != NULL)
            eeglFree(cp[cnt]);
         }
          eeglFree(mp);
          eeglFree(cp);
      }
      mp = NULL;
      cp = NULL;
      cnt = 0;
      next = 0;
      break;
   case Print:
      cs_print_tags_priv(mp, cp, cnt);
      break;
   default:   // should not reach here
      internalErrMsg(e_fatal_error_in_cs_manage_matches);
      return NULL;
   }

   return p;
}

//Parse cscope output.
private CS
cs_parse_results(
   int cnumber,
   CS buf,
   int bufsize,
   Byte **context,
   Byte **linenumber,
   Byte **search
) {
   int ch;
   CS p;
   CS name;

   if (FGETS(buf, bufsize, csinfo[cnumber].fr_fp) == NULL) {
      if (feof(csinfo[cnumber].fr_fp))
          errno = EIO;

      cs_reading_emsg(cnumber);

      return NULL;
   }

   // If the line's too long for the buffer, discard it.
   if ((p = STRCHR(buf, '\n')) == NULL) {
      while ((ch = getc(csinfo[cnumber].fr_fp)) != EOF && ch != '\n')
          ;
      return NULL;
   }
   *p = '\0';

   //cscope output is in the following format:
   //
   //  <filename> <context> <line number> <pattern>
   if ((name = (CS)strtok((char*)buf, (const char *)" ")) == NULL)
      return NULL;
   if ((*context = (CS)strtok(NULL, (const char *)" ")) == NULL)
      return NULL;
   if ((*linenumber = (CS)strtok(NULL, (const char *)" ")) == NULL)
      return NULL;
   *search = *linenumber + STRLEN(*linenumber) + 1;   // +1 to skip \0

   // --- nvi ---
   // If the file is older than the cscope database, that is,
   // the database was built since the file was last modified,
   // or there wasn't a search string, use the line number.
   if (STRCMP(*search, "<unknown>") == 0)
      *search = NULL;

   name = cs_resolve_file(cnumber, name);
   return name;
}

//Write cscope find results to file.
private void
cs_file_results(FILE* f, int* nummatches_a) {
   int i, j;
   CS search;
   CS slno;
   CS fullname;
   CS cntx;

   CS buf = alloc(CSREAD_BUFSIZE);

   for (i = 0; i < csinfo_size; i++) {
      if (nummatches_a[i] < 1)
          continue;

      for (j = 0; j < nummatches_a[i]; j++) {
         if ((fullname = cs_parse_results(i, buf, CSREAD_BUFSIZE, &cntx, &slno, &search)) == NULL)
            continue;

         CS context = alloc(STRLEN(cntx)+5);

         if (STRCMP(cntx, "<global>")==0)
            STRCPY(context, "<<global>>");
         else
            SPRINTF(context, "<<%s>>", cntx);

         if (search == NULL)
             fprintf(f, "%s\t%s\t%s\n", fullname, slno, context);
         else
             fprintf(f, "%s\t%s\t%s %s\n", fullname, slno, context, search);

         eeglFree(context);
         eeglFree(fullname);
      } // for all matches

      (void)cs_read_prompt(i);

   } // for all cscope connections
   eeglFree(buf);
}

//Get parsed cscope output and calls cs_make_eegl_style_matches() to convert into ctags format.
//When there are no matches, set "*matches_p" to NULL.
private void
cs_fill_results(
   CS tagstr,
   int totmatches,
   int *nummatches_a,
   Byte ***matches_p,
   Byte ***cntxts_p,
   int *matched
) {
   int j;
   Byte *search, *slno;
   int totsofar = 0;
   Byte *fullname;
   Byte *cntx;

   assert(totmatches > 0);

   CS buf = alloc(CSREAD_BUFSIZE);

   Arr(CS) matches = ALLOC_MULT(Byte *, totmatches);
   Arr(CS) cntxts = ALLOC_MULT(Byte *, totmatches);

   for (int i = 0; i < csinfo_size; i++) {
      if (nummatches_a[i] < 1)
          continue;

      for (j = 0; j < nummatches_a[i]; j++) {
         if ((fullname = cs_parse_results(i, buf, CSREAD_BUFSIZE, &cntx,
               &slno, &search)) == NULL
         )
            continue;

         matches[totsofar] = cs_make_eegl_style_matches(fullname, slno, search, tagstr);

         eeglFree(fullname);

         if (STRCMP(cntx, "<global>") == 0)
            cntxts[totsofar] = NULL;
         else
            // note: if copyStr returns NULL, then the context
            // will be "<global>", which is misleading.
            cntxts[totsofar] = copyStr((CS)cntx);

         if (matches[totsofar] != NULL)
            totsofar++;
      } // for all matches

      (void)cs_read_prompt(i);

   } // for all cscope connections

   if (totsofar == 0) {
      // No matches, free the arrays and return NULL in "*matches_p".
      EE_CLEAR(matches);
      EE_CLEAR(cntxts);
   }
   *matched = totsofar;
   *matches_p = matches;
   *cntxts_p = cntxts;

   eeglFree(buf);
}


//get the requested path components
private CS
cs_pathcomponents(CS path) {
   if (p_cspc == 0)
      return path;

   CS s = path + STRLEN(path) - 1;
   for (int i = 0; i < p_cspc; ++i) {
      while (s > path && *--s != '/')
         {}
   } 
   if ((s > path && *s == '/'))
      ++s;
   return s;
}

//Called from cs_manage_matches().
private void
cs_print_tags_priv(Arr(CS) matches, Arr(CS) cntxts, int num_matches) {
   int bufsize = 0; // Track available bufsize
   int newsize = 0;
   CS fname, lno, extra;
   int i, idx, num;
   CS globalcntx = S"GLOBAL";
   CS cntxformat = S" <<%s>>";
   CS context;
   CS cstag_msg = _("Cscope tag: %s");
   CS csfmt_str = S"%4d %6s  ";

   assert(num_matches > 0);

   CS matchesbuf = alloc(STRLEN(matches[0]) + 1);

   STRCPY(matchesbuf, matches[0]);
   CS ptag = (CS)strtok((char*)matchesbuf, "\t");
   if (ptag == NULL) {
      eeglFree(matchesbuf);
      return;
   }

   newsize = (int)(STRLEN(cstag_msg) + STRLEN(ptag));
   CS buf = alloc(newsize);
   bufsize = newsize;
   (void)SPRINTF(buf, cstag_msg, ptag);
   msgPutsDeco(buf, getDecoFlags(HLF_T));

   eeglFree(matchesbuf);

   msgPutsDeco(_("\n   #   line"), getDecoFlags(HLF_T));    // STRLEN is 7
   msg_advance(msgColG + 2);
   msgPutsDeco(_("filename / context / line\n"), getDecoFlags(HLF_T));

   num = 1;
   for (i = 0; i < num_matches; i++) {
      idx = i;

      //if we really wanted to, we could avoid this malloc and STRCPY by parsing matches[i] on 
      //the fly and placing stuff into the string buffer directly, but that's too much of a hassle
      matchesbuf = alloc(STRLEN(matches[idx]) + 1);
      (void)STRCPY(matchesbuf, matches[idx]);

      if (strtok((char*)matchesbuf, (const char *)"\t") == NULL
         || (fname = (CS)strtok(NULL, (const char *)"\t")) == NULL
         || (lno = (CS)strtok(NULL, (const char *)"\t")) == NULL)
      {
          eeglFree(matchesbuf);
          continue;
      }
      extra = (CS)strtok(NULL, (const char *)"\t");

      lno[STRLEN(lno)-2] = '\0';  // ignore ;" at the end

      // hopefully 'num' (num of matches) will be less than 10^16
      newsize = (int)(STRLEN(csfmt_str) + 16 + STRLEN(lno));
      if (bufsize < newsize) {
         buf = eeRealloc(buf, newsize);
         bufsize = newsize;
      }
      if (buf) {
         // csfmt_str = "%4d %6s  ";
         (void)SPRINTF(buf, csfmt_str, num, lno);
         msgPutsDeco(buf, getDecoFlags(HLF_CM));
      }
      outputShortenedToALine(mbText((CS)cs_pathcomponents(fname)), getDecoFlags(HLF_CM));

      // compute the required space for the context
      if (cntxts[idx] != NULL)
         context = cntxts[idx];
      else
         context = globalcntx;
      newsize = (int)(STRLEN(context) + STRLEN(cntxformat));

      if (bufsize < newsize) {
         buf = eeRealloc(buf, newsize);
         bufsize = newsize;
      }
      if (buf) {
         (void)SPRINTF(buf, cntxformat, context);

         // print the context only if it fits on the same line
         if (msgColG + (int)STRLEN(buf) >= (int)visibleColsG)
            msg_putchar('\n');
         msg_advance(12);
         outputShortenedToALine(text(buf), 0);
         msg_putchar('\n');
      }
      if (extra) {
          msg_advance(13);
          outputShortenedToALine(text((CS)extra), 0);
      }

      eeglFree(matchesbuf); // only after printing extra due to strtok use

      if (msgColG)
          msg_putchar('\n');

      ui_breakcheck();
      if (gotInterruptG) {
         gotInterruptG = FALSE;   // don't print any more matches
         break;
      }

      num++;
   } // for all matches

   eeglFree(buf);
}


//Read a cscope prompt (basically, skip over the ">> ").
private int
cs_read_prompt(int i) {
   int      ch;
   CS buf = NULL; // buf for possible error message from cscope
   int      bufpos = 0;
   static CS eprompt = (CS)"Press the RETURN key to continue:";
   int epromptlen = (int)STRLEN(eprompt);
   int      n;

   // compute maximum allowed len for Cscope error message
   CS cs_emsg = _(e_cscope_error_str);
   int maxlen = (int)(IOSIZE - STRLEN(cs_emsg));

   for (;;) {
      while ((ch = getc(csinfo[i].fr_fp)) != EOF && ch != CSCOPE_PROMPT[0]) {
         // if there is room and char is printable
         if (bufpos < maxlen - 1 && eeIsPrintable(ch)) {
            if (!buf) // lazy buffer allocation
               buf = alloc(maxlen);
            // append character to the message
            buf[bufpos++] = ch;
            buf[bufpos] = ZERO;
            if (bufpos >= epromptlen && STRCMP(&buf[bufpos - epromptlen], eprompt) == 0) {
               // remove eprompt from buf
               buf[bufpos - epromptlen] = ZERO;

               // print message to user
               (void)showErrFmtMsg(cs_emsg, buf);

               // send RETURN to cscope
               (void)putc('\n', csinfo[i].to_fp);
               (void)fflush(csinfo[i].to_fp);

               // clear buf
               bufpos = 0;
               buf[bufpos] = ZERO;
            }
         }
      } 

      for (n = 0; n < (int)STRLEN(CSCOPE_PROMPT); ++n) {
         if (n > 0)
            ch = getc(csinfo[i].fr_fp);
         if (ch == EOF) {
            if (buf && buf[0] != ZERO)
               (void)showErrFmtMsg(cs_emsg, buf);
            ei (p_csverbose)
               cs_reading_emsg(i); // don't have additional information
            cs_release_csp(i, TRUE);
            eeglFree(buf);
            return CSCOPE_FAILURE;
         }

         if (ch != CSCOPE_PROMPT[n]) {
            ch = EOF;
            break;
         }
      }

      if (ch == EOF)
         continue;       // didn't find the prompt
      break;          // did find the prompt
    }

    eeglFree(buf);
    return CSCOPE_SUCCESS;
}

#if defined(SIGALRM)
//Used to catch and ignore SIGALRM below.
private void
sig_handler SIGDEFARG(sigarg) {
   // do nothing
}
#endif

//Do the actual free'ing for the cs ptr with an optional flag of whether to free the filename.
//Called by cs_kill and cs_reset.
private void
cs_release_csp(int i, int freefnpp) {
   //Trying to exit normally (not sure whether it is fit to Unix cscope
   if (csinfo[i].to_fp != NULL) {
      (void)fputs("q\n", csinfo[i].to_fp);
      (void)fflush(csinfo[i].to_fp);
   }
   {
   int waitpid_errno;
   int pstat;
   pid_t pid;

   struct sigaction sa, old;

   // Use sigaction() to limit the waiting time to two seconds.
   sigemptyset(&sa.sa_mask);
   sa.sa_handler = sig_handler;
#  ifdef SA_NODEFER
   sa.sa_flags = SA_NODEFER;
#  else
   sa.sa_flags = 0;
#  endif
   sigaction(SIGALRM, &sa, &old);
   alarm(2); // 2 sec timeout

   // Block until cscope exits or until timer expires
   pid = waitpid(csinfo[i].pid, &pstat, 0);
   waitpid_errno = errno;

   // cancel pending alarm if still there and restore signal
   alarm(0);
   sigaction(SIGALRM, &old, NULL);
   //If the cscope process is still running: kill it.
   //Safety check: If the PID would be zero here, the entire X session
   //would be killed.  -1 and 1 are dangerous as well.
   if (pid < 0 && csinfo[i].pid > 1) {
# ifdef ECHILD
      int alive = TRUE;

      if (waitpid_errno == ECHILD) {
         //When using 'vim -g', vim is forked and cscope process is
         //no longer a child process but a sibling.  So waitpid()
         //fails with errno being ECHILD (No child processes).
         //Don't send SIGKILL to cscope immediately but wait
         //(polling) for it to exit normally as result of sending
         //the "q" command, hence giving it a chance to clean up its temporary files.
         int waited;

         sleep(0);
         for (waited = 0; waited < 40; ++waited) {
            // Check whether cscope process is still alive
            if (kill(csinfo[i].pid, 0) != 0) {
               alive = FALSE; // cscope process no longer exists
               break;
            }
            mch_delay(50L, 0); // sleep 50 ms
         }
      }
      if (alive)
# endif
       {
      kill(csinfo[i].pid, SIGKILL);
      (void)waitpid(csinfo[i].pid, &pstat, 0);
       }
   }
   }

   if (csinfo[i].fr_fp)
      (void)fclose(csinfo[i].fr_fp);
   if (csinfo[i].to_fp)
      (void)fclose(csinfo[i].to_fp);

   if (freefnpp) {
      eeglFree(csinfo[i].fname);
      eeglFree(csinfo[i].ppath);
      eeglFree(csinfo[i].flags);
   }

   clear_csinfo(i);
}


//Call cs_kill on all cscope connections then reinits.
private int
cs_reset(Invocation* invo UNUSED) {
   Byte buf[20]; // for SPRINTF " (#%d)"

   if (csinfo_size == 0)
      return CSCOPE_SUCCESS;

   // malloc our db and ppath list
   CS* dblist = ALLOC_MULT(CS, csinfo_size);
   CS* pplist = ALLOC_MULT(CS, csinfo_size);
   CS* fllist = ALLOC_MULT(CS, csinfo_size);

   for (int i = 0; i < csinfo_size; i++) {
      dblist[i] = csinfo[i].fname;
      pplist[i] = csinfo[i].ppath;
      fllist[i] = csinfo[i].flags;
      if (csinfo[i].fname != NULL)
         cs_release_csp(i, FALSE);
   }

   // rebuild the cscope connection list
   for (int i = 0; i < csinfo_size; i++) {
      if (dblist[i] != NULL) {
         cs_add_common(dblist[i], pplist[i], fllist[i]);
         if (p_csverbose) {
            // don't use smsgDeco() because we want to display the
            // connection number in the same line as "Added cscope database..."
            SPRINTF(buf, " (#%d)", i);
            msgPutsDeco(buf, getDecoFlags(HLF_R));
         }
      }
      eeglFree(dblist[i]);
      eeglFree(pplist[i]);
      eeglFree(fllist[i]);
   }
   eeglFree(dblist);
   eeglFree(pplist);
   eeglFree(fllist);

   if (p_csverbose)
      msgDeco(_("All cscope databases reset"), getDecoFlags(HLF_R) | MSG_HIST);
   return CSCOPE_SUCCESS;
}


//Construct the full pathname to a file found in the cscope database. Prepend ppath, if there
//is one and if it's not already prepended, otherwise just use the name found.
//
//We need to prepend the prefix because on some cscope's, the output never has the prefix 
//prepended. Contrast this with my development system (Digital Unix), which does.
private CS
cs_resolve_file(int i, CS name) {
   //Ppath is freed when we destroy the cscope connection. Fullname is freed after 
   //cs_make_eegl_style_matches, after it's been copied into the tag buffer used by Eegl.
   Unt len = (int)(STRLEN(name) + 2);
   Byte csdir[MAXPATHL];
   if (csinfo[i].ppath)
      len += STRLEN(csinfo[i].ppath);
   ei (p_csre && csinfo[i].fname != NULL) {
      // If 'cscoperelative' is set and ppath is not set, use cscope.out path in path resolution.
      copySubstrToAllocation(
            csdir, (Text){csinfo[i].fname, gettail(csinfo[i].fname) - csinfo[i].fname}
      );
      len += STRLEN(csdir);
   }

   //Note/example: this won't work if the cscope output already starts
   //"../.." and the prefix path is also "../..".  if something like this
   //happens, you are screwed up and need to fix how you're using cscope.
   CS fullname;
   if (csinfo[i].ppath
       && (STRNCMP(name, csinfo[i].ppath, STRLEN(csinfo[i].ppath)) != 0)
       && (name[0] != '/')
     
   ) {
      fullname = alloc(len);
      (void)SPRINTF(fullname, "%s/%s", csinfo[i].ppath, name);
   } ei (csinfo[i].fname && *csdir != ZERO) {
      // Check for csdir to be non empty to avoid empty path concatenated to cscope output.
      fullname = concat_fnames(csdir, name, TRUE);
   } else {
      fullname = copyStr(name);
   }

   return fullname;
}


// Show all cscope connections.
private int
cs_show(Invocation* invo UNUSED) {
   if (cs_cnt_connections() == 0)
      msg_puts(_("no cscope connections\n"));
   else {
      msgPutsDeco(
         _(" # pid    database name                       prepend path\n"),
         getDecoFlags(HLF_T));
      for (int i = 0; i < csinfo_size; i++) {
         if (!csinfo[i].fname)
            continue;

         if (csinfo[i].ppath)
            (void)smsg(
               "%2d %-5ld  %-34s  %-32s", i, (long)csinfo[i].pid, csinfo[i].fname, csinfo[i].ppath
            );
         else
            (void)smsg("%2d %-5ld  %-34s  <none>", i, (long)csinfo[i].pid, csinfo[i].fname);
      }
   }

   wait_return(FALSE);
   return CSCOPE_SUCCESS;
}


//Only called when Eegl exits to quit any cscope sessions.
void
cs_end(void) {
   for (int i = 0; i < csinfo_size; i++)
      cs_release_csp(i, TRUE);
   eeglFree(csinfo);
   csinfo_size = 0;
}

//"cscope_connection([{num} , {dbpath} [, {prepend}]])" function
//Check the existence of a cscope connection.
void
f_cscope_connection(Var *argvars UNUSED, Var *returnVar UNUSED) {
   int      num = 0;
   CS dbpath = NULL;
   CS prepend = NULL;
   Byte buf[NUMBUFLEN];

   if (argvars[0].tag != VAR_UNKNOWN && argvars[1].tag != VAR_UNKNOWN) {
      num = (int)tv_get_number(&argvars[0]);
      dbpath = tv_get_string(&argvars[1]);
      if (argvars[2].tag != VAR_UNKNOWN)
         prepend = tv_get_string_buf(&argvars[2], buf);
   }

   returnVar->number = cs_connection(num, dbpath, prepend);
}

//}}}
