//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## option.c: code controlling user options 

#define IN_OPTION_C
#include "eegl.h"

//{{{info & types

//Code to handle user-settable options. Checklist for adding a new option:
//- Put it in one of the arrays in defoption.h (depending on if it's global or local).
//- If it's a numeric option, add a setter callback and perform necessary bounds checks there
//- If it's a list of flags, add some code in setStringImpl(), search for WW_ALL.
//- Add documentation!  One line in manual/manual.help, full description in
//  noncode/help/reference.help, and any other related places.
//When making changes:
//- Adjust the help for the option in /usr/share/eegl/doc/reference.help.

//{{{enums


// Option Flags

#define P_GLOBAL        0x01 //Global option
#define P_PORTAL        0x02 //Portal-local option
#define P_BOOK          0x04 //Book-local option
                          
#define P_EXPAND_DIR    0x10 //environment expansion for directories
#define P_EXPAND_3_BS   0x20 //need three backslashes for a space
#define P_NO_CMD_EXPAND 0x40 //don't perform cmdline completions
#define P_NODEFAULT     0x80 //don't set to default value
#define P_WAS_SET      0x100 //option has been set/reset
#define P_EXPAND       0x200 //environment expansion. NOTE: P_EXPAND can never be used for local or 
                             //hidden options!

            // when option changed, what to display:
#define P_RSTAT        0x400 //redraw status lines
#define P_REDRAW_PORT  0x800 //redraw current portal and recompute text
#define P_RBUF        0x1000 //redraw current buffer and recompute text
#define P_RALL        0x1100 //redraw all portals and recompute text
#define P_RCLR        0x1110 //clear and redraw all and recompute text

#define P_COMMA       0x2000 //comma separated list
#define P_ONECOMMA    0x2800 //P_COMMA and cannot have two consecutive commas
#define P_NODUP       0x4000 //don't allow duplicate strings
#define P_FLAGLIST    0x8000 //list of single-char flags

#define P_NFNAME     0x20000 //only normal file name chars allowed
#define P_PRI_MKRC   0x80000 //priority for :mkeeglrc (setting option has side effects)
#define P_CURSWANT  0x100000 //update curswant required; not needed when there is a redraw flag
#define P_NDNAME    0x200000 //only normal dir name chars allowed
#define P_HLONLY    0x400000 //option only changes highlight, not text
#define P_FUNC      0x800000 //accept a function reference or a lambda
#define P_COLON    0x1000000 //values use colons to create sublists
#define P_NO_MKRC  0x2000000 //don't include in :mkeeglrc output

typedef enum {
   PRINT_CHANGED,
   PRINT_NONTERMINAL
} ToPrint;

//}}}
//{{{defaults

//Default values for @errorformat.
//The "%f|%l| %m" one is used for when the contents of the quickfix window is
//written to a file.
#define DFLT_EFM   "%*[^\"]\"%f\"%*\\D%l: %m,\"%f\"%*\\D%l: %m,%-Gg%\\?make[%*\\d]: *** [%f:%l:%m,%-Gg%\\?make: *** [%f:%l:%m,%-G%f:%l: (Each undeclared identifier is reported only once,%-G%f:%l: for each function it appears in.),%-GIn file included from %f:%l:%c:,%-GIn file included from %f:%l:%c\\,,%-GIn file included from %f:%l:%c,%-GIn file included from %f:%l,%-G%*[ ]from %f:%l:%c,%-G%*[ ]from %f:%l:,%-G%*[ ]from %f:%l\\,,%-G%*[ ]from %f:%l,%f:%l:%c:%m,%f(%l):%m,%f:%l:%m,\"%f\"\\, line %l%*\\D%c%*[^ ] %m,%D%*\\a[%*\\d]: Entering directory %*[`']%f',%X%*\\a[%*\\d]: Leaving directory %*[`']%f',%D%*\\a: Entering directory %*[`']%f',%X%*\\a: Leaving directory %*[`']%f',%DMaking %*\\a in %f,%f|%l| %m"

#define DFLT_TEXTAUTO false
#define FO_ALL      "tcro/q2vlb1mMBn,aw]jp"   // for c_set()

// characters for p_ww option:
#define WW_ALL      "bshl<>[]~"

//}}}

//{(Byte \*)true, (Byte \*)0L}
//{(Byte \*)1L, (Byte \*)0L}

//Return NULL if the new value is valid and can be applied to the option.
//Otherwise return an error message.
//Type for the hook that is invoked after an option value is changed to apply the new value.
//
//Return NULL if the post-application hook ran succesfully, or error message if not.
typedef CS (*OptionSetter)(OptionChange* cha);

//Return NULL if the new value is valid and can be applied to the option.
//Otherwise return an error message.
typedef CS (*OptionValidator)(OptionChange* cha);

//Argument for the callback function (OptionExpander) invoked after a string
//option value is expanded for cmdline completion.
typedef struct {
   OptionRef ref;
   // The original option value, escaped.
   OptionValue origValue;

   // True if using set+= instead of set=
   Boole      append;
   // If we would like to add the original option value as the first choice.
   Boole      includeOrigVal;

   //Regex from the cmdline, for matching potential options against.
   RegMatch   *oe_regmatch;
   //The expansion context.
   Expand* expand;

   // The full argument passed to :set. For example, if the user inputs
   // ':set dip=icase,algorithm:my<Tab>', @expand->pattern will only have
   // 'my', but @setArg will contain the whole 'icase,algorithm:my'.
   CS setArg;
} OptExpand;

//Type for the callback function that is invoked when expanding possible string option values 
//during commline completion.
//
//Strings in returned matches will be managed and freed by caller.
//
//Return OK if the expansion succeeded (matches have to be set). Otherwise FAIL.
//
//Note: If returned FAIL or matches->len is 0, matches->c will NOT be freed by caller.
typedef int (*OptionExpander)(OptExpand* args, OUT ExpandMatch* matches);

struct Option { //:Option
   CS fullName;   // full option name
   OptionValue defaultValue; // default value for option
   
   //callback function to validate and apply the change as well as the post-processing
   OptionSetter setter;

   //callback function to invoke when expanding possible values on the cmdline. Only useful for 
   //string options.
   OptionExpander expander;

   Unt flags;  
   ScriptPos scriptPos;   // script context where the option was last set
   union {
      struct {
         OptionValue val; // the global value of a local option
         Unt offset;      // the offset from struct start. Otherwise, UNT
      } local;
      OptionRef reference; // for global options
   } c;
};

#define refStr(x) (OptionRef){.tag = OPTION_STRING, .string = (CS*)x}
#define refNum(x) (OptionRef){.tag = OPTION_NUM, .num = x}
#define refBoole(x) (OptionRef){.tag = OPTION_BOOLE, .boole = x}
#define refEnum(x) (OptionRef){.tag = OPTION_ENUM, .enume = x}
#define refFlag(x) (OptionRef){.tag = OPTION_FLAGS, .flags = x}
#define refCallback(x) (OptionRef){.tag = OPTION_CALLBACK, .callback = x}
#define portal() (OptionRef){.tag = OPTION_PORTAL_LOCAL, .num = null}


#define FOR_GLOBAL(o) \
   for (Option* o = OPTIONS_GLOBAL; o < OPTIONS_GLOBAL + OPTION_GLOBAL_COUNT; o++)
   
#define FOR_PORTAL(o) \
   for (Option* o = OPTIONS_PORTAL; o < OPTIONS_PORTAL + OPTION_PORTAL_COUNT; o++)
   
#define FOR_BOOK(o) \
   for (Option* o = OPTIONS_BOOK; o < OPTIONS_BOOK + OPTION_BOOK_COUNT; o++)
   
//}}}
//{{{forward declarations

declStruct(Option);

private void setDefaultValuesForAllOptions(SetScope scope);
private Option* findOption(CS arg);
private OptionRef getRefInScope(Option*, SetScope);
private void check_redraw(Unt flags);
private int isOptionAtDefault(Option *, OptionRef ref);
private void toString(Option*, SetScope);
private void showoneopt(Option *, SetScope setScope);
private CS setImpl(Option*, OptionValue, SetScope);
private void set_helplang_default(CS lang);
private int put_setnum(FILE *fd, CS cmd, CS name, OptionRef ref);
private int put_setstring(FILE *fd, CS cmd, CS name, OptionRef ref, Ulong flags);
private int put_setbool(FILE *fd, CS cmd, CS name, Boole value);
private int checkBreakIndent(CS briopt, Portal* po);
private CS validateAndSetListOfStrings(OUT OptionChange* cha, Arr(CS) values);
private int optionCompletionExpand(
      OUT ExpandMatch* matches, OptExpand* args, CS ((*func)(Expand *, int))
);
private int wildcharUseKeyname(OptionRef ref, long *wcp);

private void printOptionGroup(
   Arr(Option*) items, Arr(Option) group, Unt count, Unt printFlags, 
   ToPrint which, int run
);
private void
changeStringOptionDirectImpl(Option* o, CS val, SetScope scope, ScriptId setSid);
private void setScriptPos(Option* o, SetScope scope, ScriptPos scriptPos);
private void didset_options(void);
private CS expandEnvVarsInStringOption(Option* o, CS val);
private void do_spelllang_source(void);
private int find_key_option(CS arg_arg, Boole has_lt);
private void printSingleOption(
   Option* o, SetScope setScope, Unt printFlags, ToPrint which, int run,
   OUT Arr(Option*) items, OUT Unt* item_count
);
private CS get_eventignore_name(Expand *xp, int idx);
private CS check_stl_option(CS s);
private CS illegal_char_after_chr(OUT ErrBuilder* errb, int c);
private void updateBoolRef(OptionChange* cha);
private void updateNumRef(OptionChange* cha);

//}}}
//{{{general option code

private Option* expandOptionS = null;
private SetScope expandOptionScopeS = SET_GLOBAL;
private Boole expandAppendS = false;
private int expandStartColS = 0;

// Return true if option "p" has its default value.
private int
isOptionAtDefault(Option* o, OptionRef ref) {
   if (o->defaultValue.tag == OPTION_NUM)
      return (*ref.num == o->defaultValue.num);
   if (o->defaultValue.tag == OPTION_BOOLE)
      return (*ref.boole == o->defaultValue.boole);
   // OPTION_STRING
   return (STRCMP(*ref.string, o->defaultValue.string) == 0);
}

//private Boole
//eqRef(OptionRef a, OptionRef b) {
//   return (a.tag == OPTION_BOOLE && b.tag == OPTION_BOOLE && a.boole == b.boole)
//       || (a.tag == OPTION_NUM && b.tag == OPTION_NUM && a.num == b.num)
//       || (a.tag == OPTION_STRING && b.tag == OPTION_STRING && a.string == b.string)
//   ; 
//}

//Set the default value of a string option from @Option.defaultValue.
//Used for @sh, @backupskip and @term. When "escape" is true, escape spaces with a backslash.
private void
optSetStringDefault_esc(CS name, CS val, Boole escape) {
   Option* o = findOption(name);
   if (!o) {
      return;
   }
   
   CS p;
   if (escape && firstOccurrence(val, ' ') != NULL)
      p = copyStr_escaped(val, S" ");
   else
      p = copyStr(val);


   o->defaultValue.string = p;
}

//Set the value of a boolean option, and take care of side effects.
//Return NULL for success, or an error message for an error.
private CS
setBoolImpl(
   OUT Option* o,
   Boole newValue,
   SetScope setScope
){
   OptionRef ref = getRefInScope(o, setScope);
   Boole oldValue = *ref.boole;
   CS errmsg = NULL;

   // Remember where the option was set.
   setScriptPos(OUT o, setScope, scriptPosG);

   // Handle side effects of changing a bool option.
   if (o->setter) {
      OptionChange cha;
      CLEAR_FIELD(cha);
      cha.ref = ref;
      cha.setScope = setScope;
      cha.oldVal.boole = oldValue;
      cha.newVal.boole = newValue;
      errmsg = o->setter(&cha);
   } else {
      *ref.boole = newValue;
      return null;
   }
   return errmsg;
}


//For an option value that contains comma separated items, find "newVal" in
//"origVal". Return NULL if not found.
private CS
findUnchangedItemInCommaList(CS origVal, CS newVal, Unt newVallen, Ulong flags) {
   if (!origVal)
      return null;
      
   int bs = 0;
   for (CS s = origVal; *s != ZERO; ++s) {
      if ((!(flags & P_COMMA)
             || s == origVal
             || (s[-1] == ',' && !(bs & 1)))
            && STRNCMP(s, newVal, newVallen) == 0
            && (!(flags & P_COMMA)
                || s[newVallen] == ','
                || s[newVallen] == ZERO)
      )
         return s;
      //Count backslashes.  Only a comma with an even number of backslashes
      //or a single backslash preceded by a comma before it is recognized as a separator.
      if ((s > origVal + 1 && s[-1] == '\\' && s[-2] != ',') || (s == origVal + 1 && s[-1] == '\\'))
         ++bs;
      else
         bs = 0;
   }
   return null;
}

//Set the default for @backupskip to include environment variables for temp files.
private void
set_init_default_backupskip(void) {
   static CS names[4] = {S"", S"TMPDIR", S"TEMP", S"TMP"};
   ArrayList ga;

   Option* o = findOption(S"backupskip");

   ga_init2(&ga, 1, 100);
   CS p;
   for (int i = 0; i < (int)ARRAY_LENGTH(names); ++i) {
      int mustfree = false;
      int plen;
      if (*names[i] == ZERO) {
         p = S"/tmp";
         plen = (int)STRLEN_LITERAL("/tmp");
      } else {
         p = eeglGetEnv((CS)names[i]);
         plen = 0;       // will be calculated below
      }
      if (p && *p != ZERO) {
         Byte* item;
         Unt itemsize;
         int has_trailing_path_sep = false;

         if (plen == 0) {
            //the value was retrieved from the environment
            plen = (int)STRLEN(p);
            //does the value include a trailing path separator?
            if (after_pathsep(p, p + plen))
               has_trailing_path_sep = true;
         }

         //item size needs to be large enough to include "/*" and a trailing ZERO
         //note: the value (and therefore plen) may already include a path separator
         itemsize = plen + (has_trailing_path_sep ? 0 : 1) + 2;
         item = alloc(itemsize);
         //add a preceding comma as a separator after the first item
         Unt itemseplen = (ga.len == 0) ? 0 : 1;
         Unt itemlen;

         itemlen = eeSnprintf(
            item, itemsize, "%s%s*", p, (has_trailing_path_sep) ? S"" : S"/"
         );

         if (findUnchangedItemInCommaList((CS)ga.c, item, itemlen, o->flags) == NULL
               && ga_grow(&ga, (int)(itemseplen + itemlen + 1)) == OK) {
            ga.len += eeSnprintf((CS)ga.c + ga.len,
                   itemseplen + itemlen + 1, "%s%s", (itemseplen > 0) ? "," : "", item
            );
         }
         eeglFree(item);
      }
      if (mustfree)
         eeglFree(p);
   }
   if (ga.c) {
      optSetStringDefault(S"backupskip", ga.c);
      eeglFree(ga.c);
   }
}

//Initialize the @cdpath to a default value.
private void
set_init_default_cdpath(void) {
   CS cdpath = eeglGetEnv(S"CDPATH");
   if (!cdpath)
      return;

   CS buffer = alloc((STRLEN(cdpath) << 1) + 2);
   buffer[0] = ',';       // start with ",", current dir first
   int j = 1;
   for (int i = 0; cdpath[i] != ZERO; ++i) {
      if (cdpath[i] == ':') {
         buffer[j] = ',';
         j++;
      } else {
         if (cdpath[i] == ' ' || cdpath[i] == ',')
            buffer[j++] = '\\';
         buffer[j] = cdpath[i];
         j++;
      }
   }
   buffer[j] = ZERO;
   Option* cdPath = findOption(S"cdpath");
   cdPath->defaultValue = optStr(buffer);
}

//Set an option to its default value. Do not take care of side effects!
private void
setDefault(Option* o, SetScope setScope){
   OptionRef ref = getRefInScope(o, setScope);
   
   if (o->defaultValue.tag == OPTION_STRING) {
      // Use optChangeStringOptionDirect() for local options to handle freeing and allocating the value
      if ((o->flags & (P_BOOK|P_PORTAL)) != 0) {
         changeStringOptionDirectImpl(o, o->defaultValue.string, setScope, 0);
      } else {
         *ref.string = o->defaultValue.string;
      }
   } ei (o->defaultValue.tag == OPTION_NUM) {
      long defaultValue = o->defaultValue.num;

      if (ref.num == &curPor->o.scrollOff || ref.num == &curPor->o.sideScrollOff)
         //@scrolloff and @sidescrolloff local values have a
         //different default value than the global default.
         *ref.num = -1;
      else
         *ref.num = defaultValue;
   } ei (o->defaultValue.tag == OPTION_ENUM) {
      Byte defaultValue = o->defaultValue.enume;
      *ref.enume = defaultValue;
   } ei (o->defaultValue.tag == OPTION_FLAGS) {
      Unt defaultValue = o->defaultValue.flags;
      *ref.flags = defaultValue;
      *getRefInScope(o, setScope).flags = defaultValue;
   } else {  // OPTION_BOOLE
      *ref.boole = o->defaultValue.boole;
   }

   setScriptPos(o, setScope, scriptPosG);
}

void
optSetStringDefault(CS name, CS val) {
   optSetStringDefault_esc(name, val, false);
}

#if defined(EXITFREE) || defined(PROTO)

//Free all options.

void
optFreeAllOptions(void) {
   free(globalStringOptionsG);
   free(bookStringOptionsG);
   free(portalStringOptionsG);
   
   opsFreeOperatorFnOption();
   tagFreeTagFnOption();
   doFreeFindFnOption();
}
#endif

// Parse the @cursorNormal and @cursorInsert options
// Return error message for an illegal option, NULL otherwise.
private Byte
parseCursorShape(CS input) {
   if (STRCMP(input, "block") == 0) {
      return CURSOR_BLOCK;
   } ei (STRCMP(input, "bar") == 0) {
      return CURSOR_BAR;
   } else {
      return CURSOR_UNDERSCORE;
   }
}

// Initialize the options, part two: After getting visibleRowsG and visibleColsG and setting 'term'
void
optInit1(void) {
   didset_options();
   //'scroll' defaults to half the portal height. Need to calculate and set it now
   curPor->scroll = curPor->height/2 + 1;
   computeColumnsForRulerAndCommand();
}

//When @helplang is still at its default value, set it to "lang". Only the first two characters 
//of "lang" are used.
private void
set_helplang_default(CS lang) {
   if (!lang)   // safety check
      return;

   Unt langlen = STRLEN(lang);
   if (langlen < 2)   // safety check
      return;

   Option* o = findOption(S"helplang");
   if (!o || (o->flags & P_WAS_SET) != 0)
      return;

   p_hlg = copySubstr(lang, langlen);
   // zh_CN becomes "cn", zh_TW becomes "tw"
   if (STRNICMP(p_hlg, "zh_", 3) == 0 && langlen >= 5) {
      p_hlg[0] = TOLOWER_ASC(p_hlg[3]);
      p_hlg[1] = TOLOWER_ASC(p_hlg[4]);
   }
   // any C like setting, such as C.UTF-8, becomes "en"
   ei (langlen >= 1 && *p_hlg == 'C') {
      p_hlg[0] = 'e';
      p_hlg[1] = 'n';
   }
   p_hlg[2] = ZERO;
}

//Copy the new string value into allocated memory for the option.
//Can't use optChangeStringOptionDirect(), because we need to remove the backslashes.
private CS
stropt_copy_value( CS arg) {
   // get a bit too much
   Unt newlen = STRLEN(arg) + 1;
   CS newVal = alloc(newlen);
   CS s = newVal;

   //Copy the string, skip over escaped chars.
   //The reverse is found in escape_option_str_cmdline().
   while (*arg != ZERO && !SPACE_OR_TAB(*arg)) {
      int i;

      if (*arg == '\\' && arg[1] != ZERO)
         ++arg;   // remove backslash
      if ((i = utfCharLen(arg)) > 1) {
         //copy multibyte char
         MEMMOVE(s, arg, (Unt)i);
         arg += i;
         s += i;
      } else
         *s++ = *arg++;
   }
   *s = ZERO;

   return newVal;
}

//Get the string value specified for a ":set" command. The following set options are supported:
//  set {o}={val}
private CS
getNewValOfStringOption(Option* o, OUT CS* argp, OUT CS* origval_arg) {
   CS arg = *argp;
   CS origVal = *origval_arg;
   CS save_arg = NULL;

   ++arg;   // consume the `=` or `:`

   // Copy the new string into allocated memory.
   CS newVal = stropt_copy_value(arg);

   //Expand environment variables and ~.
   newVal = expandEnvVarsInStringOption(o, newVal);
   if (!newVal)
      return newVal;

   if (save_arg)
      arg = save_arg;  // arg was temporarily changed, restore it
   *argp = arg;
   *origval_arg = origVal;

   return newVal;
}

//Check for a "normal" directory or file name in some options. Disallow a path separator (slash), 
//wildcards and characters that are often illegal in a file name. Be more permissive if "secure" 
//is off.
private Boole
checkIllegalPathNames(Option* o, OptionRef ref) {
   return (((o->flags & P_NFNAME) != 0
         && eeStrpbrk(*ref.string, S"/*?[<>\r\n") != NULL
       ) 
      || ((o->flags & P_NDNAME) != 0 && eeStrpbrk(*ref.string, S"*?[|;&<>\r\n") != NULL)
   );
}

private Sbuf*
getSbuf(Option* o, SetScope setScope) {
   if (setScope == SET_GLOBAL) {
      if ((o->flags & P_GLOBAL) != 0) {
         return &globalStringOptionsG;
      } ei ((o->flags & P_BOOK) != 0) {
         return &bookStringOptionsG;
      } else {
         return &portalStringOptionsG;
      }
   } else {
      if ((o->flags & P_BOOK) != 0) {
         return &curBook->o.stringOptions;
      } else {
         return &curPor->o.stringOptions;
      }
      
   }
}

//Handle string options that need some action to perform when changed.
//The new value must be allocated.
//Returns NULL for success, or an untranslated error message for an error.
private CS
setStringImpl(
   Option* o,
   CS oldVal,    //previous value of the option
   CS newVal, 
   SetScope setScope
){
   CS errmsg = NULL;
   OptionSetter setter = o->setter;

   OptionChange cha;
   CLEAR_FIELD(cha);
   OptionRef ref = getRefInScope(o, setScope);

   // Check for a "normal" directory or file name in some options.
   if (checkIllegalPathNames(o, ref))
      errmsg = e_invalid_argument;
   else {
      cha.ref = ref;
      cha.setScope = setScope;
      cha.oldVal.string = oldVal;
      cha.newVal.string = newVal;
      cha.buf = getSbuf(o, setScope);
      if (setter) { // normal option updatin'
         //Invoke the option specific callback to validate and apply the new option value.
         errmsg = setter(&cha);
      } else {
         updateStringRef(&cha);
      }
   } 

   if (errmsg) {
      return errmsg;
   } 
   
   if (ref.string == &(curPor->ownSyntax->spellLang))
      do_spelllang_source();

   if (curPor->cursWant != MAXCOL
         && (o->flags & (P_CURSWANT | P_RALL)) != 0
         && (o->flags & P_HLONLY) == 0
   ) 
      curPor->setCursWant = true;

   if ((o->flags & OPT_NO_REDRAW) == 0) {
      check_redraw(o->flags);
   }

   return errmsg;
}

private CS
parseAndSetEnum(OUT Option* o, CS arg, SetScope setScope) {
   OptionRef ref = getRefInScope(o, setScope);
   Byte oldValue = *ref.enume;

   if (!o->setter) {
      return e_setter_required_for_enum_or_flag_option;
   } 
   OptionChange cha;
   CLEAR_FIELD(cha);
   cha.ref = ref;
   cha.setScope = setScope;
   cha.oldVal.enume = oldValue;
   cha.newVal.string = arg;
   return o->setter(&cha);
}

private CS
parseAndSetFlags(OUT Option* o, CS arg, SetScope setScope) {
   OptionRef ref = getRefInScope(o, setScope);
   Unt oldValue = *ref.flags;

   if (!o->setter) {
      return e_setter_required_for_enum_or_flag_option;
   } 
   OptionChange cha;
   CLEAR_FIELD(cha);
   cha.ref = ref;
   cha.setScope = setScope;
   cha.oldVal.flags = oldValue;
   cha.newVal.string = arg;
   return o->setter(&cha);
}

private CS
parseAndSetCallback(OUT Option* o, CS arg, SetScope setScope) {
   OptionRef ref = getRefInScope(o, setScope);
   Unt oldValue = *ref.flags;

   if (!o->setter) {
      return e_setter_required_for_enum_or_flag_option;
   } 
   OptionChange cha;
   CLEAR_FIELD(cha);
   cha.ref = ref;
   cha.setScope = setScope;
   cha.oldVal.flags = oldValue;
   cha.newVal.string = arg;
   return o->setter(&cha);
}

//Part of c_set() for string options. Return FAIL on failure, do not process further options.
private CS
changeStringOption(OUT Option* o, CS arg, SetScope scope) {
   CS errmsg = null;
   
   CS oldVal = NULL;
   CS saved_newVal = NULL;

   //The old value is kept until we are sure that the new value is valid.
   CS origVal = *(getRefInScope(o, scope)).string;

   //Get the new value for the option
   CS newVal = getNewValOfStringOption(o, &arg, &origVal);

   if (!starting && origVal && newVal) {
      //newVal (and ref) may become invalid if the buffer is closed by autocommands.
      saved_newVal = copyStr(newVal);
   }

   //Handle side effects. Note: when setting @syntax, autocommands may be triggered 
   //that can cause havoc.
   errmsg = setStringImpl(OUT o, oldVal, newVal, scope);

   eeglFree(oldVal);
   eeglFree(saved_newVal);
   return errmsg;
}

//Set a boolean option. Return an untranslated error message or NULL.
private CS
parseAndSetBool(OUT Option* o, CS newString, SetScope setScope){
   if (eq(newString, S"true")) {
      return setBoolImpl(o, true, setScope);
   } ei (eq(newString, S"false")) {
      return setBoolImpl(o, false, setScope);
   } else {
      return e_bool_required_for_argument_nr;
   }
}

//Set the value of a number option, and take care of side effects.
//Return NULL for success, or an error message for an error.
private CS
setNumericImpl(
   OUT Option* o,
   OUT OptionRef ref,
   long newValue,
   SetScope setScope
){
   CS errmsg = NULL;

   long oldValue = *(ref.num);
   
   //Invoke the option specific callback function to validate and apply the new value.
   if (o->setter) {
      OptionChange cha;
      CLEAR_FIELD(cha);
      cha.ref = ref;
      cha.setScope = setScope;
      cha.oldVal.num = oldValue;
      cha.newVal.num = newValue;
      if ((errmsg = o->setter(&cha))) {
         return errmsg;
      } 
   } else {
      //Actually change the option value
      *ref.num = newValue;
   }
   
   //Remember where the option was set.
   setScriptPos(o, setScope, scriptPosG);
   o->flags |= P_WAS_SET;

   computeColumnsForRulerAndCommand();             // in case @columns changed

   if (curPor->cursWant != MAXCOL
          && (o->flags & (P_CURSWANT | P_RALL)) != 0
          && (o->flags & P_HLONLY) == 0
   )
      curPor->setCursWant = true;

   if ((o->flags & OPT_NO_REDRAW) == 0)
      check_redraw(o->flags);

   return errmsg;
}

//Set a numeric option. Return an untranslated error message or NULL.
private CS
parseAndSetNumeric(OUT Option* o, CS arg, SetScope setScope) {
   Long newValue;
   CS errmsg = NULL;
   
   //Different ways to set a number option:
   //[-]0-9       set number
   //other       error
   if (*arg == '-' || EE_ISDIGIT(*arg)) {
      // Allow negative (for @undolevels) and hex numbers.
      int i; 
      readLongNumber(arg, NULL, OUT &i, STR2NR_ALL, OUT &newValue, NULL, 0, true, NULL);
      if (i == 0 || (arg[i] != ZERO && !SPACE_OR_TAB(arg[i]))) {
         errmsg = e_number_required_after_equal;
         goto skip;
      }
   } else {
      errmsg = e_number_required_after_equal;
      goto skip;
   }

   OptionRef ref = getRefInScope(o, setScope);
   errmsg = setNumericImpl(OUT o, OUT ref, newValue, setScope);

skip:
   return errmsg;
}

//Call this when an option has been given a new value through a user command.
//Set the P_WAS_SET flag.
private void
did_set_option(Option* o){
   o->flags |= P_WAS_SET;
}

//The only way to set an option to a new value
private CS
setImpl(Option* o, OptionValue newValue, SetScope setScope) {
   CS errmsg;
   OptionRef ref = getRefInScope(o, setScope);
   switch (o->defaultValue.tag) {
   case OPTION_BOOLE:
      if (newValue.tag != OPTION_BOOLE) {
         return e_trying_to_set_option_to_wrong_type;
      }
   
      errmsg = setBoolImpl(o, newValue.boole, setScope);
      break;
   case OPTION_NUM: {
      if (newValue.tag != OPTION_NUM) {
         return e_trying_to_set_option_to_wrong_type;
      }
      errmsg = setNumericImpl(OUT o, OUT ref, newValue.num, setScope);
      break;
      } 
   case OPTION_STRING:  {
      if (newValue.tag != OPTION_STRING) {
         return e_trying_to_set_option_to_wrong_type;
      }
      ref = getRefInScope(o, setScope);
      errmsg = setStringImpl(o, *(ref.string), newValue.string, setScope);
      break;
      } 
   case OPTION_ENUM: 
      if (newValue.tag != OPTION_STRING) {
         return e_trying_to_set_option_to_wrong_type;
      }
      errmsg = parseAndSetEnum(OUT o, newValue.string, setScope);
      break;
   case OPTION_FLAGS: 
      if (newValue.tag != OPTION_STRING) {
         return e_trying_to_set_option_to_wrong_type;
      }
      errmsg = parseAndSetFlags(o, newValue.string, setScope);
      break;
   case OPTION_CALLBACK: 
      if (newValue.tag != OPTION_STRING) {
         return e_trying_to_set_option_to_wrong_type;
      }
      errmsg = parseAndSetCallback(o, newValue.string, setScope);
      break;
   }
   
   // Remember where the option was set.
   setScriptPos(OUT o, setScope, scriptPosG);
   o->flags |= P_WAS_SET;
   
   if (newValue.tag != OPTION_STRING) {

      computeColumnsForRulerAndCommand(); // in case @ruler or @showcmd changed

      if (curPor->cursWant != MAXCOL
              && (o->flags & (P_CURSWANT | P_RALL)) != 0
                  && (o->flags & P_HLONLY) == 0)
         curPor->setCursWant = true;

      if ((o->flags & OPT_NO_REDRAW) == 0)
         check_redraw(o->flags);
   } 

   return errmsg;
}

private CS
setFromString(OUT Option* o, CS arg, CS newVal, SetScope setScope) {
   CS errmsg;
   switch (o->defaultValue.tag) {
   case OPTION_BOOLE:
      errmsg = parseAndSetBool(OUT o, newVal, setScope);
      goto end;
   case OPTION_NUM:
      errmsg = parseAndSetNumeric(OUT o, arg, setScope);
      goto end;
   case OPTION_STRING: 
      if ((errmsg = changeStringOption(OUT o, arg, setScope)) != null) {
         return errmsg;
      }
      goto end;
   case OPTION_ENUM: 
      errmsg = parseAndSetEnum(OUT o, arg, setScope);
      goto end;
   case OPTION_FLAGS: 
      errmsg = parseAndSetFlags(OUT o, arg, setScope);
      goto end;
   case OPTION_CALLBACK: 
      errmsg = parseAndSetCallback(OUT o, arg, setScope);
      goto end;
   }
   if (o->defaultValue.tag != OPTION_STRING) {
      computeColumnsForRulerAndCommand(); // in case @ruler or @showcmd changed

      if (curPor->cursWant != MAXCOL
              && (o->flags & (P_CURSWANT | P_RALL)) != 0
                  && (o->flags & P_HLONLY) == 0)
         curPor->setCursWant = true;

      if ((o->flags & OPT_NO_REDRAW) == 0)
         check_redraw(o->flags);
   } 

end:
   if (!errmsg) {
      did_set_option(o);
   }
   return errmsg;
}

//Set an option to a new value.
private CS
parseAndSetImpl(Option* o, CS arg, SetScope setScope) {
   CS errmsg = NULL;
   
   // Copy the new string into allocated memory.
   CS newVal = stropt_copy_value(arg);

   //Expand environment variables and ~.
   newVal = expandEnvVarsInStringOption(o, newVal);
   setFromString(OUT o, arg, newVal, setScope);
   return errmsg;
}

private CS
tryFindOptionFromCommand(OUT Option** o, OUT CS* arg) {
   // find end of name
   int len = 0;
   while (ASCII_ISALNUM((*arg)[len]))
      ++len;
   
   if (len == 0)
      return e_invalid_argument;

   // remember character after option name
   Unt afterchar = (*arg)[len];   // character just after option name
   (*arg)[len] = ZERO;
   *o = findOption(*arg);
   (*arg)[len] = afterchar;
   *arg += len;
   return o ? null : e_option_not_supported;
}

//:set an option to a new value. Return NULL if OK, return an untranslated error message when 
//something is wrong. "errb[errbuflen]" can be used to create the error message.
private CS
parseAndSet(SetScope setScope, OUT CS* arg) {
   Option* o;
   CS errmsg = tryFindOptionFromCommand(OUT &o, OUT arg);
   if (errmsg)
      return errmsg;

   *arg = skipwhite(*arg);

   Unt nextchar = (*arg)[0];   // next non-white char after option name
   if (nextchar != '=') {
      return e_use_get_not_set_for_reading_options;
   }
   *arg = skipwhite(*arg + 1); // consume `=`
   // Make sure the option value can be changed.
   if (frozenOptionsG && setScope == SET_GLOBAL)
      return e_options_are_frozen;
   
   return parseAndSetImpl(o, *arg, (o->flags & P_GLOBAL) != 0 ? SET_GLOBAL : setScope);
}

CS
optSetByName(CS name, OptionValue newVal, SetScope setScope) {
   Option* o = findOption(name);
   if (!o) {
      return e_option_not_supported;
   }
   return setImpl(o, newVal, setScope);
}

//Convert a key name or string into a key value. Used for @termwinkey, @wildchar and @wildcharm 
//options. When "multi_byte" is true, allow for multi-byte characters.
Unt
stringToChar(CS arg, Boole multi_byte) {
   if (*arg == '<' && arg[1] != ZERO)
      return find_key_option(arg + 1, true);
   if (*arg == '^' && arg[1] != ZERO) {
      int key = charMinusCtrl(arg[1]);
      if (key == 0)      // ^@ is <ZERO>
         key = K_ZERO;
      return key;
   }
   if (multi_byte)
      return mb_ptr2char(arg);
   return *arg;
}

//Expand environment variables for some string options. If "newVal" is NULL, expand the current 
//value of the option. Return a fresh allocation if expanded, or "newVal".
private CS
expandEnvVarsInStringOption(Option* o, CS newVal) {
   //if option doesn't need expansion, nothing to do
   if ((o->flags & P_EXPAND) == 0)
      return newVal;

   //If val is longer than MAXPATHL, no meaningful expansion can be done;
   //doExpandEnv() would truncate the string.
   if (newVal && STRLEN(newVal) > MAXPATHL)
      return newVal;

   if (!newVal) {
      if ((o->flags & (P_BOOK|P_PORTAL)) != 0) {
         newVal = o->c.local.val.string;
      } else {
         newVal = *(o->c.reference.string);
      }
   }
      
   if (!newVal)
      return null;

   doExpandEnvVarsWithEscaped(OUT nameBuffTextG, newVal, false, null);
   if (eq(nameBuffG, newVal))   // they are the same
      return newVal;

   return copyStr(nameBuffG);
}

//Get the script context of global option "name".
ScriptPos*
optGetScriptPos(CS name) {
   Option* o = findOption(name);

   if (o)
      return &o->scriptPos;
   internalErrFmtMsg("no such option: %s", name);
   return NULL;
}

//Called after an option changed: check if something needs to be redrawn.
private void
check_redraw(Unt flags) {
   // Careful: P_RCLR and P_RALL are a combination of other P_ flags
   int      doclear = (flags & P_RCLR) == P_RCLR;
   int      all = ((flags & P_RALL) == P_RALL || doclear);

   if ((flags & P_RSTAT) || all)   // mark all status lines dirty
      status_redraw_all();

   if ((flags & P_RBUF) || (flags & P_REDRAW_PORT) || all) {
      if (flags & P_HLONLY)
         redraw_later(UPD_NOT_VALID);
      else
         didChangePortalSettingCurPor();
   }
   if (flags & P_RBUF)
      drawCurBookLater(UPD_NOT_VALID);
   if (doclear)
      redraw_all_later(UPD_CLEAR);
   ei (all)
      redraw_all_later(UPD_NOT_VALID);
}

//Get the value for an option.
//"flagsp" (if not NULL) is set to the option flags (P_xxxx).
OptionValue
optGetValue(OUT Unt* flagsp, CS name, int scope) {
   Option* o = findOption(name);
   if (!o) {        // option not in the table
      return (OptionValue){};
   }

   OptionRef ref = getRefInScope(o, scope);

   if (flagsp)
      // Return the P_xxxx option flags.
      *flagsp = o->flags;

   OptionValue retVal = (OptionValue){.tag = ref.tag};
   
   if (ref.tag == OPTION_STRING) {
      retVal.string = copyStr(*(ref.string));
   } ei (ref.tag == OPTION_NUM)
      retVal.num = *(ref.num);
   ei (ref.tag == OPTION_ENUM)
      retVal.enume = *(ref.enume);
   else {
      retVal.boole = *(ref.boole);
   }
   return retVal;
}

Boole
optIsFnOption(Unt flags) {
   return (flags & P_FUNC) > 0;
}

// Set option "varname" to the value of "varp" for the current buffer/portal.
void
optSetFromVar(CS varname, Var *varp) {
   Byte   nbuf[NUMBUFLEN];
   Boole error = false;

   Option* o = findOption(varname);
   if (!o) {
      showErrFmtMsg(_(e_unknown_option_str_2), varname);
      return;
   }
   OptionValue optVal = (OptionValue){.tag = o->defaultValue.tag};
   if (varp->tag == VAR_STRING) {
      if (optVal.tag != OPTION_STRING) {
         emsg(_(e_string_required));
         return;
      }
      optVal.string = convertVarToString(varp, nbuf);
   } else {
      if (optVal.tag == OPTION_NUM)
         optVal.num = (long)varGetNumberChk(varp, OUT &error);
      ei (optVal.tag == OPTION_BOOLE)
         optVal.boole = varGetNumberChk(varp, OUT &error) > 0;
   }
   if (!error)
      optChangeAndReportError(varname, optVal, SET_LOCAL);
}

// Escape an option value that can be used on the command-line with :set.
// Caller needs to free the returned string, unless NULL is returned.
private CS
escape_option_str_cmdline(CS var) {
   //A backslash is required before some characters. This is the reverse of what happens in c_set()
   return copyStr_escaped(var, escape_chars);
}


//Return true if 'optStr' either matches 'regmatch' or fuzzy matches 'pat'.
//
//If 'fuzzy' is false and if 'optStr' matches the regular expression 'regmatch', then store the 
//match in matches[idx] and return true.
//
//If 'fuzzy' is true and if 'optStr' fuzzy matches
//'fuzzystr', then store the match details in fuzmatch[idx] and return true.
private Boole
matchString(
   CS optStr,
   RegMatch* regmatch,
   ExpandMatch* matches,
   Boole isFuzzy,
   CS fuzzystr,
   Fuzzy* fuzzy
) {
   if (isFuzzy) {
      int score = fuzzyMatchStr(optStr, fuzzystr);
      if (score != FUZZY_SCORE_NONE) {
         addFuzzyMatch((FuzzyMatch){.score = score, .str = copyStrA(optStr, fuzzy->a)}, OUT fuzzy);
         return true;
      }
   } else {
      if (eeRegexec(regmatch, optStr, (ColNr)0)) {
         addExpandMatch(copyStrA(optStr, matches->a), matches);
         return true;
      }
   }

   return false;
}

// Expansion handler for `:set=` or `:set+=` when the option has a custom expansion handler.
int
optExpandForSet(Expand* xp, RegMatch* regmatch, OUT ExpandMatch* matches){
   if (!expandOptionS || expandOptionS->expander) {
      //Not supposed to reach this. This function is only for options with
      //custom expansion callbacks.
      return FAIL;
   }

   OptExpand cha;
   cha.ref = getRefInScope(expandOptionS, expandOptionScopeS);
   cha.append = expandAppendS;
   cha.oe_regmatch = regmatch;
   cha.expand = xp;
   cha.setArg = xp->fullInput + expandStartColS;
   cha.includeOrigVal = !expandAppendS && (*cha.setArg == ZERO);

   //Retrieve the existing value, but escape it as a reverse of setting it. We technically only 
   //need to do this when append or includeOrigVal is true.
   toString(expandOptionS, expandOptionScopeS);
   CS var = nameBuffG;
   CS buffer = escape_option_str_cmdline(var);

   cha.origValue = optStr(buffer);

   int num_ret = expandOptionS->expander(&cha, matches);

   eeglFree(buffer);
   return num_ret;
}

//Get the value for the numeric or string option in a nice format into nameBuffG[].
private void
toString(Option* o, SetScope scope) {
   OptionRef ref = getRefInScope(o, scope);
   if (ref.tag == OPTION_NUM || ref.tag == OPTION_FLAGS || ref.tag == OPTION_ENUM) {
      long wc = 0;
      if (wildcharUseKeyname(ref, &wc))
         STRCPY(nameBuffG, get_special_key_name((int)wc, 0));
      ei (wc != 0)
         STRCPY(nameBuffG, transchar((int)wc));
      else
         SPRINTF(nameBuffG, "%ld", *ref.num);
   } ei (ref.tag == OPTION_BOOLE) {
      if (*ref.boole) {
         STRCPY(nameBuffG, S"true");
      } else {
         STRCPY(nameBuffG, S"false");
      }
   } ei (ref.tag == OPTION_STRING) {   // P_STRING
      if (*ref.string != null) {
         if ((o->flags & P_EXPAND) != 0)
            home_replace(*ref.string, nameBuffG, MAXPATHL, false);
         else
            copySubstrToAllocation(nameBuffG, (Text){*ref.string, MAXPATHL - 1});
      }
   }
}

//Return true when option "name" has been set.
Boole
optWasSet(CS name) {
   Option* o = findOption(name);
   if (!o)
      return false;
   return (o->flags & P_WAS_SET) > 0;
}

private Unt
calcNewBufferCap(Unt oldSize) {
   if (oldSize == 0) {
      return 256;
   } else {
      return oldSize + (oldSize / 5) + 10;
   }
}

//An option that accepts a list of flags is changed. eg. @viewoptions, @switchbook etc
//Check an option that can be a range of string values. Empty is always OK.
private CS
readOptionFlags(OptionChange* cha, Arr(CS) validValues, OUT Unt *flagp) {
   Unt newFlags = 0;
   CS newVal = cha->newVal.string;
   while (*newVal != ZERO) {
      for (int i = 0; ; ++i) {
         if (!validValues[i])
            return e_invalid_argument;

         int len = (int)STRLEN(validValues[i]);
         if (STRNCMP(validValues[i], newVal, len) == 0
             && (newVal[len] == ',' || newVal[len] == ZERO)
         ) {
            newVal += len + (newVal[len] == ',');
            newFlags |= (1 << i);
            break;      // check next item in newVal list
         }
      }
   }
   if (flagp)
      *flagp = newFlags;

   return null;
}

//A flag option is changed. cha->ref must be a string option ref.
//e.g. @scrollopt, @wildoptions, etc.
private CS
validateAndSetListOfStrings(OUT OptionChange* cha, Arr(CS) values) {
   Unt new;
   CS errMsg = readOptionFlags(cha, values, OUT &new);
   if (!errMsg) {
      *(cha->ref.flags) = new;
   }
   return errMsg;
}


//Expand an option that accepts a list of fixed string values with a known number of items.
private int
expandFlagOption(
   OUT ExpandMatch* matches,
   OptExpand* args,
   Arr(CS) const values,
   Unt const numValues
) {
   RegMatch   *regmatch = args->oe_regmatch;
   Boole      include_orig_val = args->includeOrigVal;
   OptionValue origVal = args->origValue;

   if (include_orig_val && *origVal.string != ZERO) {
      addExpandMatch(origVal.string, OUT matches);
   }

   for (CS* val = values; val < values + numValues; val++) {
      if (include_orig_val && *origVal.string != ZERO) {
         if (STRCMP(*val, origVal.string) == 0)
            continue;
      }
      if (eeRegexec(regmatch, (*val), (ColNr)0)) {
         addExpandMatch(copyStr(*val), OUT matches);
      }
   }
   if (matches->len == 0) {
      return FAIL;
   }
   return OK;
}

//Expand an option which is a string of single-char flags.
private int
expand_set_opt_listflag(OUT ExpandMatch* matches, OptExpand *args, CS flags) {
   OptionValue origVal = args->origValue;
   CS cmdline_val = args->setArg;
   Boole include_orig_val = args->includeOrigVal && (*origVal.string != ZERO);

   if (include_orig_val) {
      addExpandMatch(copyStrA(origVal.string, matches->a), OUT matches);
   }

   for (CS flag = flags; *flag != ZERO; flag++) {
      if (args->append && firstOccurrence(origVal.string, *flag) != NULL)
         continue;

      if (firstOccurrence(cmdline_val, *flag) == NULL) {
         if (include_orig_val && origVal.string[1] == ZERO && *flag == origVal.string[0]) {
            //This value is already used as the first choice as it's the
            //existing flag. Just skip it to avoid duplicate.
            continue;
         }
         addExpandMatch(copySubstrA(flag, 1, matches->a), OUT matches);
      }
   }

   if (matches->len == 0) {
      return FAIL;
   }
   return OK;
}

private CS
illegal_char(OUT ErrBuilder* errb, Unt c) {
   if (!errb->c)
      return null;
   eeSnprintf(errb->c, errb->len, _(e_illegal_character_str), transchar(c));
   return errb->c;
}

//An option which is a list of flags is set.  Valid values are in 'flags'.
private CS
did_set_option_listflag(CS val, CS flags, OUT ErrBuilder* errb) {
   for (CS s = val; *s; ++s) {
      if (firstOccurrence(flags, *s) == NULL)
         return illegal_char(OUT errb, *s);
   } 

   return NULL;
}

#define INC 20
#define GAP 3

private void
printOptionGroup(
   Arr(Option*) items, Arr(Option) group, Unt count, Unt printFlags, 
   ToPrint which, int run
) {

   //collect the items in items[]
   Unt item_count = 0;
   for (Option* o = group; o < group + count; o++) {
      //apply :filter /pat/
      if (message_filtered(o->fullName))
         continue;

      printSingleOption(o, SET_LOCAL, printFlags, which, run, OUT items, OUT &item_count);
      printSingleOption(o, SET_GLOBAL, printFlags, which, run, OUT items, OUT &item_count);
   }

   // display the items
   int rows;
   if (run == 0) {
      int cols = (visibleColsG + GAP - 3) / INC;
      if (cols == 0)
         cols = 1;
      rows = (item_count + cols - 1) / cols;
   } else   // run == 1
      rows = (int)item_count;
   for (int row = 0; row < rows && !gotInterruptG; ++row) {
      msg_putchar('\n');  //go to next line
      if (gotInterruptG)  //'q' typed in more
         break;
      int col = 0;
      for (int i = row; i < (int)item_count; i += rows) {
         msgColG = col;   // make columns
         showoneopt(items[i], SET_LOCAL);
         showoneopt(items[i], SET_GLOBAL);
         col += INC;
      }
      out_flush();
      ui_breakcheck();
   }
}

//showoneopt: show the value of one option.
private void
showoneopt(Option* o, SetScope setScope) {   // OPT_LOCAL or OPT_GLOBAL
   Boole save_silent = silentModeG;

   silentModeG = false;
   info_message = true;   // use mch_msg(), not mch_errmsg()

   OptionRef ref = getRefInScope(o, setScope);

   // for @modified' we also need to check if 'ff' changed.
   if (o->defaultValue.tag == OPTION_BOOLE
         && (ref.boole == &curBook->wasModified ? !doWasCurBookChanged() : !(*(ref.boole)))
   )
      msg_puts(S"no");
   ei ((o->defaultValue.tag == OPTION_BOOLE) && !(*(ref.boole)))
      msg_puts(S"--");
   else
      msg_puts(S"  ");
   msg_puts(o->fullName);
   if (o->defaultValue.tag != OPTION_BOOLE) {
      msg_putchar('=');
      // put value string in nameBuffG
      toString(o, setScope);
      msg_outtrans(nameBuffG);
   }

   silentModeG = save_silent;
   info_message = false;
}

//Generate set commands for the local fold options only. Used when
//[sessionoptions] or [viewoptions] contains "folds" but not "options".
int
makefoldset(FILE *fd) {
   if (put_setnum(fd, S"set", S"foldmethod", refNum((long*)&curPor->o.foldMethod)) == FAIL
       || put_setstring(fd, S"set", S"foldexpr", refStr(&curPor->o.foldExpr), 0) == FAIL
       || put_setstring(fd, S"set", S"foldmarker", refStr(&curPor->o.foldMarker), 0) == FAIL
       || put_setstring(fd, S"set", S"foldignore", refStr(&curPor->o.foldIgnore), 0) == FAIL
       || put_setnum(fd, S"set", S"foldlevel", refNum(&curPor->o.foldLevel)) == FAIL
       || put_setbool(fd, S"set", S"foldenable", curPor->o.foldEnable) == FAIL
   )
      return FAIL;

   return OK;
}

////Return true if "val" is a valid @filetype name. Also used for @syntax and @keymap.
//private int
//valid_filetype(CS val) {
//   return valid_name(val, S".-_");
//}

private Byte
parseEnumValue(CS newVal, Arr(CS) validValues) {
   for (Unt i = 0; validValues[i]; ++i) {
      if (STRCMP(validValues[i], newVal) == 0) {
         return i;
      }
   }
   return 255;
}

//Check validity of options with the @statusline format.
//Return an untranslated error message or NULL.
private CS
check_stl_option(CS s) {
   int groupdepth = 0;
   static Byte errbuf[ERR_BUFLEN];
   ErrBuilder errb = (ErrBuilder){.c = errbuf, .len = ERR_BUFLEN};

   while (*s) {
      // Check for valid keys after % sequences
      while (*s && *s != '%')
         s++;
      if (!*s)
         break;
      s++;
      if (*s == '%' || *s == STL_TRUNCMARK || *s == STL_SEPARATE) {
         s++;
         continue;
      }
      if (*s == ')') {
         s++;
         if (--groupdepth < 0)
            break;
         continue;
      }
      if (*s == '-')
          s++;
      while (EE_ISDIGIT(*s))
          s++;
      if (*s == STL_USER_HL)
          continue;
      if (*s == '.') {
          s++;
          while (*s && EE_ISDIGIT(*s))
         s++;
      }
      if (*s == '(') {
         groupdepth++;
         continue;
      }
      if (firstOccurrence(STL_ALL, *s) == NULL) {
         return illegal_char(OUT &errb, *s);
      }
      if (*s == '{') {
         int reevaluate = (*++s == '%');

         if (reevaluate && *++s == '}')
            // "}" is not allowed immediately after "%{%"
            return illegal_char(OUT &errb, '}');
         while ((*s != '}' || (reevaluate && s[-1] != '%')) && *s)
            s++;
         if (*s != '}')
            return e_unclosed_expression_sequence;
      }
   }
   if (groupdepth != 0)
      return e_unbalanced_groups;
   return NULL;
}

//}}}
//{{{setter functions (validation, option setting and postprocessing actions)

// Process the updated @balloonevalterm option value.
private CS
did_set_balloonevalterm(OptionChange* cha) {
   updateStringRef(cha);
   mch_bevalterm_changed();
   return NULL;
}

//Process the updated @binary option value.
CS
optSetBinary(OptionChange* cha) {
   if (cha->setScope != SET_LOCAL) {
      return e_unknown_option;
   }
   
   updateBoolRef(cha);
   
   Boole newVal = cha->newVal.boole;
   
   //The option values that are changed when @binary changes are
   //copied when @binary is set and restored when @binary is reset.
   if (newVal) {
      curBook->o.textWidth = 0;   // no automatic line wrap
      curBook->o.wrapMargin = 0;  // no automatic line wrap
      curBook->o.expandTab = 0;   // no expandtab
   }
   // Remember where the dependent option were reset
   CS options[] = {S "textwidth", S"wrapmargin", S"expandtab"};
   
   for (Unt i = 0; i < 3; ++i) {
      setScriptPos(findOption(options[i]), SET_LOCAL, scriptPosG);
   }
   
   needRedrawTabpanelG = true;

   return NULL;
}

private void
resizeOrPlanResizingWindow() {
   if (fullScreenG)
      set_shellsize((int)visibleColsG, (int)visibleRowsG, true);
   else {
      // Postpone the resizing; check the size and cmdline position for messages.
      check_shellsize();
      if (commlineRowG > visibleRowsG - commlineHeightG && visibleRowsG > commlineHeightG)
         commlineRowG = visibleRowsG - commlineHeightG;
   }
}

private CS
setVisibleLines(OptionChange* cha) {
   if (updating_screen) {
      // Changing the window size is not allowed while updating the screen.
      return null;
   }
   if (cha->newVal.num < minRowsForAllTabs() && fullScreenG) {
      showErrFmtMsg(_(e_need_at_least_nr_lines), minRowsForAllTabs());
      return e_need_at_least_nr_lines;
   } else {
      visibleRowsG = cha->newVal.num;
   }
   clampScreenSize();
   if (cha->newVal.num != cha->oldVal.num) {
      resizeOrPlanResizingWindow();
   }
   return null;
}

private CS
setVisibleCols(OptionChange* cha) {
   if (updating_screen) {
      // Changing the window size is not allowed while updating the screen.
      return null;
   }
   
   if (cha->newVal.num < MIN_COLUMNS && fullScreenG) {
      showErrFmtMsg(_(e_need_at_least_nr_columns), MIN_COLUMNS);
      return e_need_at_least_nr_lines;
   } else {
      visibleColsG = cha->newVal.num;
   }
   
   clampScreenSize();

   if (cha->newVal.num != cha->oldVal.num) {
      resizeOrPlanResizingWindow();
   }
   return null;
}

//Process the updated @buflisted
private CS
setBookListed(OptionChange* cha) {
   // when @buflisted changes, trigger autocommands
   updateBoolRef(cha);
   if (cha->oldVal.boole != curBook->o.bookListed) {
      applyAutocomms(
         curBook->o.bookListed ? EVENT_BUFADD : EVENT_BUFDELETE, NULL, NULL, true, curBook
      );
   } 
   return NULL;
}

private CS
setCommHeight(OptionChange* cha) {
   long oldVal = cha->oldVal.num;
   long newVal = cha->newVal.num;

   // if cmdHeight changed value, change the command line height
   if (newVal < MIN_COMMHEIGHT) {
      return e_argument_must_be_positive;
   }
   if (newVal > visibleRowsG - min_rows() + MIN_COMMHEIGHT)
      newVal = visibleRowsG - min_rows() + MIN_COMMHEIGHT;

   //Only compute the new portal layout when startup has been
   //completed. Otherwise the frame sizes may be wrong.
   if ((newVal != oldVal || topframeG->width != visibleRowsG - commlineHeightG)
          && fullScreenG
   ) {
      updateNumRef(cha);
      command_height();
   } 
   return null;
}

//Process the updated @diff
private CS
did_set_diff(OptionChange* cha) {
   //May add or remove the book from the list of diff books.
   updateBoolRef(cha);
   diffBookAdjust(curPor);
   
   if (curPor->o.foldMethod == FOLD_DIFF)
      foldUpdateAll(curPor);
      
   //when @scrollbind is set: snapshot the current position to avoid a jump
   //at the end of normal_cmd()
   if (cha->newVal.boole)
      return null;
   normPostProcessScrollbind(false);
   curPor->scbindPos = curPor->topLine;
   return NULL;
}

private CS
did_set_equalalways(OptionChange* cha) {
   updateBoolRef(cha);
   if (cha->newVal.boole && !cha->oldVal.boole)
      portEqualizeHeight(curPor, false, 0);
   return NULL;
}

private CS
did_set_foldlevel(OptionChange* cha) {
   if (cha->newVal.num < 0)
      return e_argument_must_not_be_negative;
   updateNumRef(cha);
   newFoldLevel();
   return NULL;
}

private CS
did_set_hlsearch(OptionChange* cha) {
   // when @hlsearch is set or reset: reset hiliteSearchG
   updateBoolRef(cha);
   setHlsearch(true);
   return NULL;
}

private CS
did_set_ignorecase(OptionChange* cha) {
   // when @ignorecase is set or reset and @hlsearch is set, redraw
   updateBoolRef(cha);
   if (cha->newVal.boole)
      redraw_all_later(UPD_SOME_VALID);
   return NULL;
}

CS
setModifiable(OptionChange* cha) {
   updateBoolRef(cha);
   return NULL;
}

private CS
did_set_numberwidth(OptionChange* cha) {
   CS errmsg = NULL;
   long new = cha->newVal.num;
   // @numberwidth must be positive
   if (new < 1) {
      return e_argument_must_be_positive;
   } ei (new > 20) {
      return e_invalid_argument;
   }
   updateNumRef(cha);
   curPor->lineCountSaved = 0; // trigger a redraw

   return errmsg;
}

//Process the new @maxsearchcount option value.
private CS
did_set_maxsearchcount(OptionChange* cha) {
   long new = cha->newVal.num;
   if (new <= 0)
      return e_argument_must_be_positive;
   ei (new > 9999) // if you increase this limit, also increase search.c:SEARCH_STAT_BUF_LEN
      return e_invalid_argument;
      
   updateNumRef(cha);
   return null;
}

private CS
setShiftWidth(OptionChange* cha) {
   if (cha->newVal.num <= 0) {
      return e_argument_must_be_positive;
   }
   updateNumRef(cha);

   if (curPor->o.foldMethod == FOLD_INDENT)
      foldUpdateAll(curPor);

   return null;
}

private CS
did_set_smoothscroll(OptionChange* cha) {
   updateBoolRef(cha);
   if (cha->newVal.boole == false)
      curPor->skipCol = 0;
   return NULL;
}

private CS
did_set_swapfile(OptionChange* cha) {
   updateStringRef(cha);
   //when @swapfile is set, create swapfile, when reset remove swapfile
   if (curBook->o.swapFile && swapEnabledG)
      memOpenSwapFile(curBook);      // create the swap file
   else
      // no need to reset curBook->maySwap, memOpenSwapFile() will check buf->o.swapFile
      mf_close_file(curBook, true);   // remove the swap file
   return NULL;
}

private CS
did_set_termwinscroll(OptionChange* cha) {
   if (cha->newVal.num < 1) {
      return e_argument_must_be_positive;
   }
   updateNumRef(cha);
   return null;
}

private CS
did_set_textwidth(OptionChange* cha) {
   if (cha->newVal.num < 0) {
      return e_argument_must_be_positive;
   }
   updateNumRef(cha);

   return null;
}

private CS
did_set_undofile(OptionChange* cha) {
   updateBoolRef(cha);
   // Only take action when the option was set.
   if (!cha->newVal.boole)
      return NULL;

    //When reset we do not delete the undo file, the option may be set again
    //without making any changes in between.
    Byte hash[UNDO_HASH_SIZE];
    Book* saveCurBook = curBook;

    FOR_ALL_BOOKS(curBook) {
      //When @undofile is set globally: for every buffer, otherwise only for the current buffer: 
      //Try to read in the undofile, if one exists, the buffer wasn't changed and the buffer was 
      //loaded
      if ((curBook == saveCurBook || (cha->setScope == SET_GLOBAL))
         && !doWasCurBookChanged() && curBook->mem.mfile != NULL
      ){
         u_compute_hash(OUT hash);
         u_read_undo(NULL, hash, curBook->currFileName);
      }
   }
   curBook = saveCurBook;

   return NULL;
}

// Process the new @undolevels option value.
private CS
did_set_undolevels(OptionChange* cha) {
   if (cha->newVal.num < 0) {
      return e_argument_must_not_be_negative;
   }
   updateNumRef(cha);
   return NULL;
}

//Process the new @wildchar / @wildcharm option value.
private CS
did_set_wildchar(OptionChange* cha){
   long new = cha->newVal.num;

   // Don't allow key values that wouldn't work as wildchar.
   if (new == Ctrl_C || new == '\n' || new == '\r' || new == K_KENTER)
      return e_invalid_argument;

   updateNumRef(cha);
   return NULL;
}

//Process the new @winheight or the @helpheight value.
private CS
setWinHeight(OptionChange* cha) {
   long new = cha->newVal.num;

   if (new < 1) {
      return e_argument_must_be_positive;
   }
   if (MIN_PORTAL_HEIGHT > new) {
      return e_winheight_cannot_be_smaller_than_winminheight;
   }

   updateNumRef(cha);
   // Change portal height NOW
   if (!ONLY_ONE_PORTAL && curPor->height < new)
      portSetHeight((int)new, curPor);

   return null;
}

//Process the new @winheight value
private CS
setHelpHeight(OptionChange* cha) {
   long new = cha->newVal.num;
   if (new < 0) {
      return e_argument_must_not_be_negative;
   }

   // Change portal height NOW
   if (!ONLY_ONE_PORTAL && curBook->kind == BOOK_HELP && curPor->height < new)
      portSetHeight((int)new, curPor);

   return null;
}

//Process the new @winwidth value.
private CS
did_set_winwidth(OptionChange* cha) {
   long new = cha->newVal.num;

   if (new < 1) {
      return e_argument_must_be_positive;
   }
   if (new < MIN_PORTAL_WIDTH) {
      return e_winwidth_cannot_be_smaller_than_winminwidth;
   }

   // Change portal width NOW
   updateNumRef(cha);
   if (!ONLY_ONE_PORTAL && curPor->width < new)
      portSetWidth((int)new, curPor);

   return null;
}

// Process the new @wlsteal option value.
private CS
did_set_wlsteal(OptionChange* cha) {
   updateBoolRef(cha);
   wayland_cb_reload();
   return NULL;
}

//Process the new @wltimeoutlen option value.
private CS
did_set_wltimeoutlen(OptionChange* cha) {
   if (cha->newVal.num < 0) {
      return e_argument_must_not_be_negative;
   }
   updateNumRef(cha);
   return NULL;
}

//Process the updated @wrap value.
private CS
did_set_wrap(OptionChange* cha) {
   // Set leftCol or skipCol to zero.
   if (cha->newVal.boole)
      curPor->leftCol = 0;
   else
      curPor->skipCol = 0;

   return NULL;
}

private CS
setTimeoutLen(OptionChange* cha) {
   if (cha->newVal.num < 0) return e_argument_must_be_positive;
   updateNumRef(cha);
   return null;
}

private CS
setHistory(OptionChange* cha) {
   long new = cha->newVal.num;
   if (new < 0) {
      return e_argument_must_be_positive;
   }  ei (new > 10000) {
      return e_invalid_argument;
   }
   updateNumRef(cha);
   return null;
}

private CS
setScrollJump(OptionChange* cha) {
   long new = cha->newVal.num;
   if (new == cha->oldVal.num)
      return null;
   if ((new < -100 || new >= visibleRowsG) && fullScreenG) {
      return e_invalid_scroll_size;
   }
   updateNumRef(cha);
   return null;
}

private CS
setScrollOff(OptionChange* cha) {
   if (cha->newVal.num < 0 && fullScreenG) {
      return e_argument_must_not_be_negative;
   }
   updateNumRef(cha);
   return null;
}

private CS
setSideScrollOff(OptionChange* cha) {
   if (cha->newVal.num < 0) {
      return e_argument_must_not_be_negative;
   }
   updateNumRef(cha);
   return null;
}

private CS
setStrictlyPositive(OptionChange* cha) {
   if (cha->newVal.num < 1) {
      return e_argument_must_be_positive;
   }
   updateNumRef(cha);
   return null;
}

private CS
setNonNegative(OptionChange* cha) {
   if (cha->newVal.num < 0) {
      return e_argument_must_not_be_negative;
   }
   updateNumRef(cha);
   return null;
}

// Call optChangeValue() and when an error is returned report it.
void
optChangeAndReportError(
   CS name,
   OptionValue newValue,
   SetScope setScope
){
   Option* o = findOption(name);
   if (!o) {
      emsg(_(e_option_not_supported));
      return;
   }
   CS errmsg = setImpl(o, newValue, setScope);
   if (errmsg)
      emsg(_(errmsg));
}

//Translate a string like "t_xx", "<t_XX>" or "<S-Tab>" to a key number.
//When "has_lt" is true there is a '<' before "*arg_arg". Return 0 when the key is not recognized.
private int
find_key_option(CS arg_arg, Boole has_lt) {
   int key = 0;
   CS arg = arg_arg;

   //Don't use get_special_key_code() for t_XX, we don't want it to call add_termcap_entry().
   if (arg[0] == 't' && arg[1] == '_' && arg[2] && arg[3]) {
      if (!has_lt || arg[4] == '>')
         key = TERMCAP2KEY(arg[2], arg[3]);
   } ei (has_lt) {
      --arg;             // put arg at the '<'
      Unt modifiers = 0;
      key = termFindSpecialKey(
            OUT &arg, OUT &modifiers, FSK_KEYCODE | FSK_KEEP_X_KEY | FSK_SIMPLIFY, NULL
      );
      if (modifiers != 0)          // can't handle modifiers here
         key = 0;
   }
   return key;
}

private void
printSingleOption(
      Option* o, SetScope setScope, Unt printFlags, ToPrint which, int run,
      OUT Arr(Option*) items, OUT Unt* item_count
) {
   OptionRef ref = getRefInScope(o, setScope);
   if (which != PRINT_CHANGED || (which == PRINT_CHANGED && !isOptionAtDefault(o, ref))) {
      int len;
      if ((printFlags & OPT_ONECOLUMN) != 0)
         len = visibleColsG;
      ei (o->defaultValue.tag == OPTION_BOOLE)
         len = 1;      // a toggle option fits always
      else {
         toString(o, setScope);
         len = (int)STRLEN(o->fullName) + eeglStrSize(nameBuffG) + 1;
      }
      if ((len <= INC - GAP && run == 0) || (len > INC - GAP && run == 1))
         items[*item_count++] = o;
   }
}

//Copy options from one portal to another. Used when portal splittin'
void
portCopyOptions(Portal* to, Portal* from) {
   copyPortOpt(&to->o, &from->o);
   afterCopyPortOpt(to);
}

//After copying portal options: update variables depending on options.
void
afterCopyPortOpt(Portal* po) {
   // Set leftCol or skipCol to zero.
   if (po->o.wrap)
      po->leftCol = 0;
   else
      po->skipCol = 0;
   checkBreakIndent(NULL, po);
}

private CS
copyOptionVal(OUT Sbuf* buf, CS val) {
   if (!val)
      return null;  // no need to allocate memory
   int len = STRLEN(val) + 1;
   CS valueInBuffer = buf->c + buf->len; 
   memcpy(valueInBuffer, val, len);
   buf->len += len;
   return valueInBuffer;
}

//Copy the options from one PortalOptions to another.
void
copyPortOpt(PortalOptions* t, PortalOptions* s) {
   Unt neededCap = s->stringOptions.cap;
   //If target buffer is big enouth enough, reuse it. Otherwise, free and allocate new one
   if (t->stringOptions.cap < neededCap) {
      free(t->stringOptions.c);
      t->stringOptions.c = malloc(neededCap);
      t->stringOptions.c[neededCap - 1] = ZERO;
      t->stringOptions.len = 0;
      t->stringOptions.cap = neededCap;
   }

#define OPTIONS_COPY
#define OPTIONS_LIST_PORTAL
#include "defoption.h"
#undef OPTIONS_LIST_PORTAL
#undef OPTIONS_COPY

   // Copy the script context so that we know where the value was last set.
   MEMMOVE(t->scriptLocs, s->scriptLocs, sizeof(t->scriptLocs));
}

// Free the allocated memory inside a PortalOptions.
void
optClearPortOptions(PortalOptions* t) {
   free(t->stringOptions.c);
}

private void
expand1(OUT Expand* xp, Option* o, CS argend) {
   //Now pick. If the option has a custom expander, use that. Otherwise, just
   //fill with the existing option value.
   if (expandOptionS && expandOptionS->expander) {
      xp->context = EXPAND_STRING_OPTION;
   } ei (xp->input.c[0] == ZERO) {
      xp->context = EXPAND_OLD_OPTION;
      return;
   } else
      xp->context = EXPAND_NOTHING;

   if (o->defaultValue.tag == OPTION_NUM)
      return;

   // Only string options below

   // Options that have P_EXPAND are considered to all use file/dir expansion.
   if ((o->flags & P_EXPAND) != 0) {
      xp->context = EXPAND_FILES;
      // for some options, we need three backslashes for a space
      if ((o->flags & P_EXPAND_3_BS) != 0)
         xp->backslash = XP_BS_THREE;
      else
         xp->backslash = XP_BS_ONE;
      if ((o->flags & P_COMMA) != 0)
         xp->backslash |= XP_BS_COMMA;
   } ei ((o->flags & P_EXPAND_DIR) != 0) {
      xp->context = EXPAND_DIRECTORIES;
      if ((o->flags & P_EXPAND_3_BS) != 0)
         xp->backslash = XP_BS_THREE;
      else
         xp->backslash = XP_BS_ONE;
      if ((o->flags & P_COMMA) != 0)
         xp->backslash |= XP_BS_COMMA;
   } 

   //For an option that is a list of file names, or comma/colon-separated
   //values, split it by the delimiter and find the start of the current
   //pattern, while accounting for backslash-escaped space/commas/colons.
   //Triple-backslashed escaped file names (e.g. @path) can also be delimited by space.
   if ((o->flags & (P_EXPAND|P_COMMA|P_COLON)) != 0) {
      for (CS p = argend - 1; p >= xp->input.c; --p) {
         // count number of backslashes before ' ' or ',' or ':'
         if (*p == ' ' || *p == ',' || (*p == ':' && (o->flags & P_COLON) != 0)) {
            CS backslashStart = p;
            while (backslashStart > xp->input.c && *(backslashStart - 1) == '\\')
               --backslashStart;
            if ((*p == ' ' && ((xp->backslash & XP_BS_THREE) && (p - backslashStart) < 3))
               || (*p == ',' && (o->flags & P_COMMA) != 0 && (p - backslashStart) < 2)
               || (*p == ':' && (o->flags & P_COLON) != 0)
            ) {
                xp->input = mbText(p + 1);
                break;
            }
         }
      }
   }

   //An option that is a list of single-character flags should always start
   //at the end as we don't complete words.
   if ((o->flags & P_FLAGLIST) > 0)
      xp->input = mbText(argend);

   return;
}

void
optInitExpandContextForSet(
   OUT Expand* xp,
   CS arg,
   SetScope setScope
){
   expandOptionScopeS = setScope;
   xp->context = EXPAND_OPTION;
   if (*arg == ZERO) {
      xp->input = mbText(arg);
      return;
   }
   CS argend = arg + STRLEN(arg);
   CS p = argend - 1;
   if (*p == ' ' && *(p - 1) != '\\') {
      xp->input = (Text){argend, 0};
      return;
   }
   
   while (p > arg) {
      CS backslashStart = p;
      // count number of backslashes before ' ' or ','
      if (*p == ' ' || *p == ',') {
         while (backslashStart > arg && *(backslashStart - 1) == '\\')
            --backslashStart;
      }
      // break at a space with an even number of backslashes
      if (*p == ' ' && ((p - backslashStart) & 1) == 0) {
         ++p;
         break;
      }
      --p;
   }
   arg = p;
   xp->input = mbText(arg);
   Unt nextchar;
   Option* o;
   // Allow `*` wildcard
   while (ASCII_ISALNUM(*p) || *p == '_' || *p == '*')
      p++;
   if (*p == ZERO)
      return;
   nextchar = *p;
   *p = ZERO;
   o = findOption(arg);
   *p = nextchar;
   if (!o) {
      xp->context = EXPAND_NOTHING;
      return;
   }
   if (o->defaultValue.tag == OPTION_BOOLE) {
      xp->context = EXPAND_NOTHING;
      return;
   }
   expandAppendS = false;
   if (nextchar != '=' && nextchar != ':') {
      xp->context = EXPAND_UNSUCCESSFUL;
      return;
   }

   //These are for handling expanding a specific option's value after the '=' or ':'
   expandOptionS = o;
   if ((o->flags & P_NO_CMD_EXPAND) != 0) {
      xp->context = EXPAND_UNSUCCESSFUL;
      return;
   }

   xp->input = mbText(p + 1);
   expandStartColS = (int)(p + 1 - xp->fullInput);

   //Certain options currently have special case handling to reuse the
   //expansion logic with other commands.
   if (eq(o->fullName, S"syntax")) {
      xp->context = EXPAND_OWNSYNTAX;
      return;
   } ei (eq(o->fullName, S"filetype")) {
      xp->context = EXPAND_FILETYPE;
      return;
   } 

   expand1(xp, o, argend);
}

//Expansion handler for :set= when we just want to fill in with the existing value.
int
optExpandOldOption(OUT ExpandMatch* matches) {
   matches->len = 0;

   CS var = NULL;

   if (expandOptionS) {
      // put string of option value in nameBuffG
      toString(expandOptionS, expandOptionScopeS);
      var = nameBuffG;
   }

   CS buffer = escape_option_str_cmdline(var);

   matches->c[0] = buffer;
   matches->len = 1;
   return OK;
}

//Return true if "ref" points to 'wildchar' or 'wildcharm' and it can be printed as a keyname.
//"*wcp" is set to the value of the option if it's 'wildchar' or 'wildcharm'.
private int
wildcharUseKeyname(OptionRef ref, long* wcp) {
   if (ref.tag == OPTION_NUM && (ref.num == &p_wc || ref.num == &p_wcm)) {
      *wcp = *ref.num;
   if (IS_SPECIAL(*wcp) || termFindSpecialKey_in_table((int)*wcp) >= 0)
      return true;
   }
   return false;
}

//Reset the flag indicating option "name" was set.
int
reset_optWasSet(CS name) {
   Option* o = findOption(name);
   if (!o)
      return FAIL;

   o->flags &= ~P_WAS_SET;
   return OK;
}

//Return the effective 'sidescrolloff' value for the current portal, using the
//global value when appropriate.
long
get_sidescrolloff_value(void) {
   return curPor->o.sideScrollOff;
}

//Set the callback function value for an option that accepts a function name,
//lambda, et al. (e.g. @operatorfunc, @tagfunc, etc.)
//Return OK if the option is successfully set to a function, otherwise return FAIL.
int
optSetCallback(OUT Callback* cb, CS new) {
   if (!new || *new == ZERO) {
      evFreeCallback(cb);
      return OK;
   }

   Var* tv;
   if (*new == '{' 
       || (STRNCMP(new, "function(", 9) == 0)
       || (STRNCMP(new, "funcref(", 8) == 0)
   )
      // Lambda expression or a funcref
      tv = eval_expr(new, NULL);
   else {
      // treat everything else as a function name string
      tv = allocStringVar(copyStr(new));
   }
   if (tv == NULL)
      return FAIL;

   Callback newCb = get_callback(tv);
   if (!newCb.name || *newCb.name == ZERO) {
      freeVar(tv);
      return FAIL;
   }

   evFreeCallback(cb);
   set_callback(cb, &newCb);
   if (newCb.needsFreeing)
      eeglFree(newCb.name);
   freeVar(tv);

   return OK;
}

//Process the new @showtabpanel value.
CS
setShowTabpanel(OptionChange* cha) {
   updateBoolRef(cha);
   shell_new_columns();
   return NULL;
}

private CS
setFormatListPat(OptionChange* cha) {
   updateStringRef(cha);
   //Changing format list pattern when breakindentopt includes the list setting: redraw
   if (curPor->breakIndent.list)
      redraw_all_later(UPD_NOT_VALID);
   return null; 
}

private CS (p_cfc_values[]) = {SMAP((CS), "keyword", "files", "whole_line")};
private CS
setCompletefuzzycollect(OptionChange* cha) {
   return validateAndSetListOfStrings(OUT cha, p_cfc_values);
}

private int
expandCompletefuzzycollect(OptExpand* args, OUT ExpandMatch *matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_cfc_values));
}

private CS
did_set_completeitemalign(OptionChange* cha) {
   if (!cha->newVal.string) {
      p_cia = 0;
      return null;
   } 
   
   Unt newCia = 0;
   int seen[3] = { false, false, false };
   int count = 0;
   Byte   buffer[10];
   CS p = cha->newVal.string;
   while (*p) {
      strCutPathFromListOfPaths(OUT &p, OUT buffer, sizeof(buffer), S",");
      if (count >= 3)
          return e_invalid_argument;

      if (STRCMP(buffer, "abbr") == 0) {
         if (seen[CPT_ABBR])
            return e_invalid_argument;
         newCia = newCia * 10 + CPT_ABBR;
         seen[CPT_ABBR] = true;
         count++;
      } ei (STRCMP(buffer, "kind") == 0) {
         if (seen[CPT_KIND])
            return e_invalid_argument;
         newCia = newCia * 10 + CPT_KIND;
         seen[CPT_KIND] = true;
         count++;
      } ei (STRCMP(buffer, "menu") == 0) {
         if (seen[CPT_MENU])
            return e_invalid_argument;
         newCia = newCia * 10 + CPT_MENU;
         seen[CPT_MENU] = true;
         count++;
      } else
         return e_invalid_argument;
   }
   if (newCia == 0 || count != 3)
      return e_invalid_argument;
      
   p_cia = newCia;
   return NULL;
}

private CS
did_set_completepopup(OptionChange* cha UNUSED) {
   if (parse_completepopup(NULL) == FAIL)
      return e_invalid_argument;

   popup_close_info();
   return NULL;
}

private CS p_debug_values[] = {SMAP((CS), "msg", "throw", "beep")};

private CS
did_set_debug(OptionChange* cha) {
   return validateAndSetListOfStrings(OUT cha, p_debug_values);
}

private int
expand_set_debug(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_debug_values));
}

private CS
did_set_diffanchors(OptionChange* cha) {
   if (diffanchors_changed(cha->newVal.string, cha->setScope == SET_LOCAL) == FAIL)
      return e_invalid_argument;
   updateStringRef(cha);
   return NULL;
}

private CS
setDiffopt(OptionChange* cha) {
   if (diffopt_changed(cha->newVal.string) == FAIL)
      return e_invalid_argument;
   return NULL;
}

// Note: Keep this in sync with diffopt_changed()
private CS p_dip_values[] = {SMAP((CS), 
   "filler", "anchor", "context:", "iblank", "icase", "iwhite", 
   "iwhiteall", "iwhiteeol", "horizontal", "vertical", "closeoff", "hiddenoff",  
   "followwrap", "internal", "indent-heuristic", "algorithm:", "inline:", "linematch:"
)};


private CS(p_dip_algorithm_values[]) = {SMAP((CS), 
   "myers", "minimal", "patience", "histogram"
)};

private CS(p_dip_inline_values[]) = {SMAP((CS), "none", "simple", "char", "word")};

private int
expandDiffopt(OptExpand* args, OUT ExpandMatch* matches) {
   Expand *xp = args->expand;

   if (xp->input.c > args->setArg && *(xp->input.c - 1) == ':') {
      // Within "algorithm:", we have a subgroup of possible options.
      int algo_len = sizeof("algorithm:") - 1;
      if (xp->input.c - args->setArg >= algo_len 
            && STRNCMP(xp->input.c - algo_len, "algorithm:", algo_len) == 0
      ) {
         return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_dip_algorithm_values));
      }
      // Within "inline:", we have a subgroup of possible options.
      int inline_len = sizeof("inline:") - 1;
      if (xp->input.c - args->setArg >= inline_len &&
         STRNCMP(xp->input.c - inline_len, "inline:", inline_len) == 0
      ) {
          return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_dip_inline_values));
      }
      return FAIL;
   }

   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_dip_values));
}

private CS(p_popup_option_align_values[]) = {S"item", S"menu"};

private CS(p_popup_option_border_values[]) = {S"on", S"off"};

// Note: Keep this in sync with portal.c:parse_popup_option()
private CS p_popup_option_values[] = {SMAP((CS), 
   "height:", "width:", "highlight:", "border:", "align:"
)};

private int
expand_set_popupoption(OptExpand* args, OUT ExpandMatch* matches) {
   Expand *xp = args->expand;

   if (xp->input.c > args->setArg && *(xp->input.c - 1) == ':') {
      // Within "highlight:"/"border:"/"align:", we have a subgroup of possible options.
      int border_len = (int)STRLEN("border:");
      if (xp->input.c - args->setArg >= border_len &&
         STRNCMP(xp->input.c - border_len, "border:", border_len) == 0
      ){
         return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_popup_option_border_values));
      }
      int align_len = (int)STRLEN("align:");
      if (xp->input.c - args->setArg >= align_len &&
         STRNCMP(xp->input.c - align_len, "align:", align_len) == 0
      ){
         return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_popup_option_align_values));
      }
      int highlight_len = (int)STRLEN("highlight:");
      if (xp->input.c - args->setArg >= highlight_len &&
         STRNCMP(xp->input.c - highlight_len, "highlight:", highlight_len) == 0
      ){
          // Return the list of all highlight names
          return optionCompletionExpand(OUT matches, args, &getHiliteGroupNameAsCString);
      }
      return FAIL;
   }

   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_popup_option_values));
}

private CS
setCursorInsert(OptionChange* cha) {
   Byte newShape = parseCursorShape(cha->newVal.string);
   if (newShape >= 3)
      return e_invalid_argument;
   cursorInsertG = newShape;
   return null; // success
}

private CS
setCursorNormal(OptionChange* cha UNUSED) {
   Byte newShape = parseCursorShape(cha->newVal.string);
   if (newShape >= 3)
      return e_invalid_argument;
   cursorNormalG = newShape;
   return null;
}

private int
expand_set_formatoptions(OptExpand* args, OUT ExpandMatch* matches) {
   return expand_set_opt_listflag(OUT matches, args, (Byte*)FO_ALL);
}

private CS
did_set_helplang(OptionChange* cha) {
   CS new = cha->newVal.string;
   if (!new) {
      p_hlg = null;
      return null;
   }
   
   // Check for "", "ab", "ab,cd", etc.
   for (Byte *s = new; *s != ZERO; s += 3) {
      if (s[1] == ZERO || ((s[2] != ',' || s[3] == ZERO) && s[2] != ZERO)) {
         return e_invalid_argument;
      }
      if (s[2] == ZERO)
         break;
   }

   p_hlg = new;
   return null;
}

//One of the '*expr' options is changed: @balloonexpr, @diffexpr, @foldexpr, @foldtext, 
//@formatexpr, @includeexpr, @indentexpr, @patchexpr or @printexpr.
private CS
setOptexpr(OptionChange* cha) {
   OptionRef ref = cha->ref;

   //If the option value starts with <SID> or s:, then replace that with the script identifier.
   CS name = get_scriptlocal_funcname(*ref.string);
   cha->newVal.string = name;
   updateStringRef(cha);

   return NULL;
}

private CS (p_ead_values[]) = {SMAP((CS), "both", "ver", "hor")};
private CS
setEadirection(OptionChange* cha) {
   Byte v = parseEnumValue(cha->newVal.string, p_ead_values);
   if (v == 255)
      return e_invalid_argument;

   p_ead = v;
   return NULL;
}

private int
expandEadirection(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_ead_values));
}

private CS
did_set_eventignore(OptionChange* cha) {
   OptionRef ref = cha->ref;
 
   if (check_ei(*ref.string) == FAIL)
      return e_invalid_argument;
   return NULL;
}

private CS
did_set_verbosefile(OptionChange* cha UNUSED) {
   verbose_stop();
   if (p_vfile && verbose_open() == FAIL)
      return e_invalid_argument;

   return NULL;
}

private CS
setEeglinfo(OptionChange* cha) {
   CS new = cha->newVal.string;
   if (!new) {
      p_eeglinfo = null;
      return null;
   }
   
   CS errmsg = NULL;

   for (CS s = new; *s != ZERO;) {
      // Check it's a valid character
      if (firstOccurrence(S"!\"%'/:<@cfhnrs", *s) == NULL) {
         errmsg = illegal_char(OUT &cha->errb, *s);
         break;
      }
      if (*s == 'n')   // name is always last one
          break;
      ei (*s == 'r') {// skip until next ','
          while (*++s && *s != ',')
             {}
      } ei (*s == '%') {
          // optional number
          while (eeIsDigit(*++s))
             {}
      } ei (*s == '!' || *s == 'h' || *s == 'c')
          ++s;      // no extra chars
      else {     // must have a number
         while (eeIsDigit(*++s))
             {}

         if (!EE_ISDIGIT(*(s - 1))) {
            if (cha->errb.c) {
               eeSnprintf(
                   cha->errb.c, cha->errb.len, _(e_missing_number_after_angle_str_angle),
                   bookTranscharByte(*(s - 1))
               );
               errmsg = cha->errb.c;
            } else
               errmsg = null;
            break;
         }
      }
      if (*s == ',')
         ++s;
      ei (*s) {
         if (cha->errb.c)
            errmsg = e_missing_comma;
         else
            errmsg = null;
         break;
      }
   }
   //The ' must be included if new value is non-zero
   if (new[0] != ZERO && !errmsg && get_eeglinfo_parameter('\'') < 0)
      errmsg = e_must_specify_a_value;
      
   if (!errmsg) 
      p_eeglinfo = new;

   return errmsg;
}

//The 'whichwrap' option is changed.
private CS
did_set_whichwrap(OptionChange* cha) {
   //Add ',' to the list flags because 'whichwrap' is a flag list that is comma-separated
   return did_set_option_listflag(*(cha->ref.string), (CS)(WW_ALL ","), OUT &cha->errb);
}

private int
expand_set_whichwrap(OptExpand* args, OUT ExpandMatch* matches) {
   return expand_set_opt_listflag(OUT matches, args, (CS)WW_ALL);
}

// Note: Keep this in sync with check_opt_wim()
private CS(p_wim_values[]) = {SMAP((CS), "full", "longest", "list", "lastused", "noselect")};

private CS
did_set_wildmode(OptionChange* cha UNUSED) {
   if (check_opt_wim() == FAIL)
      return e_invalid_argument;
   return NULL;
}

private int
expand_set_wildmode(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_wim_values));
}

private CS(p_wop_values[]) = {SMAP((CS), "fuzzy", "tagfile", "pum", "exacttext")};
private CS
setWildoptions(OptionChange* cha) {
   return validateAndSetListOfStrings(OUT cha, p_wop_values);
}

private int
expandWildoptions(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_wop_values));
}

private Boole expandEipS = false;

private int
expand_set_eventignore(OptExpand* args, OUT ExpandMatch* matches) {
   expandEipS = args->ref.string != &p_ei;
   return optionCompletionExpand(
       OUT matches,
       args,
       &get_eventignore_name
   );
}

private CS
did_set_foldexpr(OptionChange* cha) {
   (void)setOptexpr(cha);
   if (curPor->o.foldMethod == FOLD_EXPR)
      foldUpdateAll(curPor);
   return NULL;
}

private CS
did_set_foldignore(OptionChange* cha UNUSED) {
   if (curPor->o.foldMethod == FOLD_INDENT)
      foldUpdateAll(curPor);
   return NULL;
}

private CS
did_set_foldmarker(OptionChange* cha) {
   OptionRef ref = cha->ref;

   CS p = firstOccurrence(*ref.string, ',');
   if (!p)
      return e_comma_required;
   ei (p == *ref.string || p[1] == ZERO)
      return e_invalid_argument;
      
   if (curPor->o.foldMethod == FOLD_MARKER)
      foldUpdateAll(curPor);

   return NULL;
}

private CS p_fdm_values[] = {SMAP((CS), 
      "manual", "expr", "marker", "indent", "syntax", "diff"
)};

private CS
setFoldMethod(OptionChange* cha) {
   Byte v = parseEnumValue(cha->newVal.string, p_fdm_values);
   if (v == 255)
      return e_invalid_argument;

   *(cha->ref.enume) = v;
   foldUpdateAll(curPor);
   if (v == FOLD_DIFF)
      newFoldLevel();
   return NULL;
}

private int
expand_set_foldmethod(OptExpand* args, OUT ExpandMatch *matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_fdm_values));
}

// keep in sync with eegl.h:FDO_ flags
private CS(p_fdo_values[]) = {SMAP((CS), 
   "all", "block", "hor", "mark", "percent", "quickfix", "search", "tag", "insert", "undo", "jump"
)};

private CS
setFoldopen(OptionChange* cha) {
   return readOptionFlags(cha, p_fdo_values, &p_fdo);
}

private int
expandFoldopen(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_fdo_values));
}

private CS
did_set_formatoptions(OptionChange* cha) {
   return did_set_option_listflag(*(cha->ref.string), (CS)FO_ALL, OUT &cha->errb);
}

//The @isident or the @iskeyword or the @isprint or the @isfname options are changed.
private CS
setIsopt(OptionChange* cha) {
   //'isident', 'iskeyword' or 'isfname' option: refill g_chartab[]
   //If the new option is invalid, use old value.
   if (bookInitCharsForKeywordsForCurbook() == FAIL) {
      return e_invalid_argument;   // error in value
   }
   updateStringRef(cha);
   return NULL;
}

private CS
did_set_matchpairs(OptionChange* cha) {
   OptionRef ref = cha->ref;
   for (CS p = *ref.string; *p != ZERO; ++p) {
      int x2 = -1;
      int x3 = -1;

      p += utfCharLen(p);
      if (*p != ZERO)
         x2 = *p++;
      if (*p != ZERO) {
         x3 = mb_ptr2char(p);
         p += utfCharLen(p);
      }
      if (x2 != ':' || x3 == -1 || (*p != ZERO && *p != ','))
         return e_invalid_argument;
      if (*p == ZERO)
         break;
   }
   return NULL;
}

private CS (p_mopt_values[]) = {SMAP((CS), "hit-enter", "wait:", "history:")};

private CS
did_set_messagesopt(OptionChange* cha) {
   if (messagesopt_changed(cha->newVal.string) == FAIL)
      return e_invalid_argument;
   updateStringRef(cha);

   return NULL;
}

private int
expand_set_messagesopt(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_mopt_values));
}

#if defined(PROTO)

private CS
did_set_imactivatekey(OptionChange* cha UNUSED) {
   if (!im_xim_isvalid_imactivate())
   return e_invalid_argument;
   return NULL;
}
#endif

private CS
setExpandTriggers(OptionChange* cha) {
   Boole     lastWasStick = false;
   CS newVal = cha->newVal.string;

   for (CS p = newVal; *p != ZERO;) {
      if (*p == '|') {
         if (lastWasStick)
            return e_invalid_argument;
         lastWasStick = true;
         p++;
         continue;
      }

      lastWasStick = false;
      MB_PTR_ADV(p);
   }
   if (lastWasStick)
      return e_invalid_argument;

   return NULL;
}

private CS
did_set_iskeyword(OptionChange* cha) {
   return setIsopt(cha);
}

//The @statusline or the @rulerformat option is changed.
//"rulerformat" is true if the @rulerformat is changed.
private CS
parse_status_rulerformat(OptionChange* cha) {
   OptionRef ref = cha->ref;
   CS errmsg = NULL;
   int      wid;

   CS new = cha->newVal.string;
   if (new)   // reset rulerWidthG first
      rulerWidthG = 0;
   CS s = new;
   if (new && *s == '%') {
      // set rulerWidthG if 'ruf' starts with "%99("
      if (*++s == '-')   // ignore a '-'
         s++;
      wid = parseLong(&s);
      if (wid && *s == '(' && (errmsg = check_stl_option(new)) == NULL)
         rulerWidthG = wid;
      else {
         //Validate the flags in 'rulerformat' only if it doesn't point to a custom function 
         //("%!" flag).
         if ((*ref.string)[1] != '!')
            errmsg = check_stl_option(new);
      }
   }
   // check 'statusline' only if it doesn't start with "%!"
   ei (new || s[0] != '%' || s[1] != '!')
      errmsg = check_stl_option(s);
   if (new && !errmsg)
      computeColumnsForRulerAndCommand();
      
   if (!errmsg) 
      updateStringRef(cha); 

   return errmsg;
}

private CS
setRulerFormat(OptionChange* cha) {
   return parse_status_rulerformat(cha);
}

private CS
did_set_tabpanelopt(OptionChange* cha) {
   if (uiValidateTabpanelopt(cha->newVal.string) == FAIL)
      return e_invalid_argument;
   updateStringRef(cha);
   return NULL;
}

private CS (p_tplo_align_values[]) = {S"left", S"right"};

// Note: Keep this in sync with ui.c:tabpanelopt_changed()
private CS p_tplo_values[] = {SMAP((CS), "align:", "columns:", "vert")};

private int
expand_set_tabpanelopt(OptExpand* args, OUT ExpandMatch* matches) {
   Expand *xp = args->expand;
   if (xp->input.c > args->setArg && *(xp->input.c - 1) == ':') {
      // Within "align:", we have a subgroup of possible options.
      int align_len = (int)STRLEN("align:");
      if (xp->input.c - args->setArg >= align_len 
            && STRNCMP(xp->input.c - align_len, "align:", align_len) == 0
      ) {
         return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_tplo_align_values));
      }
      return FAIL;
   }

   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_tplo_values));
}

private CS p_scbopt_values[] = {SMAP((CS), "ver", "hor", "jump")};

// The @scrollopt option is changed.
private CS
setScrollopt(OptionChange* cha) {
   Unt new;
   CS errmsg = readOptionFlags(cha, p_scbopt_values, OUT &new);
   if (errmsg)
      return errmsg;
   p_sbo = new; 
   return null;
}

private int
expand_set_scrollopt(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_scbopt_values));
}

private CS
setWlseat(OptionChange* cha UNUSED) {
    //If there isn't any seat named 'wlseat', then let the Wayland clipboard be
    //unavailable. Ignore errors returned.
    wayland_cb_reload();

   return NULL;
}

private CS backupCopyValues[] = {SMAP((CS), 
   "yes", "auto", "no", "breaksymlink", "breakhardlink"
)};

private CS
did_set_showbreak(OptionChange* cha) {
   OptionRef ref = cha->ref;
   Byte   *s;

   for (s = *ref.string; *s; ) {
      if (bookPtr2Cells(s) != 1)
         return e_showbreak_contains_unprintable_or_wide_character;
      MB_PTR_ADV(s);
   }

   return NULL;
}

private CS p_sloc_values[] = {S"last", S"statusline"};

private CS
did_set_showcmdloc(OptionChange* cha) {
   Byte v = parseEnumValue(cha->newVal.string, p_sloc_values);
   if (v == 255)
      return e_invalid_argument;
   *(cha->ref.enume) = v;
   return null;
}

private int
expand_set_showcmdloc(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_sloc_values));
}

private CS
did_set_statusline(OptionChange* cha) {
   return parse_status_rulerformat(cha);
}

private CS(p_swb_values[]) = {SMAP((CS), 
   "useopen", "usetab", "split", "newtab", "vsplit", "uselast"
)};

private CS
setSwitchbook(OptionChange* cha) {
   return validateAndSetListOfStrings(OUT cha, p_swb_values);
}

private int
expand_set_switchbook(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_swb_values));
}

// Keep in sync with portal.c:TCL_ flags
private CS p_tcl_values[] = {S"left", S"uselast"};

private CS
setTabClose(OptionChange* cha) {
   Byte v = parseEnumValue(cha->newVal.string, p_tcl_values);
   if (v == 255)
      return e_invalid_argument;
   *(cha->ref.enume) = v;
   return null;
}

private int
expand_set_tabclose(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(p_tcl_values));
}

// Keep in sync with SWB_ flags
private CS tagCaseValues[] = {SMAP((CS), "followic", "ignore", "match", "followscs", "smart")};

private CS
setTagcase(OptionChange* cha) {
   Byte new = parseEnumValue(cha->newVal.string, tagCaseValues);
   if (new == 255)
      return e_invalid_argument;
   *(cha->ref.enume) = new; 
   return null;
}

private int
expand_set_tagcase(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(tagCaseValues));
}

private CS
setTerm(OptionChange* cha) {
   if (*cha->newVal.string == ZERO)
      return e_cannot_set_term_to_empty_string;
   if (set_termname(cha->newVal.string) == FAIL)
      return e_not_found_in_termcap;

   // Screen colors may have changed.
   redraw_later_clear();

   return NULL;
}

private CS
did_set_termwinkey(OptionChange* cha) {
   OptionRef ref = cha->ref;

   if ((*ref.string)[0] != ZERO && stringToChar(*ref.string, true) == 0)
      return e_invalid_argument;

   return NULL;
}

private CS
did_set_termwinsize(OptionChange* cha) {
   OptionRef ref = cha->ref;
   if ((*ref.string)[0] == ZERO)
      return NULL;

   CS p = skipdigits(*ref.string);
   if (p == *ref.string || (*p != 'x' && *p != '*') || *skipdigits(p + 1) != ZERO)
      return e_invalid_argument;

   return NULL;
}

private CS
setBufType(OptionChange* cha) {
   CS newVal = cha->newVal.string;
   Byte v = 255;
   for (Unt i = 0; i < p_buftypeValuesLen; i++) {
      if (eq(p_buftype_values[i], newVal)) {
         v = i;
         goto argumentIsValid;
      }
   }
   return e_invalid_argument;
   
argumentIsValid:
   *(cha->ref.enume) = v;
   curPor->statusLineNeedsRedraw = true;
   redraw_later(UPD_VALID);
   needRedrawTabpanelG = true;
   return NULL;
}

private int
expand_set_buftype(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, p_buftype_values, p_buftypeValuesLen);
}

private CS
setListChars(OptionChange* cha) {
   // only apply the global value to "curPor" when it does not have a local value
   ErrBuilder errb = {};
   CS errmsg = drawSetListChars(cha->newVal.string, OUT &errb);
   if (errmsg)
      return errmsg;
   else {
      updateStringRef(cha);
   }

   redraw_all_later(UPD_NOT_VALID);

   return NULL;
}

private CS
setFillChars(OptionChange* cha) {
   // only apply the global value to "curPor" when it does not have a local value
   ErrBuilder errb = {};
   CS errmsg = drawSetFillChars(cha->newVal.string, OUT &errb);
   if (errmsg)
      return errmsg;
   else {
      updateStringRef(cha);
   }

   redraw_all_later(UPD_NOT_VALID);

   return NULL;
}

//Expand @fillchars or @listchars option values.
private int
expand_set_chars_option(OptExpand* args, OUT ExpandMatch* matches) {
   OptionRef ref = args->ref;
   Boole is_lcs = ref.string == &p_lcs;
   return optionCompletionExpand(
       OUT matches,
       args,
       is_lcs ? get_listchars_name : get_fillchars_name
   );
}

private CS
did_set_comments(OptionChange* cha) {
   OptionRef ref = cha->ref;
   CS errmsg = NULL;

   for (CS s = *ref.string; *s; ) {
      while (*s && *s != ':') {
         if (firstOccurrence((CS)COM_ALL, *s) == NULL && !EE_ISDIGIT(*s) && *s != '-') {
            errmsg = illegal_char(OUT &cha->errb, *s);
            break;
         }
         ++s;
      }
      if (*s++ == ZERO)
          errmsg = e_missing_colon;
      ei (*s == ',' || *s == ZERO)
          errmsg = e_zero_length_string;
      if (errmsg)
          break;
      while (*s && *s != ',') {
         if (*s == '\\' && s[1] != ZERO)
            ++s;
         ++s;
      }
      s = skip_to_option_part(s);
   }

   return errmsg;
}

private CS
did_set_commentstring(OptionChange *cha) {
   OptionRef ref = cha->ref;

   if (**(ref.string) != ZERO && STRSTR(*ref.string, "%s") == NULL)
      return e_commentstring_must_be_empty_or_contain_str;
   updateStringRef(cha); 

   return NULL;
}

private CS
setBackupCopy(OptionChange* cha) {
   Unt newVal = 0;
   CS errmsg = readOptionFlags(cha, backupCopyValues, OUT &newVal);
   if (errmsg)
      return errmsg;

   if (cha->setScope == SET_LOCAL) {
      curBook->o.backupCopy = newVal;
   } 

   if ((int)((newVal & BKC_AUTO) != 0)
         + (int)((newVal & BKC_YES) != 0)
         + (int)((newVal & BKC_NO) != 0) != 1
   ){
      // Must have exactly one of "auto", "yes"  and "no".
      showErrFmtMsg(_(e_illegal_combination_of_flags_str), "backupcopy");
      return e_illegal_combination_of_flags_str;
   }

   *(cha->ref.flags) = newVal;
   return null;
}

private int
expand_set_backupcopy(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(backupCopyValues));
}

// Note: Keep this in sync with checkBreakIndent()
private CS brioptValues[] = {SMAP((CS), "shift:", "min:", "sbr", "list:", "column:")};

//Check "briopt" as @breakindentopt and update the members of "po".
//This is called when @breakindentopt is changed and when a portal is initialized.
//Return FAIL for failure, OK otherwise.
private int
checkBreakIndent(
   CS briopt,  // when NULL: use "po->o.breakIndent"
   Portal* po       // when NULL: only check "briopt"
){
   int bri_shift = 0;
   long bri_min = 20;
   int bri_sbr = false;
   int bri_list = 0;
   int bri_vcol = 0;

   CS p = briopt ? briopt : po->o.breakIndentOpt;
   if (!p) {
      return OK;
   }

   while (*p != ZERO) {
      // Note: Keep this in sync with p_briopt_values
      if (STRNCMP(p, brioptValues[0], 6) == 0
          && ((p[6] == '-' && EE_ISDIGIT(p[7])) || EE_ISDIGIT(p[6]))
      ) {
         p += 6;
         bri_shift = parseLong(&p);
      } ei (STRNCMP(p, brioptValues[1], 4) == 0 && EE_ISDIGIT(p[4])) {
         p += 4;
         bri_min = parseLong(&p);
      } ei (STRNCMP(p, brioptValues[2], 3) == 0) {
         p += 3;
         bri_sbr = true;
      } ei (STRNCMP(p, brioptValues[3], 5) == 0) {
         p += 5;
         bri_list = parseLong(&p);
      } ei (STRNCMP(p, brioptValues[4], 7) == 0) {
         p += 7;
         bri_vcol = parseLong(&p);
      }
      if (*p != ',' && *p != ZERO)
          return FAIL;
      if (*p == ',')
          ++p;
   }

   if (!po)
      return OK;

   po->breakIndent.shift = bri_shift;
   po->breakIndent.min   = bri_min;
   po->breakIndent.showBreak = bri_sbr;
   po->breakIndent.list  = bri_list;
   po->breakIndent.vcol  = bri_vcol;

   return OK;
}

//The @breakindentopt option is changed.
private CS
setBreakindentOpt(OptionChange* cha) {
   OptionRef ref = cha->ref;

   if (checkBreakIndent(*ref.string, ref.string == &curPor->o.breakIndentOpt ? curPor : NULL
       ) == FAIL
   )
      return e_invalid_argument;

   updateStringRef(cha);
   // list setting requires a redraw
   if (ref.string == &curPor->o.breakIndentOpt && curPor->breakIndent.list)
      redraw_all_later(UPD_NOT_VALID);

   return NULL;
}

private int
expandBreakindentOpt(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(brioptValues));
}

private CS
setComplete(OptionChange* cha) {
   OptionRef ref = cha->ref;
   Byte   *p, *t;
   Byte   buffer[LSIZE];
   CS bufferPtr;
   Byte   char_before = ZERO;
   int      escape;

   for (p = *ref.string; *p; ) {
      memset(buffer, 0, LSIZE);
      bufferPtr = buffer;
      escape = 0;

      // Extract substring while handling escaped commas
      while (*p && (*p != ',' || escape) && bufferPtr < (buffer + LSIZE - 1)) {
         if (*p == '\\' && *(p + 1) == ',') {
            escape = 1;  // Mark escape mode
            p++;         // Skip '\'
         } else {
            escape = 0;
            *bufferPtr++ = *p;
         }
         p++;
      }
      *bufferPtr = ZERO;

      if (firstOccurrence(S".wbuksid]tUFo", *buffer) == NULL)
         return illegal_char(OUT &cha->errb, *buffer);

      if (firstOccurrence(S"ksF", *buffer) == NULL && *(buffer + 1) != ZERO
            && *(buffer + 1) != '^')
         char_before = *buffer;
      else {
         // Test for a number after '^'
         if ((t = firstOccurrence(buffer, '^')) != NULL) {
            *t++ = ZERO;
            if (!*t)
                char_before = '^';
            else {
               for (; *t; t++) {
                  if (!eeIsDigit(*t)) {
                      char_before = '^';
                      break;
                  }
               }
            }
         }
      }
      if (char_before != ZERO)
         return illegal_char_after_chr(OUT &cha->errb, char_before);
      // Skip comma and spaces
      while (*p == ',' || *p == ' ')
         p++;
   }

   if (setCompletionCallbacks(cha) != OK)
      return illegal_char_after_chr(OUT &cha->errb, 'F');
   return NULL;
}

private int
expandComplete(OptExpand* args, ExpandMatch* matches) {
   static CS (completeValues[]) = {SMAP((CS),
      ".", "w", "b", "u", "k", "kspell", "s", "i", "d", "]", "t", "U", "F", "o"
   )};
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(completeValues));
}

//Keep in sync with eegl.h:COT_*
private CS (completeOptValues[]) = {SMAP((CS), "menu", "menuone", "longest", "preview", 
   "popup", "popuphidden", "noinsert", "noselect", "fuzzy", "nosort", "preinsert", "nearest"
)};

private CS
setCompleteopt(OptionChange* cha) {
   Unt new;
   CS errmsg = readOptionFlags(cha, completeOptValues, OUT &new);
   if (errmsg)
      return errmsg;
      
   *(cha->ref.flags) = new;
   return null;
}

private int
expandCompleteopt(OptExpand* args, OUT ExpandMatch* matches) {
   return expandFlagOption(OUT matches, args, CONST_ARRAY_ARG(completeOptValues));
}

//}}}
//{{{option definitions

private Option OPTIONS_GLOBAL[] = {
#define GLOBAL_OPTION_DEFS
#define OPTIONS_LIST_GLOBAL
#include "defoption.h"
#undef OPTIONS_LIST_GLOBAL
#undef GLOBAL_OPTION_DEFS
};

private Option OPTIONS_PORTAL[] = {
#define PORTAL_OPTION_DEFS
#define OPTIONS_LIST_PORTAL
#include "defoption.h"
#undef OPTIONS_LIST_PORTAL
#undef PORTAL_OPTION_DEFS
};

// Set portal options to their default values
void
initPortalOptions(PortalOptions* o) {
#define OPTIONS_INIT_PORTAL
#define OPTIONS_LIST_PORTAL
#include "defoption.h"
#undef OPTIONS_LIST_PORTAL
#undef OPTIONS_INIT_PORTAL

}

private Option OPTIONS_BOOK[] = {
#define BOOK_OPTION_DEFS
#define OPTIONS_LIST_BOOK
#include "defoption.h"
#undef OPTIONS_LIST_BOOK
#undef BOOK_OPTION_DEFS
};

//}}}
//{{{option indices and offsets

#include "indices/options.h"
//static NameIndex const NAME_INDICES[]
//static NameIndex const FIRST_LETTER_INDICES[27]
//static NameIndex const NAME_INDICES_TERMINAL[]

//It's a able of (name -> index) for all options, with names sorted ascending lexicografically.
//Indices are encoded:
//- if < 65536, then it's an index into OPTIONS_GLOBAL;
//- if < 2*65536, then ind - 65536 = an index into OPTIONS_PORTAL;
//- else ind - 2*65536 = an index into OPTIONS_BOOK.

//}}}
//{{{forward declarations

private void optSetStringDefault_esc(CS name, CS val, Boole escape);
private CS findUnchangedItemInCommaList(CS origVal, CS newVal, Unt newVallen, Ulong flags);
private int find_key_option(CS arg_arg, Boole has_lt);
private void printOptions(ToPrint which);
private OptionRef getRefInScope(Option *p, SetScope setScope);

private CS setStringImpl(
   OUT Option* o, CS oldVal, CS newVal, SetScope setScope
);

//}}}
//{{{other general functions

private Unt
calcDefaultStringValuesLen(Arr(Option) opts, Unt count) {
   Unt totalLen = 0;
   for (Option* o = opts; o < opts + count; o++) {
      if (o->defaultValue.tag == OPTION_STRING && (o->defaultValue.string)) {
         totalLen += (STRLEN(o->defaultValue.string) + 1); // +1 for the ZERO
      }
   }
   return totalLen;
}

private Unt
calcGlobalStringValuesLen() {
   Unt totalLen = 0;
   Option* o UNUSED;
   FOR_GLOBAL(o) {
      if (o->defaultValue.tag == OPTION_STRING && (*o->c.reference.string)) {
         totalLen += (STRLEN(*o->c.reference.string) + 1); // +1 for the ZERO
      }
   }
   return totalLen;
}

private Unt
calcLocalStringsLength(Arr(Option) opts, Unt count) {
   Unt totalLen = 0;
   for (Unt i = 0; i < count; i++) {
      Option* o = opts + i;
      if (o->defaultValue.tag == OPTION_STRING && o->c.local.val.string) {
         totalLen += (STRLEN(o->c.local.val.string) + 1); // +1 for the ZERO
      }
   }
   return totalLen;
}

//String defaults -> a global buffer
private void
copyDefaultsToGlobalStringValues(OUT Sbuf* bui, Arr(Option) opts, Unt count) {
   Unt totalLen = calcDefaultStringValuesLen(opts, count);
   Unt newCap = calcNewBufferCap(totalLen);
   *bui = sbuf(newCap);
   
   CS wr = bui->c;
   
   for (Option* o = opts; o < opts + count; o++) {
      if (o->defaultValue.tag == OPTION_STRING 
            && (o->flags & P_NODEFAULT) == 0 
            && (o->defaultValue.string)
      ) {
         Unt len = STRLEN(o->defaultValue.string) + 1; // +1 for the ZERO
         memcpy(wr, o->defaultValue.string, len);
         if ((o->flags & P_GLOBAL) > 0) {
            *(o->c.reference.string) = wr;
         } else {
            o->c.local.val.string = wr;
         }
         wr += len;
      }
   }
   bui->len = wr - bui->c;
}

//Invoked by code in defoption.h
private void
copyStringOptToBook(OUT CS wr, CS* old, OptionChange* cha) {
   if (*old && old != cha->ref.string) {
      Unt len = STRLEN(*old) + 1;
      memcpy(wr, *old, len);
      wr += len;
   }
}

void
updateStringRef(OptionChange* cha) {
   CS const new = cha->newVal.string;
   Unt newLen = STRLEN(new) + 1;
   
   if (newLen == 1) {
      *cha->ref.string = null;
      return;
   }
   
   Unt oldLen = STRLEN(cha->oldVal.string) + 1; //overwrite old value
   if (newLen <= oldLen) {
      memcpy(cha->oldVal.string, new, newLen);
      return;
   }
   
   if (cha->buf->len + newLen < cha->buf->cap) { //allocate new val at buffer's tail
      *cha->ref.string = new;
      CS wr = cha->buf->c + cha->buf->len;
      memcpy(wr, new, newLen);
      cha->buf->len += newLen;
      return;
   }
   
   //allocate new val at buffer's tail
   if (cha->setScope == SET_LOCAL) {
      // copy all local string opts except the old one into the buffer
      CS wr = cha->buf->c;
      *cha->ref.string = new;
      if (cha->buf == &curBook->o.stringOptions) {
      
#define COPY_STRINGS_TO_BOOK
#define OPTIONS_LIST_BOOK
#include "defoption.h"
#undef OPTIONS_LIST_BOOK
#undef COPY_STRINGS_TO_BOOK
      } else {
      
#define COPY_STRINGS_TO_PORTAL
#define OPTIONS_LIST_PORTAL
#include "defoption.h"
#undef OPTIONS_LIST_PORTAL
#undef COPY_STRINGS_TO_PORTAL

      }
      cha->buf->len = wr - cha->buf->c;
      memcpy(wr, new, newLen);
      cha->buf->len += newLen;
   } else {
      if (cha->buf == &globalStringOptionsG) {
         Unt totalLen = calcGlobalStringValuesLen() + newLen - oldLen;
         Unt newCap = calcNewBufferCap(totalLen);
         Sbuf buf = sbuf(newCap);
         
         CS wr = buf.c;
         Option* o UNUSED;
         FOR_GLOBAL(o) {
            if (*o->c.reference.string == cha->oldVal.string) {
               
            } ei (o->defaultValue.tag == OPTION_STRING) {
               Unt len = STRLEN(*o->c.reference.string) + 1; // +1 for the ZERO
               if (len > 1) {
                  memcpy(wr, *o->c.reference.string, len);
                  wr += len;
               }
            }
         }
         buf.len = wr - buf.c;
         eeglFree(cha->buf->c);
         *cha->buf = buf;
      } ei (cha->buf == &bookStringOptionsG) {
      } else { // &portalStringOptionsG
         Arr(Option) opts;
         Unt count;
         if (cha->buf == &bookStringOptionsG) {
            opts = OPTIONS_BOOK;
            count = OPTION_BOOK_COUNT;
         } else {
            opts = OPTIONS_PORTAL;
            count = OPTION_PORTAL_COUNT;
         }
         
         Unt totalLen = calcLocalStringsLength(opts, count) + newLen - oldLen;
         Unt newCap = calcNewBufferCap(totalLen);
         Sbuf buf = sbuf(newCap);
         CS wr = buf.c;
         
         for (Unt i = 0; i < count; i++) {
            Option* o = opts + i;
            if (o->c.local.val.string == cha->oldVal.string) {
               
            } ei (o->defaultValue.tag == OPTION_STRING) {
               Unt len = STRLEN(o->c.local.val.string) + 1; // +1 for the ZERO
               if (len > 1) {
                  memcpy(wr, o->c.local.val.string, len);
                  wr += len;
               }
            }
         }
         buf.len = wr - buf.c;
         eeglFree(cha->buf->c);
         *cha->buf = buf;
      }
   }
}

private void
updateBoolRef(OptionChange* cha) {
   *cha->ref.boole = cha->newVal.boole;
}

private void
updateNumRef(OptionChange* cha) {
   *cha->ref.num = cha->newVal.num;
}

//After setting various option values: recompute variables that depend on option values.
private void
didset_options(void) {
   //initialize the table for @iskeyword et.al.
   (void)bookInitCharsForKeywordsForCurbook();
   //zero out the table for @breakat.
   for (Unt i = 0; i < 256; i++)
      breakat_flags[i] = false;
   afterCopyPortOpt(curPor);
}

private void 
expandEnvVarsInDefault(Option* o) {
   if (o->defaultValue.tag == OPTION_STRING) {
      CS p = expandEnvVarsInStringOption(o, NULL);
      if (p) {
         *(o->c.reference.string) = p;
         o->defaultValue.string = p;
      }
   }
}

//Expand environment variables and things like "~" for the defaults. If 
//expandEnvVarsInStringOption() returns non-NULL, the variable is expanded. This can only happen 
//for non-indirect options. Also set the default to the expanded value, so ":set" doesn't list them.
private void
expandEnvVarsInDefaults(void) {
   FOR_GLOBAL(o) {
      expandEnvVarsInDefault(o);
   }
   FOR_PORTAL(o) {
      expandEnvVarsInDefault(o);
   }
   FOR_BOOK(o) {
      expandEnvVarsInDefault(o);
   }
}

//Initialize the options, first part.
//Called only once from main(), just after creating the first book.
void
optInit0() {
   langmap_init();
   
   Option* o UNUSED;
   FOR_BOOK(o) {
      o->flags |= P_BOOK;
   }
   FOR_PORTAL(o) {
      o->flags |= P_PORTAL;
   }

   set_init_default_backupskip();
   set_init_default_cdpath();

   setDefaultValuesForAllOptions(0);

#ifdef CLEAN_RUNTIMEPATH
   if (clean_arg)
      set_init_clean_rtp();
#endif

   //curBook->o.initialized = true;

   
   //Expand environment variables and things like "~" for the defaults.
   expandEnvVarsInDefaults();

   //Set the default for @helplang.
   set_helplang_default(get_mess_lang());
}

private void
printOptions(ToPrint which) {  // OPT_LOCAL and/or OPT_GLOBAL
   //Hilite title
   msg_puts_title(_("\n--- Global option values ---"));
   msg_puts_title(_("\n--- Local option values ---"));
   msg_puts_title(_("\n--- Options ---"));

   //Do the loop twice:
   //1. display the short items
   //2. display the long items (only strings and numbers)
   //When "printFlags" has OPT_ONECOLUMN, do everything in run 2.
   Option* displayGlobal[OPTION_GLOBAL_COUNT];
   Option* displayPortal[OPTION_PORTAL_COUNT];
   Option* displayBook[OPTION_BOOK_COUNT];
   for (Unt run = 0; run < 2 && !gotInterruptG; ++run) {
      printOptionGroup(displayGlobal, OPTIONS_GLOBAL, OPTION_GLOBAL_COUNT, which, PRINT_NONTERMINAL,
      run
      );
   }
   for (Unt run = 0; run < 2 && !gotInterruptG; ++run) {
      printOptionGroup(displayPortal, OPTIONS_PORTAL, OPTION_PORTAL_COUNT, which, PRINT_NONTERMINAL, 
      run
      );
   }
   for (Unt run = 0; run < 2 && !gotInterruptG; ++run) {
      printOptionGroup(displayBook, OPTIONS_BOOK, OPTION_BOOK_COUNT, which, PRINT_NONTERMINAL, 
      run
      );
   }
}

private int
put_setstring(
   FILE   *fd,
   CS cmd,
   CS name,
   OptionRef ref,
   Ulong   flags
) {
   CS buffer = NULL;
   CS  part = NULL;
   CS p;

   if (fprintf(fd, "%s %s=", cmd, name) < 0)
      return FAIL;
   if (!ref.string) {
      goto end;
   } 
   
   //For options, some characters have to be escaped with CTRL-V or backslash.
   //expand the option value, replace $HOME by ~
   if ((flags & P_EXPAND) != 0) {
      int  size = (int)STRLEN(*ref.string) + 1;

      //replace home directory in the whole option value into "buffer"
      buffer = alloc(size);
      home_replace(*ref.string, buffer, size, false);

      //If the option value is longer than MAXPATHL, we need to append each comma separated part 
      //of the option separately, so that it can be expanded when read back.
      if (size >= MAXPATHL && (flags & P_COMMA) != 0 && firstOccurrence(*ref.string, ',') != NULL) {
         part = alloc(size);

         // write line break to clear the option, e.g. ':set rtp='
         if (put_eol(fd) == FAIL)
             goto fail;

         p = buffer;
         while (*p != ZERO) {
            // for each comma separated option part, append value to the option, :set rtp+=value
            if (fprintf(fd, "%s %s+=", cmd, name) < 0)
               goto fail;
            (void)strCutPathFromListOfPaths(OUT &p, OUT part, size,  S",");
            if (put_escstr(fd, part, 2) == FAIL || put_eol(fd) == FAIL)
               goto fail;
         }
         eeglFree(buffer);
         eeglFree(part);
         return OK;
      }
      if (put_escstr(fd, buffer, 2) == FAIL) {
         eeglFree(buffer);
         return FAIL;
      }
      eeglFree(buffer);
   } ei (put_escstr(fd, *ref.string, 2) == FAIL)
      return FAIL;
       
end:
   if (put_eol(fd) < 0)
      return FAIL;
   return OK;
fail:
   eeglFree(buffer);
   eeglFree(part);
   return FAIL;
}

private int
put_setnum(FILE* fd, CS cmd, CS name, OptionRef ref) {
   long   wc;
   if (fprintf(fd, "%s %s=", cmd, name) < 0)
      return FAIL;
   if (wildcharUseKeyname(ref, &wc)) {
      // print 'wildchar' and 'wildcharm' as a key name
      if (fputs((char *)get_special_key_name((int)wc, 0), fd) < 0)
         return FAIL;
   } ei (fprintf(fd, "%ld", *ref.num) < 0)
      return FAIL;
   if (put_eol(fd) < 0)
      return FAIL;
   return OK;
}

private int
put_setbool(
   FILE   *fd,
   CS cmd,
   CS name,
   Boole value
) {
   if (fprintf(fd, "%s %s%s", cmd, value ? "" : "no", name) < 0 || put_eol(fd) < 0)
      return FAIL;
   return OK;
}

//Write modified options as ":set" commands to a file.
//
//There are three values for "optFlags":
//OPT_GLOBAL:         Write global option values and fresh values of
//           buffer-local options (used for start of a session
//           file).
//OPT_GLOBAL + OPT_LOCAL: Idem, add fresh values of portal-local options for
//           curPor (used for a vimrc file).
//OPT_LOCAL:         Write buffer-local option values for curBook, fresh
//           and local values for portal-local options of
//           curPor.  Local values are also written when at the
//           default value, because an autocommand may have set them when doing ":edit file" 
//           and the user has set them back at the default or fresh value.
//(fresh value = value used for a new buffer or portal for a local option).
//
//Return FAIL on error, OK otherwise.
int
writeOptionsAsSet(FILE *fd UNUSED) {
   Option   *p;

   //Terminal options are also not written. Do the loop over the options twice: once for 
   //options with the P_PRI_MKRC flag and once without.
   for (Unt pri = 1; pri < 2; --pri) {
      for (p = OPTIONS_GLOBAL; p < OPTIONS_GLOBAL + OPTION_GLOBAL_COUNT; p++) {
         if ((p->flags & P_NO_MKRC) == 0
            && ((pri == 1) == ((p->flags & P_PRI_MKRC) != 0))
         ){
            // Global values are only written when not at the default value.
            OptionRef ref = getRefInScope(p, SET_GLOBAL);
            if (isOptionAtDefault(p, ref))
               continue;

         }
      } 
   }
   return OK;
}

// Set the scriptPos for an option, taking care of setting the buffer- or portal-local value
private void
setScriptPos(Option* o, SetScope scope, ScriptPos scriptPos) {
   ScriptPos newScriptPos = scriptPos;
   newScriptPos.lineNr += SOURCING_LNUM;

   // Remember where the option was set.  For local options need to do that
   // in the buffer or portal structure.
   if (scope == SET_GLOBAL)
      o->scriptPos = newScriptPos;
   else { 
      if ((o->flags & P_BOOK) != 0)
         curBook->o.scriptLocs[o - OPTIONS_BOOK] = newScriptPos;
      else {
         curPor->o.scriptLocs[o - OPTIONS_PORTAL] = newScriptPos;
      }
   }
}

//Set all the options (except the terminal options) to their default value. 
//Also set the global value for local options.
private void
setDefaultValuesForAllOptions(SetScope setScope) {
   copyDefaultsToGlobalStringValues(OUT &globalStringOptionsG, OPTIONS_GLOBAL, OPTION_GLOBAL_COUNT);
   copyDefaultsToGlobalStringValues(OUT &bookStringOptionsG, OPTIONS_BOOK, OPTION_BOOK_COUNT);
   copyDefaultsToGlobalStringValues(OUT &portalStringOptionsG, OPTIONS_PORTAL, OPTION_PORTAL_COUNT);
   
   Option* o UNUSED;
   FOR_GLOBAL(o) {
      if (o->defaultValue.tag != OPTION_STRING && (o->flags & P_NODEFAULT) == 0) {
         setDefault(o, SET_GLOBAL);
      } 
   }
   FOR_PORTAL(o) {
      if (o->defaultValue.tag != OPTION_STRING && (o->flags & P_NODEFAULT) == 0) {
         setDefault(o, setScope);
      } 
   }
   FOR_BOOK(o) {
      if (o->defaultValue.tag != OPTION_STRING && (o->flags & P_NODEFAULT) == 0) {
         setDefault(o, setScope);
      } 
   }
   
   // The @scroll must be computed for all portals.
   Portal* port;
   Tab* t;
   FOR_ALL_TAB_PORTALS(t, port) {
      portComputeScroll(port);
   } 
}

//Set all portal-local and buffer-local options to the Eegl default.
//local-global options will use the global value.
//When "doBook" is false, don't set book-local options.
void
optSetLocalOptionsToDefault(Portal *wp, Boole doBook) {
   Portal* curPorSaved = curPor;

   curPor = wp;
   curBook = curPor->book;
   block_autocmds();

   Option* o UNUSED;
   FOR_PORTAL(o) {
      if ((o->flags & P_NODEFAULT) == 0 && !isOptionAtDefault(o, getRefInScope(o, OPT_LOCAL)))
         setDefault(o, SET_LOCAL);
   }
   
   if (doBook) { 
      FOR_BOOK(o) {
         if ((o->flags & P_NODEFAULT) == 0 && !isOptionAtDefault(o, getRefInScope(o, OPT_LOCAL)))
            setDefault(o, SET_LOCAL);
      }
   } 

   unblock_autocmds();
   curPor = curPorSaved;
   curBook = curPor->book;
}

// ":get". Print the value of an option
void
c_get(Invocation* invo) {
   SetScope scope;
   if (invo->id == C_get) {
      scope = SET_LOCAL;
   } ei (invo->id == C_getGlobal) {
      scope = SET_GLOBAL; 
   } 
      
   CS arg = invo->arg;
   Boole didShow = false;   // already showed one value
   if (*arg == ZERO) {
      printOptions(PRINT_NONTERMINAL);
      didShow = true;
      goto theend;
   }
   
   if (STRNCMP(arg, "all", 3) == 0 && !ASCII_ISALPHA(arg[3])) {
      //":get all"  show all options.
      //":get all&" set all options to their default value.
      arg += 3;
      printOptions(PRINT_NONTERMINAL);
      didShow = true;
      goto theend;
   }    
   
   Option* o;
   CS errmsg = tryFindOptionFromCommand(OUT &o, OUT &arg);
   if (errmsg) {
      emsg(errmsg);
      return;
   } 
   if ((o->flags & P_GLOBAL) != 0) {
      scope = SET_GLOBAL;
   }
   
   // print value
   if (didShow)
      msg_putchar('\n');    //cursor below last one
   else {
      gotoCommline(true);   //cursor at status line
      didShow = true;       //remember that we did a line
   }
   showoneopt(o, scope);
//   OptionRef ref = getRefInScope(o, scope);
//   if (p_verbose > 0) {
//      // Mention where the option was last set.
//      if (eqRef(ref, o->reference))
//         lastSetMsg(od.scriptPos);
//      ei ((int)od.scope & PV_PORTAL)
//         lastSetMsg(curPor->o.scriptLocs[od.scope & PV_ID_MASK]);
//      ei ((int)od.scope & PV_BUF)
//         lastSetMsg(curBook->o.scriptLocs[od.scope & PV_ID_MASK]);
//    }
   
   return;
   
theend:
   if (silentModeG && didShow) {
      // After displaying option values in silent mode.
      silentModeG = false;
      info_message = true;   // use mch_msg(), not mch_errmsg()
      msg_putchar('\n');
      cursor_on();      // msg_start() switches it off
      out_flush();
      silentModeG = true;
      info_message = false;   // use mch_msg(), not mch_errmsg()
   }
}


//Parse 'arg' for option settings. 'arg' may be IObuff, but only when no errors can be present and
//option does not need to be expanded with expandEnvVarsInStringOption(). "optFlags":
//0 for ":set"
//OPT_GLOBAL     for ":setglobal"
//OPT_LOCAL      for ":set"
//
//Return FAIL if an error is detected, OK otherwise.
void
c_set(Invocation* invo) {
   SetScope setScope = (invo->id == C_setglobal) ? SET_GLOBAL : SET_LOCAL;
   
   CS arg = invo->arg;
   Boole didShow = false;   // already showed one value

   //Byte errbuf[ERR_BUFLEN];
   CS startarg = arg;

   CS errmsg = parseAndSet(setScope, OUT &arg);

   if (errmsg) {
      int i = eeSnprintf(IObuff, IOSIZE, "%s", (CS)_(errmsg)) + 2;
      if (i + (arg - startarg) < IOSIZE) {
         //append the argument with the error
         STRCPY(IObuff + i - 2, ": ");
         MEMMOVE(IObuff + i, startarg, (arg - startarg));
         IObuff[i + (arg - startarg)] = ZERO;
      }
      // make sure all characters are printable
      trans_characters(IObuff, IOSIZE);

      ++no_wait_return;      // wait_return() done later
      emsg(IObuff);   // show error highlighted
      --no_wait_return;

      return;
   }

   arg = skipwhite(arg);

   if (silentModeG && didShow) {
      // After displaying option values in silent mode.
      silentModeG = false;
      info_message = true;   // use mch_msg(), not mch_errmsg()
      msg_putchar('\n');
      cursor_on();      // msg_start() switches it off
      out_flush();
      silentModeG = true;
      info_message = false;   // use mch_msg(), not mch_errmsg()
   }
}

//Find index for option 'arg'. Return null if not found.
private Option*
findOption(CS arg) {
   //Check for name starting with an illegal character.
   if (arg[0] < 'a' || arg[0] > 'z')
      return null;
   
   Short firstLetterInd = arg[0] - 'a';
   Short searchStart = FIRST_LETTER_INDICES[firstLetterInd];
   Short searchEnd = FIRST_LETTER_INDICES[firstLetterInd + 1];

   // match full name
   for (Short i = searchStart; i < searchEnd; i++) {
      if (eq(arg, NAME_INDICES[i].name)) {
         Unt complexInd = NAME_INDICES[i].index;
         if (complexInd <= SHORT) {
            return (Option*)(OPTIONS_GLOBAL + complexInd);
         } ei (complexInd < 2*(SHORT + 1)) {
            return (Option*)(OPTIONS_PORTAL + complexInd - (SHORT + 1));
         } else {
            return (Option*)(OPTIONS_BOOK + complexInd - 2*(SHORT + 1));
         }
      }
   }
   return null;
}

// Get portal- or book-local options.
Bag*
getBookOrPortOptions(Boole bufopt) {
   Bag* b = allocBag();
   Option* o UNUSED;
   if (bufopt) { // book-local
      FOR_BOOK(o) {
         if (o->defaultValue.tag == OPTION_STRING)
            bagAddString(b, o->fullName, o->c.local.val.string);
         ei (o->defaultValue.tag == OPTION_NUM)
            bagAddNumber(b, o->fullName, o->c.local.val.num);
         ei (o->defaultValue.tag == OPTION_BOOLE)
            bagAddNumber(b, o->fullName, o->c.local.val.boole);
      }
   } else { //portal-local
      FOR_PORTAL(o) {
         if (o->defaultValue.tag == OPTION_STRING)
            bagAddString(b, o->fullName, o->c.local.val.string);
         ei (o->defaultValue.tag == OPTION_NUM)
            bagAddNumber(b, o->fullName, o->c.local.val.num);
         ei (o->defaultValue.tag == OPTION_BOOLE)
            bagAddNumber(b, o->fullName, o->c.local.val.boole);
      }
   }
   return b;
}

//Get pointer to option value, depending on local or global scope.
//"scope" can be OPT_LOCAL or OPT_GLOBAL
private OptionRef
getRefInScope(Option* o, SetScope setScope) {
   if ((o->flags & P_GLOBAL) != 0){
      return OPTIONS_GLOBAL[o - OPTIONS_GLOBAL].c.reference;
   } 
   switch (setScope) {
   case SET_LOCAL: {
      void* offsetPtr = (o->flags & P_BOOK) != 0 
         ? ((void*)(&curBook->o) + o->c.local.offset)
         : ((void*)(&curPor->o) + o->c.local.offset);
      switch (o->defaultValue.tag) {
      case OPTION_NUM:  return refNum((long*)offsetPtr);
      case OPTION_ENUM: return refEnum((Byte*)offsetPtr);
      case OPTION_STRING: return refStr((CS*)offsetPtr);
      case OPTION_BOOLE: return refBoole((Boole*)offsetPtr);
      case OPTION_FLAGS: return refFlag((Unt*)offsetPtr);
      case OPTION_CALLBACK: return refCallback((Callback**)offsetPtr);
      }
      }
      break;
   case SET_GLOBAL: {
      switch (o->defaultValue.tag) {
      case OPTION_NUM:  return refNum(&o->c.local.val.num);
      case OPTION_ENUM: return refEnum(&o->c.local.val.enume);
      case OPTION_STRING: return refStr(&o->c.local.val.string);
      case OPTION_BOOLE: return refBoole(&o->c.local.val.boole);
      case OPTION_FLAGS: return refFlag(&o->c.local.val.flags);
      case OPTION_CALLBACK: return refCallback(&o->c.local.val.callback);
      }
      }
   }
   return (OptionRef){};
}

//Copy all book options from global to a specific book
private void
copyGlobalToBookImpl(OUT Book* book) {
   Unt totalLen = calcLocalStringsLength(OPTIONS_BOOK, OPTION_BOOK_COUNT);
   Unt newCap = calcNewBufferCap(totalLen);
   //Sbuf buf UNUSED = sbuf(newCap);
   BookOptions* t = &book->o;
   t->stringOptions = sbuf(newCap);

#define COPY_GLOBAL_TO_BOOK
#define OPTIONS_LIST_BOOK
#include "defoption.h"
#undef OPTIONS_LIST_BOOK
#undef COPY_GLOBAL_TO_BOOK

   for (Unt i = 0; i < OPTION_BOOK_COUNT; i++) {
      t->scriptLocs[i] = OPTIONS_BOOK[i].scriptPos;
   }
   
   
   inSetCustomCompletionCbForBook(book);
   inSetOmniCbForBook(book);
   inSetTagCbForBook(book);
}

//Free the memory for the callback options of a book.
void
optFreeBookCallbacks(Book* book){
   evFreeCallback(book->o.completeFn);
   evFreeCallback(book->o.omniFn);
   evFreeCallback(book->o.thesaurusFn);
   evFreeCallback(book->o.completeFn);
   evFreeCallback(book->o.tagFn);
   evFreeCallback(book->o.findFn);
}

//Copy global option values to local options for one book.
//Used when creating a new buffer and sometimes when entering a book.
//flags:
//BCO_ENTER   We will enter the book "book".
//BCO_ALWAYS  Always copy the options, but only set initialized when appropriate.
void
optsCopyToBook(OUT Book* book, Unt flags) {
   Boole shouldCopy = !book->o.initialized && (flags & BCO_ENTER) != 0;
   if (shouldCopy || (flags & BCO_ALWAYS) != 0) {
      // Always free the allocated callbacks.
      optFreeBookCallbacks(book);
      copyGlobalToBookImpl(OUT book);
   }
   
   //When the options should be copied (ignoring BCO_ALWAYS), set the
   //flag that indicates that the options have been initialized.
   if (shouldCopy)
      book->o.initialized = true;
}

int
optExpandOption(
   Expand* xp UNUSED,
   RegMatch* regmatch,
   CS fuzzystr,
   Boole canFuzzy,
   OUT ExpandMatch* matches
){
   int match;
   int ic = regmatch->rm_ic;   // remember the ignore-case flag
   Fuzzy fuzzy = {.c = null, .len = 0, .a = matches->a};

   Boole doFuzzy = canFuzzy && scrIsCommlineFuzzyCompletable(fuzzystr);

   regmatch->rm_ic = ic;
   
   static CS names[] = {S"all", S"termcap"};
   for (match = 0; match < 2; ++match) {
      (void)matchString(names[match], regmatch, matches, doFuzzy, fuzzystr, &fuzzy);
   }
   
   Option* o UNUSED;
   FOR_GLOBAL(o) {
      matchString(xp->fullInput, regmatch, matches, doFuzzy, fuzzystr, &fuzzy);
   }
   FOR_PORTAL(o) {
      matchString(xp->fullInput, regmatch, matches, doFuzzy, fuzzystr, &fuzzy);
   }
   FOR_BOOK(o) {
      matchString(xp->fullInput, regmatch, matches, doFuzzy, fuzzystr, &fuzzy);
   }
   
   if (matches->len == 0 && fuzzy.len == 0) {
      return OK;
   }

   if (doFuzzy && defuzz(OUT matches, fuzzy, false) == FAIL)
      return FAIL;

   return OK;
}

Boole
optImmutableMode() {
   return !(OPTIONS_BOOK[BOOK_modifiable].c.local.val.boole);
}

//}}}
//{{{option strings: Functions related to string options

private CS
illegal_char_after_chr(OUT ErrBuilder* errb, int c) {
   if (!errb->c)
      return null;
   eeSnprintf(errb->c, errb->len, _(e_illegal_character_after_chr), c);
   return errb->c;
}

//Set a string option to a new value (without checking the effect).
//The string is copied into allocated memory.
//When "set_sid" is 0, set the scriptID to scriptPosG.sid. 
//When "set_sid" is SID_NONE don't set the scriptID. Otherwise set the scriptID to "set_sid".
private void
changeStringOptionDirectImpl(Option* o, CS val, SetScope scope, ScriptId setSid) {
   OptionRef ref = getRefInScope(o, scope);
   if (val) {
      *ref.string = copyStr(val);
   } else {
      *ref.string = null;
   }

   if (setSid != SID_NONE) {
      ScriptPos scriptPos;

      if (setSid == 0)
         scriptPos = scriptPosG;
      else {
         scriptPos.sid = setSid;
         scriptPos.seq = 0;
         scriptPos.lineNr = 0;
      }
      setScriptPos(o, scope, scriptPos);
   }
}

//Set a string option to a new value (without checking the effect).
//The string is copied into allocated memory.
//When "set_sid" is 0, set the scriptID to scriptPosG.sid. 
//When "set_sid" is SID_NONE don't set the scriptID. Otherwise set the scriptID to "set_sid".
void
optChangeStringOptionDirect(
   CS name,
   CS val,
   SetScope scope,
   ScriptId set_sid
) {
   Option* o = findOption(name);

   if (o) {
      changeStringOptionDirectImpl(o, val, scope, set_sid);
   } else { //not found (should never happen)
      showErrFmtMsg(_(e_internal_error_str), "optChangeStringOptionDirect()");
      internalErrFmtMsg("For option %s", name);
   }
}

//Like optChangeStringOptionDirect(), but for a portal-local option in "wp".
//Block autocommands to avoid the old curPor becoming invalid.
void
optSetStringOptionDirectInPort(
   OUT Portal* po,
   CS name,
   CS val,
   Unt optFlags,
   int set_sid
) {
   Portal   *save_curPor = curPor;

   block_autocmds();
   curPor = po;
   curBook = curPor->book;
   optChangeStringOptionDirect(name, val, optFlags, set_sid);
   curPor = save_curPor;
   curBook = curPor->book;
   unblock_autocmds();
}

//Like optChangeStringOptionDirect(), but for a book-local option in "book".
//Block autocommands to avoid the old curBook becoming invalid.
void
optSetStringOptionDirectInBook(
   Book* book,
   CS name,
   CS val,
   Unt optFlags,
   int set_sid
) {
   Book   *save_curBook = curBook;
   block_autocmds();
   curBook = book;
   curPor->book = curBook;
   optChangeStringOptionDirect(name, val, optFlags, set_sid);
   curBook = save_curBook;
   curPor->book = curBook;
   unblock_autocmds();
}

private CS set_opt_callback_orig_option = NULL;
private Byte *((*set_opt_callback_func)(Expand *, int));

//Callback used by optionCompletionExpand to also include the original value as the first item.
private CS
optionCompletionExpand_cb(Expand *xp, int idx) {
   if (idx == 0) {
      if (set_opt_callback_orig_option)
         return set_opt_callback_orig_option;
      else
         return S""; // empty strings are ignored
   }
   return set_opt_callback_func(xp, idx - 1);
}

//Expand an option with a callback that iterates through a list of possible names using an index.
private int
optionCompletionExpand(OUT ExpandMatch* matches, OptExpand* args, CS ((*func)(Expand *, int))) {
   set_opt_callback_orig_option = args->includeOrigVal ? args->origValue.string : NULL;
   set_opt_callback_func = func;

   int ret = expandGeneric(
       S"", // not using fuzzy as currently EXPAND_STRING_OPTION doesn't use it
       args->expand,
       args->oe_regmatch,
       optionCompletionExpand_cb,
       false,
       OUT matches
   );

   set_opt_callback_orig_option = NULL;
   set_opt_callback_func = NULL;
   return ret;
}

private CS
get_eventignore_name(Expand *xp, int idx) {
   int subtract = *xp->input.c == '-';
   // 'eventignore(win)' allows special keyword "all" in addition to all event names.
   if (!subtract && idx == 0)
      return S"all";

   CS name = get_event_name_no_group(xp, idx - 1 + subtract, expandEipS);
   if (!name)
      return NULL;

   SPRINTF(IObuff, "%s%s", subtract ? "-" : "", name);
   return IObuff;
}

//When the @spelllang option is set, source the spell/LANG.vim file in @runtimepath
private void
do_spelllang_source(void) {
   Byte fname[200];
   CS p;
   CS q = curPor->ownSyntax->spellLang;

   // Skip the first name if it is "cjk".
   if (STRNCMP(q, "cjk,", 4) == 0)
      q += 4;

   // They could set 'spellcapcheck' depending on the language.  Use the first
   // name in 'spelllang' up to '_region' or '.encoding'.
   for (p = q; *p != ZERO; ++p) {
      if (!ASCII_ISALNUM(*p) && *p != '-')
         break;
   } 
   if (p > q) {
      eeSnprintf(fname, 200, "spell/%.*s.vim", (int)(p - q), q);
      source_runtime(fname, DIP_ALL);
   }
}

//}}}
//{{{ locale: functions for language/locale configuration

# define HAVE_GET_LOCALE_VAL
private CS
get_locale_val(int what) {
   // Obtain the locale value from the libraries.
   CS loc = (CS)setlocale(what, NULL);
   return loc;
}

//Return true when "lang" starts with a valid language name.
//Reject NULL, empty string, "C", "C.UTF-8" and others.
private int
is_valid_mess_lang(Byte *lang) {
    return lang && ASCII_ISALPHA(lang[0]) && ASCII_ISALPHA(lang[1]);
}

//Obtain the current messages language.  Used to set the default for @helplang. 
//May return NULL or an empty string.
CS
get_mess_lang(void) {
   CS p;

#ifdef HAVE_GET_LOCALE_VAL
# if defined(LC_MESSAGES)
   p = get_locale_val(LC_MESSAGES);
# else
   // LC_COLLATE is the best guess, LC_TIME
   // and LC_MONETARY may be set differently for a Japanese working in the US.
   p = get_locale_val(LC_COLLATE);
# endif
#else
   p = mch_getenv(S"LC_ALL");
   if (!is_valid_mess_lang(p)) {
      p = mch_getenv(S"LC_MESSAGES");
      if (!is_valid_mess_lang(p))
         p = mch_getenv(S"LANG");
   }
#endif
    return is_valid_mess_lang(p) ? p : NULL;
}

// Complicated #if; matches with where get_mess_env() is used below.
#if (!(defined(LC_MESSAGES))) || (!defined(LC_MESSAGES))
   
// Get the language used for messages from the environment.
private CS
get_mess_env(void) {
   CS p = mch_getenv((CS)"LC_ALL");
   if (p && *p != ZERO)
      return p;

   p = mch_getenv((CS)"LC_MESSAGES");
   if (p && *p != ZERO)
      return p;

   p = mch_getenv((CS)"LANG");
   if (p && EE_ISDIGIT(*p))
      p = NULL;      // ignore something like "1043"
# ifdef HAVE_GET_LOCALE_VAL
    if (p == NULL || *p == ZERO)
   p = get_locale_val(LC_CTYPE);
# endif
    return p;
}
#endif


//Set the "v:lang" variable according to the current locale setting.
//Also do "v:lc_time"and "v:ctype".
void
set_lang_var(void) {
    CS loc;

# ifdef HAVE_GET_LOCALE_VAL
    loc = get_locale_val(LC_CTYPE);
# else
    // setlocale() not supported: use the default value
    loc = (CS)"C";
# endif
    set_EeglVar_string(VV_CTYPE, loc, -1);

    // When LC_MESSAGES isn't defined use the value from $LC_MESSAGES, fall
    // back to LC_CTYPE if it's empty.
# if defined(HAVE_GET_LOCALE_VAL) && defined(LC_MESSAGES)
    loc = get_locale_val(LC_MESSAGES);
# else
    loc = get_mess_env();
# endif
    set_EeglVar_string(VV_LANG, loc, -1);

# ifdef HAVE_GET_LOCALE_VAL
    loc = get_locale_val(LC_TIME);
# endif
    set_EeglVar_string(VV_LC_TIME, loc, -1);

# ifdef HAVE_GET_LOCALE_VAL
    loc = get_locale_val(LC_COLLATE);
# else
    // setlocale() not supported: use the default value
    loc = (CS)"C";
# endif
    set_EeglVar_string(VV_COLLATE, loc, -1);
}

//Setup to use the current locale (for ctype() and many other things).
void
init_locale(void) {
   setlocale(LC_ALL, "");

# if defined(LC_NUMERIC)
   // Make sure strtod() uses a decimal point, not a comma.
   setlocale(LC_NUMERIC, "C");
# endif

   {
   int   mustfree = false;

   // doExpandEnv() doesn't work yet, because g_chartab[] is not
   // initialized yet, call eeglGetEnv() directly
   CS p = eeglGetEnv((CS)"EEGLRUNTIME");
   if (p && *p != ZERO) {
      eeSnprintf(nameBuffG, MAXPATHL, "%s/lang", p);
      BINDTEXTDOMAIN(EEGLPACKAGE, nameBuffG);
   }
   if (mustfree)
      eeglFree(p);
   textdomain(EEGLPACKAGE);
   }
}

//":language":  Set the language (locale).
void
c_language(Invocation* invo) {
   char* loc;
   CS name;
   int      what = LC_ALL;
   CS whatstr = S"";
# ifdef LC_MESSAGES
#  define EE_LC_MESSAGES LC_MESSAGES
# else
#  define EE_LC_MESSAGES 6789
# endif

   name = invo->arg;

   // Check for "messages {name}", "ctype {name}" or "time {name}" argument.
   // Allow abbreviation, but require at least 3 characters to avoid
   // confusion with a two letter language name "me" or "ct".
   CS p = skiptowhite(invo->arg);
   if ((*p == ZERO || SPACE_OR_TAB(*p)) && p - invo->arg >= 3) {
      if (STRNICMP(invo->arg, "messages", p - invo->arg) == 0) {
          what = EE_LC_MESSAGES;
          name = skipwhite(p);
          whatstr = S"messages ";
      } ei (STRNICMP(invo->arg, "ctype", p - invo->arg) == 0) {
          what = LC_CTYPE;
          name = skipwhite(p);
          whatstr = S"ctype ";
      } ei (STRNICMP(invo->arg, "time", p - invo->arg) == 0) {
          what = LC_TIME;
          name = skipwhite(p);
          whatstr = S"time ";
      } ei (STRNICMP(invo->arg, "collate", p - invo->arg) == 0) {
          what = LC_COLLATE;
          name = skipwhite(p);
          whatstr = S"collate ";
      }
   }

   if (*name == ZERO) {
# ifndef LC_MESSAGES
      if (what == EE_LC_MESSAGES)
          p = get_mess_env();
      else
# endif
          p = (CS)setlocale(what, NULL);
      if (p == NULL || *p == ZERO)
          p = (CS)"Unknown";
      smsg(_("Current %slanguage: \"%s\""), whatstr, p);
   } else {
# ifndef LC_MESSAGES
      if (what == EE_LC_MESSAGES)
         loc = "";
      else
# endif
      {
          loc = setlocale(what, (char *)name);
# if defined(LC_NUMERIC)
          // Make sure strtod() uses a decimal point, not a comma.
          setlocale(LC_NUMERIC, "C");
# endif
      }
      if (!loc)
          showErrFmtMsg(_(e_cannot_set_language_to_str), name);
      else {
          // Need to do this for GNU gettext, otherwise cached translations will be used again.
          extern int _nl_msg_cat_cntr;

          ++_nl_msg_cat_cntr;
          // Reset $LC_ALL, otherwise it would overrule everything.
          eeSetenv(S"LC_ALL", S"");

          if (what != LC_TIME && what != LC_COLLATE) {
            //Tell gettext() what to translate to. It apparently doesn't
            //use the currently effective locale.
            if (what == LC_ALL) {
               eeSetenv(S"LANG", name);

               // Clear $LANGUAGE because GNU gettext uses it.
               eeSetenv(S"LANGUAGE", (CS)"");
            }
            if (what != LC_CTYPE) {
               Byte   *mname;
               mname = name;
               eeSetenv(S"LC_MESSAGES", mname);
               set_helplang_default(mname);
            }
         }

         // Set v:lang, v:lc_time, v:collate and v:ctype to the final result.
         set_lang_var();
      }
   }
}

private Arr(CS) locales = NULL;   // Array of all available locales
private Unt countLocales = 0;
private Boole did_init_locales = false;

//Return an array of strings for all available locales + NULL for the
//last element.  Return NULL in case of error.
private Arr(CS)
getLocalesFromEnv(OUT Unt* countOfLocales) {
   //Find all available locales by running command "locale -a".  If this
   //doesn't work we won't have completion.
   CS localeList = get_cmd_output((CS)"locale -a", NULL, SHELL_SILENT, NULL);
   if (!localeList)
      return NULL;

   //Transform locale_list string where each locale is separated by "\n"
   //into an array of locale strings.
   Arr(CS) locales = splitByCharIntoArray(localeList, '\n', OUT countOfLocales);

   eeglFree(localeList);
   
   return locales;
}

//Lazy initialization of all available locales.
private void
init_locales(void) {
   if (did_init_locales)
      return;

   locales = getLocalesFromEnv(OUT &countLocales);
   did_init_locales = true;
}

# if defined(EXITFREE) || defined(PROTO)
void
free_locales(void) {
   if (!locales)
      return;

   for (Unt i = 0; i < countLocales; i++)
      eeglFree(locales[i]);
   EE_CLEAR(locales);
   countLocales = 0;
}
# endif

//Function given to expandGeneric() to obtain the possible arguments of the ":language" command.
CS
get_lang_arg(Expand* xp UNUSED, int idx) {
   switch (idx) {
   case 0: return S"messages";
   case 1: return S"ctype";
   case 2: return S"time";
   case 3: return S"collate";
   }

   init_locales();
   return locales ? locales[idx - 4] : null;
}

//Function given to expandGeneric() to obtain the available locales.
CS
get_locales(Expand* xp UNUSED, int idx) {
   init_locales();
   return locales ? locales[idx] : null;
}

//}}}

