//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## hilite.c: hiliting text

#include "eegl.h"

//{{{Hilite groups

//Information about a hilite group. The ID of a hilite group is also called group ID.
//This is module-private info, the publically usable part is written to decorationsG.

typedef struct {
   Unt hiId;
   Text name;
   VTermDeco flags;   //flag of text decoration combo (bold, underline etc)
   Byte fieldPresence; // HI_* flags
   VTermColor fg; // foreground color
   VTermColor bg; // background color
   VTermColor under; // underline color
    
   int link;   // link to this hilite group ID
   int deflink;   // default link; restored in clearHiliteWorker()
   ScriptPos deflink_sctx; // script where the default link was set
   ScriptPos script_ctx;   // script in which the group was last set
} HiliteGroup;

// All possible keys, used for parsing
typedef enum {
   BG,
   FG,
   UNDER,
   DECO,
   LINK,
   KEY_PARSE_ERROR
} HiliteKey;

typedef struct {
   int nameStart; // index into "colorsText"
   int nameLen;
   VTermColor value;
} BuiltinColor;


//Parsed single names like the hilite group name or "clear"
typedef struct {
   Short start;
   Short end;
} HiKey;

private Text
keyName(HiKey kv, CS s) {
   return (Text){.c = s + kv.start, .len = kv.end - kv.start};
}

// Parsed key-value pairs like "fg=blue"
typedef struct {
   Short start;
   Short keyEnd; // position of the "=". The value starts at (keyEnd + 1)
   Short end;
} HiKeyValue;

//{{{forward decls

private Boole printHiliteHeaderWorker(int didHeader, int lineLen, HiliteGroup* group);
private void printHilite(HiliteGroup* g);
private void clearHiliteWorker(HiliteGroup* g);
private void printHiliteHeaderNew(HiliteGroup* group);
private void printHiliteDeco(HiliteGroup* group);
private void set_normal_colors(void);
private Short hiResolveLinks(Short hiId);

//}}}

private Text
keyOf(HiKeyValue kv, CS s) {
   return (Text){.c = s + kv.start, .len = kv.keyEnd - kv.start};
}

private Text
valueOf(HiKeyValue kv, CS s) {
   return (Text){.c = s + kv.keyEnd + 1, .len = kv.end - kv.keyEnd - 1};
}

private Boole
sliceCmpToConst0(Text a, Arr(char) b, Unt len) {
   return a.len == len && memcmp(a.c, b, len) == 0;
}
#define sliceCmpToConst(text, literal) sliceCmpToConst0(text, literal, sizeof(literal) - 1)

//{{{base definitions (colors, decoration types etc)

#define MAX_SYN_NAME   32

// must be sorted by the 'value' field because it is used by bsearch()!
private Kv decoKinds[] = {
   KEYVALUE_ENTRY(DECO_BOLD, "bold"),           
   KEYVALUE_ENTRY(DECO_INVERSE, "inverse"),     
   KEYVALUE_ENTRY(DECO_ITALIC, "italic"),       
   KEYVALUE_ENTRY(DECO_NOCOMBINE, "nocombine"), 
   KEYVALUE_ENTRY(DECO_NONE, "NONE"),         
   KEYVALUE_ENTRY(DECO_UNDERCURL, "undercurl"), 
   KEYVALUE_ENTRY(DECO_UNDERLINE, "underline")  
};

// this table is used to display hilite names in the correct order. keep in sync with decoKinds[]
private Kv *decoKindIndices[] = {
    decoKinds,       // DECO_BOLD
    decoKinds + 6,   // DECO_UNDERLINE
    decoKinds + 5,   // DECO_UNDERCURL
    decoKinds + 2,   // DECO_ITALIC
    decoKinds + 1,   // DECO_INVERSE
    decoKinds + 3,   // DECO_NOCOMBINE
    decoKinds + 4    // DECO_NONE
};

// length of all decoKinds names, plus commas, together (and a bit more)
#define MAX_DECO_LEN 60

#define COMBINE_DECORATIONS(d0, d1) ((((d1) & HL_NOCOMBINE) ? (d1) : (d0)) | (d1))

enum {
    BLACK = 0,
    DARKBLUE,
    DARKGREEN,
    DARKCYAN,
    DARKRED,
    DARKMAGENTA,
    BROWN,
    DARKYELLOW,
    GRAY,
    GREY,
    LIGHTGRAY,
    LIGHTGREY,
    DARKGRAY,
    DARKGREY,
    BLUE,
    LIGHTBLUE,
    GREEN,
    LIGHTGREEN,
    CYAN,
    LIGHTCYAN,
    RED,
    LIGHTRED,
    MAGENTA,
    LIGHTMAGENTA,
    YELLOW,
    LIGHTYELLOW,
    WHITE,
    NONE
};

// The hilite groups. Keep in sync with the HLF_* constants
private char *(hiliteGroupStrings[]) = {
   "None fg=regular7 bg=regular0", //0
   "NonText deco=bold fg=regular4",
   "NormalFloat link=None",
   "InvisAtEndOfScreen link=None", // HLF_AT chars at end of screen, chars that don't really exist in text 
   "Directories link=None",        //HLF_D directories in CTRL-D listing
   "ErrorMsg bg=regular1 fg=regular7", //HLF_E  error messages
   "WarningMsg link=None",         // HLF_W       warning messages
   "IncrementalSearch deco=inverse", //HLF_I incremental search
   "PrevSearch link=None",         //HLF_L  last search string
   "PrevSearchUnderCursor link=None", //  HLF_LC   last search string under cursor
   "MoreMsg link=None",            // 10 HLF_M    "--More--" message
   "ModeName deco=bold",           // HLF_CM    Mode (e.g., "-- INSERT --")
   "CurrentLineNr link=None",      // HLF_CLN   current line number
   "CurrentSign link=None",        // HLF_CLS   current line sign column
   "CurrentFold link=None",        // HLF_CLF   current line fold
   "YesNoQuestions link=None",     // HLF_R     return to continue message and yes/no questions
   "StatusLine deco=inverse",      // HLF_S  status lines
   "StatusLinesInactive deco=inverse", // HLF_SNC    status lines of not-current portals
   "VertSplit deco=inverse",       // HLF_C    column to separate vertically split portals
   "OutputOfAutocmd link=None",    // HLF_T     Titles for output from ":set all", ":autocmd" etc.
   "VisualMode deco=bold",         // 20 HLF_V       Visual mode
   "VisualModeAutoselecting link=None", // HLF_VNC   Visual mode, autoselecting and not clipboard owner
   "WildcardMenu link=None",       // HLF_WM    Wildmenu hilite
   "FoldedLine link=None",         // HLF_FL      Folded line
   "FoldColumn bg=grey12 fg=regular6", // HLF_FC      Fold column
   "DiffTextAdd fg=regular2",      // HLF_ADD  Added diff line
   "DiffText fg=regular4",         // HLF_CHD  Changed diff line
   "DiffChangedTextInChanged link=None", // HLF_TXD  Text Changed in changed diff line
   "DiffAddedTextInChanged link=None", // HLF_TXA  Text Added in changed diff line
   // Deleted diff line
   "DiffDeleted deco=bold bg=bright6 fg=regular4", // HLF_DED
   "SignColumn bg=grey10 fg=regular6", // 30 HLF_SC Sign column
   "Pmenu bg=regular4 bg=regular0", // HLF_PNI  popup menu normal item
   "PmenuSelected bg=grey4",         // HLF_PSI  popup menu selected item
   "PmenuMatchedText link=None",   // HLF_PMNI popup menu matched text in normal item
   "PmenuMatchedInSelected link=None", // HLF_PMSI popup menu matched text in selected item
   "PmenuNormalItem link=None",    // HLF_PNK  popup menu normal item "kind"
   "PmenuSelectedItem link=None",  // HLF_PSK   popup menu selected item "kind"
   "PmenuExtraText link=None",     // HLF_PNX   popup menu normal item "menu" (extra text)
   "PmenuSelectedExtraText link=None", // HLF_PSX   popup menu selected item "menu" (extra text)
   "PmenuScrollbar bg=grey12",       // HLF_PSB  popup menu scrollbar
   "PmenuScrollBarThumb  bg=regular7", // 40 HLF_PST  popup menu scrollbar thumb
   "Tabpanel deco=underline bg=grey4", // HLF_TPL   tabpanel
   "TabpanelSelected link=None",   // HLF_TPLS  tabpanel selected
   "TabpanelFill link=None",       // HLF_TPLF  tabpanel filler
   "CursorColumn bg=grey18",         // HLF_CUC  'cursorcolumn'
   "CursorLine  bg=444",             // HLF_CUL  'cursorline'
   "ColorColumn link=None",        // HLF_MC   'colorcolumn'
   "LocationPortalSelected link=PmenuSelectedItem", //HLF_QFL   location portal line currently 
                                                    //selected
   "TerminalStatusLine link=None", // 50 HLF_ST    status lines of terminal portals
   "TerminalNoncurrentStatusLine link=None", //HLF_STNC  status lines of not-current terminal 
                                               //portals
   "TerminalRed fg=regular1",        // HLF_TERMR  status lines of not-current terminal portals
   "TerminalGreen fg=bright2",       // HLF_TERMG  status lines of not-current terminal portals
   "TerminalBlue fg=bright4",        // HLF_TERMB  status lines of not-current terminal portals
   "MessageArea link=None",        // HLF_MSG   message area
   "MetaSpecialKeys link=None",    // HLF_8 Meta & special keys listed with ":map", text that is 
                                     // displayed different
   "LineNr fg=regular3",             // HLF_N   line number for ":number" and ":#" commands
   "LineNrAbove link=None",        // HLF_LNA  LineNrAbove
   "LineNrBelow link=None",        // HLF_LNB  LineNrBelow
   "SpellBad under=regular2 deco=undercurl", // HLF_SPB  SpellBad
   "SpellCap under=regular4 deco=undercurl", // HLF_SPC   SpellCap
   "SpellRare under=regular5 deco=undercurl", // HLF_SPR  SpellRare
   "SpellLocal under=regular6 deco=undercurl", // 60 HLF_SPL  SpellLocal
   "Directory fg=bright6",
   "CursorLineNr deco=bold fg=regular3",
   "MoreMsg deco=bold fg=143",
   "Question deco=bold fg=bright2", 
   "SpecialKey fg=bright4",
   "Title deco=bold fg=bright5",
   "WarningMsg bg=grey7 fg=bright1",
   "InfoMsg bg=grey6 fg=bright2",
   "WildMenu bg=regular3 fg=regular0",
   "Folded bg=grey2 fg=bright6",
   "Visual bg=grey16 fg=grey23",
   "ColorColumn bg=regular1",
   "MatchParen bg=regular6",
   "StatusLineTerm deco=bold fg=regular2 bg=bright2",
   "StatusLineTermNC fg=353 bg=050",
   "Search fg=regular0 bg=bright3",
   "CursorLineFold link=FoldColumn",
   "CurSearch link=Search",
   "LocationLine link=Search",
   "Comment fg=regular2",
   "Constant fg=544",
   "Special fg=540",
   "Identifier fg=255",
   "Statement fg=552",
   "PreProc fg=525",
   "Type fg=353",
   "Keyword fg=550",
   "Underlined fg=125 deco=underline",
   "Ignore fg=grey7",
   "Added fg=252",
   "Changed fg=225",
   "Removed fg=regular1",
   "Error bg=regular1",
   "Todo bg=regular3 fg=regular4",
   "Bold deco=bold",
   "Italic deco=italic"
}; 
   
// The names of hilite groups, separated by ZERO. Same len as hiliteGroupStrings
private CS hiNamesContainer;

// Table with the specifications for an decoration number.
// Note that this table is used by ALL books. This is required because the
// TUI can redraw at any time for any book.
private Arr(HiliteGroup) hilites;
private Short countGroups;
private DictStringInt128* hiNames;

//}}}

private Arena* a;


// Return the name of a hilite group.
private Text
hiliteGroupName(Short hiId) {
   return hilites[hiId].name;
}

// Return the ID of the link in a hilite group.
int
highlight_link_id(Short hiId) {
   return hilites[hiId].link;
}

// Store group names from hiliteGroupStrings into hiNames. Initialize names of hilite groups
private void
initializeGroups(void) {
   Int count = 0;
   Int len = 0;
   for (Int i = 0; i < countGroups; i++) {
      CS groupString = (CS)hiliteGroupStrings[i];
      CS nameEnd = skiptowhite(groupString);
      count++;
      len += (nameEnd - groupString + 1); // +1 for the ZERO
   }
   
   hiNamesContainer = alloc(len);
   hilites = alloc(count*sizeof(HiliteGroup));
   
   CS target = hiNamesContainer;
   HiliteGroup* targetGroup = hilites;
   for (Short i = 0; i < countGroups; i++) {
      CS groupString = (CS)hiliteGroupStrings[i];
      CS nameEnd = skiptowhite(groupString);
      Int nameLen = nameEnd - groupString;
      
      for (Short j = 0; j < nameLen; j++) {
         Byte byte = groupString[j];
         if (!bookIsCharPrintable(byte)) {
            emsg(_(e_unprintable_character_in_group_name));
            return;
         } ei (!ASCII_ISALNUM(byte) && byte != '_' && byte != '.') {
            msg_source(getDecoFlags(HLF_W));
            msg(_("W18: Invalid character in group name"));
            return;
         }
      }
      
      targetGroup->name = (Text){.c = target, .len = nameLen};
      
      memcpy(target, groupString, nameLen);
      target += nameLen; // +1 for the ZERO
      *target = ZERO;
      targetGroup->hiId = i;
      
      target++;
      targetGroup++;
   }
   
   hiNames = dictStringInt128NewJustIndices(hiNamesContainer, countGroups, a);
}

void
initHilite(int reset) { // clear group first?
   a = createArena();

   // load colors and groups (they are all built-in)
   countGroups = ARRAY_LENGTH(hiliteGroupStrings);
   initializeGroups();
   
   decorationsG = alloc((countGroups + 10)*sizeof(Decoration));
   doHilite((CS)hiliteGroupStrings[0], reset, TRUE);
   set_normal_colors();
   for (int i = 1; i < countGroups; ++i) {
      doHilite((CS)hiliteGroupStrings[i], reset, TRUE);
   } 
}

//// Reset all hiliting to the defaults. Removes all hiliting for the groups added by the user
//private void
//resetAllHilitesToDefaults(void) {
//   unletImpl((CS)"g:colors_name", true);
//
//   // Clear all default hilite groups and load the defaults.
//   for (Short hiId = 0; hiId < countGroups; ++hiId)
//      clearHiliteWorker(hiId);
//   initHilite(TRUE);
//   hiliteStarted();
//   redraw_later_clear();
//}

// Set the 'deco' field for the hilite group at index 'id'. 'arg' is deco name . 
// Returns TRUE if the decos are set.
private int
setDecoration(Text arg, OUT HiliteGroup* g) {
   Kv   input = (Kv){.value = arg};
   Kv* entry = (Kv *)bsearch(
      &input, &decoKinds,
      ARRAY_LENGTH(decoKinds), sizeof(decoKinds[0]), cmp_keyvalue_value_ni
   );
   if (!entry) {
      showErrFmtMsg(_(e_illegal_value_str), arg);
      return FALSE;
   }

   g->flags = entry->key;
   return TRUE;
}

private Boole
getColorByName(OUT VTermColor* res, Text name) {
   if (name.len == 8 
         && name.c[7] >= '0' && name.c[7] < '8' 
         && eq(((Text){name.c, 7}), S"regular")
   ) {
      *res = name.c[7] - '0';
      return true;
   } ei (name.len == 7
         && name.c[6] >= '0' && name.c[6] < '8' 
         && eq(((Text){name.c, 6}), S"bright")
   ) {
      *res = (name.c[7] - '0') + 8;
      return true;
   } ei (name.len == 3
         && EE_ISDIGIT(name.c[0])
         && EE_ISDIGIT(name.c[1])
         && EE_ISDIGIT(name.c[2])
   ) {
      *res = 16 + (name.c[0] - '0')*36 + (name.c[1] - '0')*6 + (name.c[2] - '0');
      return true;
   } ei ((name.len == 5 || (name.len == 6 && EE_ISDIGIT(name.c[5])))
         && EE_ISDIGIT(name.c[4])
         && eq(((Text){name.c, 4}), S"grey")
   ) {
      *res = 232 + ((name.len == 5) ? (name.c[4] - '0') : ((name.c[4] - '0')*10 + name.c[5] - '0'));
      return true;
   }
   return false;
}

//Set the foreground color for the hilite group at 'id'. Return TRUE if the color is set
private Boole
setForeground(HiliteGroup* group, Text arg){
   if (getColorByName(&(group->fg), arg)) {
      group->fieldPresence |= HI_HAS_FG;
      return true;
   }
   return false;
}

//Set the background color for the hilite group at 'id'. Returns TRUE if the color is set
private Boole
setBackground(HiliteGroup* group, Text arg){
   if (getColorByName(&(group->bg), arg) ) {
      group->fieldPresence |= HI_HAS_BG;
      return true;
   }
   return false; 
}

//Set the underline/undercurl color for the hilite group at 'id'.
//Return TRUE if the color is set.
private Boole
setUnderline(OUT HiliteGroup* group, Text arg) {
   if (getColorByName(&(group->under), arg)) {
      group->fieldPresence |= HI_HAS_UNDER;
      return true;
   }
   return false;
}

//{{{printing hilite groups

private CS
printColor(OUT Byte buf[static 4], VTermColor color) {
   sprintf((char *)buf, "%d", color);
   return buf;
}

// Print a hilite group to messages
private void
printHilite(HiliteGroup* group) {
   if (gotInterruptG || message_filtered(group->name.c))
      return;
   printHiliteHeaderNew(group);
   Byte buf[4];
   if (group->fieldPresence != 0)  {
      // Note: Keep this in sync with expandHiliteGroup().
      printHiliteDeco(group);
      msg_outtrans(printColor(OUT buf, group->fg));
      msg_outtrans(printColor(OUT buf, group->bg));
      msg_outtrans(printColor(OUT buf, group->under));
   } else {
      msg_outtrans(S"CLEARED");
   }
   if (group->link) {
      msgPutsDeco(S"links to", getDecoFlags(HLF_D));
      msg_putchar(' ');
      msg_outtrans(hilites[group->link].name.c);
   }

   if (p_verbose > 0)
      lastSetMsg(group->script_ctx);
}

// Prints the decorations content of a hilite group to Messages
private void
printHiliteDeco( HiliteGroup* group){
   int decoId = group->flags;
   if (decoId == 0) {
      return;
   }
   Byte buf[MAX_DECO_LEN];
   Unt resLen = 0;
   for (Unt i = 0; i < (int)ARRAY_LENGTH(decoKindIndices); ++i) {
      if (decoId & decoKindIndices[i]->key) {
         if (resLen > 0) {
            STRCPY(buf + resLen, (CS)",");
            resLen++;
         }
         STRCPY(buf + resLen, decoKindIndices[i]->value.c);
         resLen += decoKindIndices[i]->value.len;
      }
   }
   buf[resLen] = ZERO;
   msg_outtrans(buf);
}

private int
comparerByGroupName(void const* a, void const* b) {
   return STRCMP(((HiliteGroup*)a)->name.c, ((HiliteGroup*)b)->name.c);
}

private void
printAllHiliteGroups() {
   Arena* a = createArena();
   Arr(HiliteGroup) sortedGroups = allocateArray(countGroups, HiliteGroup, a);
   memcpy(sortedGroups, hilites, sizeof(HiliteGroup) * countGroups);
   qsort(sortedGroups, countGroups, sizeof(HiliteGroup), comparerByGroupName);
   for (int i = 0; i < countGroups; ++i) {
      // TODO: only call when the group has decos set?
      printHilite(sortedGroups + i);
   } 
      
   deleteArena(a); 
}

private void
printHiliteHeaderNew(HiliteGroup* group){
   msg_putchar('\n');
   msg_outtrans(group->name.c);
   int endcol = 15;
   if (visibleColsG <= (long)endcol)   // avoid hang for tiny window
      endcol = (int)(visibleColsG - 1);

   msg_advance(endcol);

   // Show how the decos look.
   msgPutsDeco((CS)"oOo", group->flags);
   msg_putchar(' ');
}

private Boole
printHiliteHeaderWorker(
   int didHeader,   // did header already
   int lineLen,      // length of the part of the line that has been printed already
   HiliteGroup* group
){
   int endcol = 19;
   Boole newline = true;
   int name_col = 0;

   if (!didHeader) {
      msg_putchar('\n');
      if (gotInterruptG)
         return true;
      msg_outtrans(group->name.c);
      name_col = msgColG;
      endcol = 15;
   } ei (msgColG + lineLen + 1 >= visibleColsG) {
      msg_putchar('\n');
      if (gotInterruptG)
         return true;
   } else {
      if (msgColG >= endcol)   // wrap around is like starting a new line
         newline = FALSE;
   }

   if (msgColG >= endcol)   // output at least one space
      endcol = msgColG + 1;
   if (visibleColsG <= (long)endcol)   // avoid hang for tiny window
      endcol = (int)(visibleColsG - 1);

   msg_advance(endcol);

   // Show "xxx" with the decos.
   if (didHeader) {
      if (endcol == visibleColsG - 1 && endcol <= name_col)
         msg_putchar(' ');
      msgPutsDeco((CS)"xxx", group->flags);
      msg_putchar(' ');
   }
   return newline;
}

// Output the syntax group header. Return TRUE when started a new line.
private Boole
printHiliteHeader(
   int       didHeader,   // did header already
   int       lineLen,     // length of the part of the line that has been printed already
   Short hiId
){
   HiliteGroup* group = hilites + hiId;
   return printHiliteHeaderWorker(didHeader, lineLen, group);
}


//}}}

// Handle ":highlight {from} link={to}" command.
private Boole
linkHilite(
   HiliteGroup* group,
   Text toName
) {
   clearHiliteWorker(OUT group);
   
   Short toId = hiliteGroupByName(toName);
   if (toId == SHORT) {
      return false;
   }
      
   group->link = toId;
   group->script_ctx = scriptPosG;
   group->script_ctx.lineNr += SOURCING_LNUM;
   group->fieldPresence |= HI_IS_LINK;
   redraw_all_later(UPD_SOME_VALID);
   return true;
}

private HiliteKey
parseHiliteKey(Text key) {
   HiliteKey retVal;
   if (sliceCmpToConst(key, "deco")) {
      retVal = DECO;
   } ei (sliceCmpToConst(key, "fg")) {
      retVal = FG;
   } ei (sliceCmpToConst(key, "bg")) {
      retVal = BG;
   } ei (sliceCmpToConst(key, "under")) {
      retVal = UNDER;
   } ei (sliceCmpToConst(key, "link")) {
      retVal = LINK;
   } else {
      retVal = KEY_PARSE_ERROR;
   }
   return retVal;
}

// Write the info from a hilite group to the corresponding decoration in decorationsG
private Decoration
writeToDecoration(HiliteGroup* restrict g) {
   Decoration deco;
   deco.fg = g->fg;
   deco.bg = g->bg;
   deco.under = g->under;
   deco.flags = g->flags;
   deco.fieldPresence = g->fieldPresence;
   deco.hiId = g->hiId;
   return deco;
}

// Fill the keys and kvs arrays for a hilite expression
// After the call, keys and kvs are terminated by structs with start = SHORT.
// In case of parse error, keys[0].start = SHORT - 1.
private void 
parseHiliteArgs(OUT HiKey keys[static 3], OUT HiKeyValue kvs[static 5], CS line) {
   Byte posEquals = 255; // set when "=" is encountered
   Byte posStart = 0;
   Byte indKeys = 0;
   Byte indKvs = 0;
   for (CS p = skipSpace(line); ;) {
      if (*p == ZERO || *p == ' ') {
         if (posEquals != 255) { //we've met "="
            if (indKvs == 4) {
               goto finalize;
            } ei (posEquals == posStart || p - line == posEquals + 1) {
               showErrFmtMsg(_(e_unexpected_equal_sign_str), line);
               goto errorOut;
            }
            
            kvs[indKvs] = (HiKeyValue){.start = posStart, .keyEnd = posEquals, .end = p - line};
            indKvs++;
         } else {
            if (indKeys == 2) {
               goto finalize;
            }
            keys[indKeys] = (HiKey){.start = posStart, .end = p - line};
            indKeys++;
         }
         p = skipSpace(p);
         if (*p == ZERO) {
            goto finalize;
         }
         posStart = (Short)(p - line);
         posEquals = 255;
      } else {
         if (*p == '=') {
            posEquals = (Short)(p - line);
         } 
         p++;
      }
   }
   
finalize:
   kvs[indKvs] = (HiKeyValue){.start = SHORT};
   keys[indKeys] = (HiKey){.start = SHORT};
   return;

errorOut: 
   keys[0] = (HiKey){.start = SHORT - 1};
}

// Handle the ":highlight .." command.
// :highlight Foo - print the hilite group
// :highlight Foo clear
// :highlight Foo link=Bar
// :highlight Foo fg=blue bg=#abcdef
// "init" is true when building the default hilite groups, false when called in script/commline
void
doHilite(CS line, Boole forceit, Boole init) { //TRUE when called for initializing
   Boole error = false;

   // If no argument, list current groups
   if (!init && endsComm(line)) {
      printAllHiliteGroups();
      return;
   }
   
   HiKey keys[3]; // up to two keys, the group name & optionala "none"
   HiKeyValue kvs[5]; // up to 4 key-values: "fg", "bg", "under" and "deco", or the single "link"
   parseHiliteArgs(OUT keys, OUT kvs, line);
   if (keys[0].start == SHORT || keys[0].start == SHORT - 1) {
      showErrFmtMsg(_(e_illegal_argument_str_3), line);
      return;
   }
   
   // Isolate the name.
   Text groupName = keyName(keys[0], line);
   Unt hiId = keys[0].start < SHORT ? hiliteGroupByName(groupName) : SHORT;
   
   if (hiId == SHORT) {
      showErrFmtMsg(_(e_hilite_group_name_not_found_str), line);
      return;
   } 
   
   HiliteGroup* group = hilites + hiId;
   
   // ":highlight {group-name}": just list hiliting for one group and exit.
   if (kvs[0].start == SHORT) {
      printHilite(group);
      return;
   }

   // Clear the highlighting for ":hi clear {group}" and ":hi clear".
   if (forceit || init) {
      clearHiliteWorker(OUT group);
   } 
   group->fieldPresence &= ~HI_IS_LINK;
   for (HiKeyValue* kv = kvs; kv->start != SHORT && !error; kv++) {
      Text keyStr = keyOf(*kv, line);
      HiliteKey key = parseHiliteKey(keyStr);
      if (key == KEY_PARSE_ERROR) {
         error = true;
         break;
      }
      Text val = valueOf(*kv, line);
      switch (key) {
      case DECO: {
         if (!setDecoration(val, OUT group)) {
            error = true;
         } 
         break;
      }
      case FG: {
         if (!setForeground(OUT group, val))
            error = true;
         break; 
      } 
      case BG: {
         if (!setBackground(OUT group, val))
            error = true;
         break; 
      } 
      case UNDER: {
         if (!setUnderline(OUT group, val))
            error = true;
         break; 
      } 
      case LINK: {
         if (kv != kvs || kvs[1].start != SHORT) { //if "link" is present, it must be alone
            showErrFmtMsg(_(e_illegal_argument_str_3), keyStr);
            error = true;
            break;
         }
         if (!linkHilite(group, val)) {
            showErrFmtMsg(_(e_hilite_group_name_not_found_str), val.c);
            error = true;
         }
         break;
      }
      default:
         showErrFmtMsg(_(e_illegal_argument_str_3), keyStr);
         error = true;
         break;
      }
   }
   
   if (error) {
      showErrFmtMsg(_(e_hilite_group_name_not_found_str), line);
      return;
   }
   
   decorationsG[group->hiId] = writeToDecoration(group);

   group->script_ctx = scriptPosG;
   group->script_ctx.lineNr += SOURCING_LNUM;

   if (!updating_screen)
      redraw_all_later(UPD_NOT_VALID);
   need_highlight_changed = TRUE;
}

// Clear hiliting for one group.
private void
clearHiliteWorker(OUT HiliteGroup* g) {
   g->fieldPresence = 0;
   g->flags = 0;
   // Since we set the default link, set the location to where the default link was set.
   g->script_ctx = g->deflink_sctx;
}

//Set the normal foreground and background colors according to the "Normal" hiliting group.
private void
set_normal_colors(void) {
   Decoration deco = getFullDecoration(0);
   // If the normal fg or bg color changed, a complete redraw is required.
   if (defaultFgColorG != deco.fg || defaultBgColorG != deco.bg) {
      defaultFgColorG = deco.fg;
      defaultBgColorG = deco.bg;
      set_must_redraw(UPD_CLEAR);
   }
}

// Combine special decos (e.g., for spelling) with other decos (e.g., for syntax hiliting).
// Since we expect there to be few spelling mistakes we don't cache the result.
// Return the resulting decos.
Decoration
combineDecorations(Decoration overlay, Decoration base) {
   if ((base.flags & DECO_UNDERLINE) != 0 && (overlay.flags & DECO_UNDERCURL) != 0) {
      return base;
   } ei((base.flags & DECO_UNDERCURL) != 0 && (overlay.flags & DECO_UNDERLINE) != 0) { 
      base.flags = (base.flags & ~DECO_UNDERCURL) | DECO_UNDERLINE;
   } else {
      base.flags |= overlay.flags;
   }
   base.under = overlay.under;
   return base;
}

// Return "1" if hilite group "id" has deco "flag". Return NULL otherwise.
private CS
hiliteHasFlag(HiliteGroup* g, Byte flag){
   return ((g->flags & flag) != 0) ? S"1" : null;
}

// Lookup a hilite group name and return its ID. If it is not found, SHORT is returned.
Short
hiliteGroupByName(Text name) {
   return getOrDefault(name, SHORT, hiNames);
}

// Lookup a hilite group name and return its decos. Return zero if not found.
Decoration
decosByHiliteName(CS name) {
   Short hiId = hiliteGroupByName(mbText(name));
   return hiId != SHORT ? getFullDecoration(hiId) : EMPTY_DECO;
}

// Return TRUE if hilite group "name" exists.
Boole
hiliteExists(Text name) {
   return (hiliteGroupByName(name) < SHORT);
}

// Return the name of hilite group "id". When not a valid ID return an empty string.
CS
syn_id2name(Unt id) {
   if (id >= countGroups)
      return S"";
   return hilites[id].name.c;
}

// Translate a group ID to hilite decos. Precondition: "hl_id" must be > 0
Byte
decorationByHiliteId(Short hiId) {
   Unt resolvedId = hiResolveLinks(hiId);
   assert(resolvedId < SHORT);
   return hilites[resolvedId].flags;
}

// Get the colors and decos for a group ID. NOTE: the colors will be regular0 when not set
Byte
syn_id2colors(Short hiId, OUT VTermColor* fgp, OUT VTermColor* bgp) {
   Unt resolvedId = hiResolveLinks(hiId);
   assert(resolvedId < SHORT);
   Decoration deco = getFullDecoration(resolvedId);

   *fgp = deco.fg;
   *bgp = deco.bg;
   return deco.flags;
}

// Translate a group ID to the final group ID (following links). hiId must be != SHORT
private Short
hiResolveLinks(Short hiId) {
   // Follow links until there is no more. Look out for loops! Break after 100 links.
   for (int depth = 0; depth < 100; depth++) {
      HiliteGroup* group = hilites + hiId;
      if ((group->fieldPresence & HI_IS_LINK) == 0)
         break;
      hiId = group->link;
   }
   return hiId;
}

// Translate a group to the final group id (following links)
private HiliteGroup*
resolveLinksByGroup(HiliteGroup* group) {
   for (; group->link; group = hilites + group->link)
      {}
   return group;
}

// context for :highlight <group> <arg> expansion
//typedef struct {
//   int expand_hi_synid;       // ID for hilite group being completed
//   int expand_hi_equal_col; // column where the '=' is
//   int expand_hi_include_orig;       // whether to fill the existing current value or not
//   CS expandCurrValue;   // the existing current value
//   DictIterator expand_colornames_iter;   // iterator for looping through v:colornames
//} HiExpand;
//private HiExpand hiExpandS = {};

// Handle command line completion for :highlight command.
void
setCompletionContextInHiliteCommand(OUT Expand* xp, CS arg) {
   // Default: expand group names
   xp->context = EXPAND_HILITE_GROUP;
   xp->input = mbText(arg);
   hiComplIncludeNoneG = 0;
   hiComplIncludeLinkG = 2;
   hiComplIncludeDefaultG = 1;
}

//{{{ auto-completion (expansion)

// Function given to expandGeneric() to obtain the list of group names.
Text
getHiliteGroupName(Expand* xp UNUSED, int id) {
   Short hiId = (Short)id;
   if (hiId == SHORT)
      return (Text){E, 0};
   ei (hiId == countGroups && hiComplIncludeNoneG != 0)
      return text(S"NONE");
   ei (hiId == countGroups + hiComplIncludeNoneG && hiComplIncludeDefaultG != 0)
      return text(S"default");
   ei (hiId == countGroups + hiComplIncludeNoneG + hiComplIncludeDefaultG 
            && hiComplIncludeLinkG != 0
   )
     return text(S"link");
   ei (hiId == countGroups + hiComplIncludeNoneG + hiComplIncludeDefaultG + 1 
            && hiComplIncludeLinkG != 0
   )
      return text(S"clear");
   return hilites[hiId].name;
}

CS
getHiliteGroupNameAsCString(Expand *xp, int id) {
   return getHiliteGroupName(xp, (Short)id).c;
}

// Command-line expansion for :hi {group-name} <args>...
int
expandHiliteGroup(
   CS pattern,
   Expand* xp,
   RegMatch* rmp,
   OUT ExpandMatch* matches
){
   return expandGeneric(
      pattern,
      xp,
      rmp,
      &getHiliteGroupNameAsCString,
      FALSE,
      OUT matches
   );
}

//}}}

// Convert each of the hilite deco bits (bold, standout, underline,
// etc.) set in 'hlattr' into a separate boolean item in a Dictionary with the deco name as the key
private Bag *
getDecorationDict(int hlDeco) {
   Bag* dict = allocBag();
   for (int i = 0; i < (int)ARRAY_LENGTH(decoKindIndices); ++i) {
      if (hlDeco & decoKindIndices[i]->key) {
         bagAdd_bool(dict, decoKindIndices[i]->value.c, VVAL_TRUE);
      }
   }

   return dict;
}

// Return the contents of the hilite group at index 'hl_idx' as a
// Dictionary. If 'resolveLinks' is TRUE, then resolves the hilite group links recursively
private Bag*
toDict(Short hiId, int resolveLinks) {
   Bag* dict = allocBag();

   HiliteGroup* group = hilites + hiId;

   if (bagAddString(dict, S"name", group->name.c) == FAIL 
         || bagAddNumber(dict, S"id", hiId) == FAIL)
      goto error;

   group = resolveLinks ? resolveLinksByGroup(group) : group;

   if (group->flags != 0) {
      Bag* deco = getDecorationDict(group->flags);
      if (deco && bagAddBag(dict, (CS)"deco", deco) == FAIL)
         goto error;
   }
   Byte buf[4];
   
   if ((group->fieldPresence & HI_IS_LINK) != 0) {
      Text linkName = hilites[group->link].name;
      if (linkName.len > 0 && bagAddString(dict, S"linksto", linkName.c) == FAIL)
         goto error;

      if (group->deflink)
         bagAdd_bool(dict, S"default", VVAL_TRUE);
   } else {
      if ((group->fieldPresence & HI_HAS_FG) != 0)
         bagAddString(dict, S"fg", printColor(OUT buf, group->fg));
      if ((group->fieldPresence & HI_HAS_BG) != 0)
         bagAddString(dict, S"bg", printColor(OUT buf, group->bg));
      if ((group->fieldPresence & HI_HAS_UNDER) != 0)
         bagAddString(dict, S"under", printColor(OUT buf, group->under));
   }
   
   if (bagSize(dict) == 2)
      // If only 'name' is present, then the hilite group is cleared.
      bagAdd_bool(dict, S"cleared", VVAL_TRUE);

   return dict;

error:
   eeglFree(dict);
   return NULL;
}

// "hlget([name])" function
// Return the decos of a specific hilite group (if specified) or all the hilite groups
void
f_hlget(Var *argvars, Var *returnVar) {

   allocReturnList(returnVar);

   if (check_for_opt_string_arg(argvars, 0) == FAIL
          || (argvars[0].tag != VAR_UNKNOWN && check_for_opt_bool_arg(argvars, 1) == FAIL)
   )
      return;

   CS hlarg = NULL;
   Boole resolveLinks = false;
   if (argvars[0].tag != VAR_UNKNOWN) {
      // hilite group name supplied
      hlarg = convertVarToStringSingleUse(&argvars[0]);
      if (!hlarg)
         return;

      if (argvars[1].tag != VAR_UNKNOWN) {
         Boole error = false;
         resolveLinks = varGetNumberChk(argvars + 1, OUT &error);
         if (error)
            return;
      }
   }

   List* list = returnVar->list;
   for (Short i = 0; i < countGroups && !gotInterruptG; ++i) {
      if (!hlarg || eq(hilites[i].name, hlarg)) {
         Bag* bag = toDict(i, resolveLinks);
         if (bag)
            listAppendBag(list, bag);
      }
   }
}

char
getDecoFlags(Short hiId) {
   if (hiId == SHORT) {
      return 0;
   }
   return hilites[hiId].flags;
}

Decoration
getFullDecoration(Short hiId) {
   Short resolvedId = hiResolveLinks(hiId);
   HiliteGroup* g = hilites + resolvedId;
   
   return (Decoration) {
      .fg = g->fg, .bg = g->bg, .under = g->under, .flags = g->flags, 
      .fieldPresence = g->fieldPresence, .hiId = hiId
   };
}

Boole
decoEq(Decoration a, Decoration b) {
   return a.flags == b.flags && a.hiId == b.hiId;
}

//}}}
//{{{syntax hiliting

// Struct used to store one state of the state stack.
typedef struct buf_state {
   int bs_idx;    // index of pattern
   int bs_flags;    // flags for pattern
   int bs_seqnr;    // stores si_seqnr
   int bs_cchar;    // stores si_cchar
   RegExternalMatch* bs_extmatch; // external matches from start pattern
} BufState;

#define HL_CONTAINED   0x01   // not used on toplevel
#define HL_TRANSP      0x02   // has no highlighting
#define HL_ONELINE     0x04   // match within one line only
#define HL_HAS_EOL     0x08   // end pattern that matches with $
#define HL_SYNC_HERE   0x10   // sync point after this item (syncing only)
#define HL_SYNC_THERE  0x20   // sync point at current line (syncing only)
#define HL_MATCH       0x40   // use match ID instead of item ID
#define HL_SKIPNL      0x80   // nextgroup can skip newlines
#define HL_SKIPWHITE  0x100   // nextgroup can skip white space
#define HL_SKIPEMPTY  0x200   // nextgroup can skip empty lines
#define HL_KEEPEND    0x400   // end match always kept
#define HL_EXCLUDENL  0x800   // exclude NL from match
#define HL_DISPLAY    0x1000   // only used for displaying, not syncing
#define HL_FOLD       0x2000   // define fold
#define HL_EXTEND     0x4000   // ignore a keepend
#define HL_MATCHCONT  0x8000   // match continued from previous line
#define HL_TRANS_CONT 0x10000 // transparent item without contains arg
#define HL_CONCEAL    0x20000 // can be concealed
#define HL_CONCEALENDS 0x40000 // can be concealed
#define HL_INCLUDED_TOPLEVEL 0x80000 // toplevel item in included syntax, allowed by contains=TOP

#define BUFF_SYN_VAR S"b:currentSyntax"
#define PORT_SYN_VAR S"w:currentSyntax"

#define SST_MIN_ENTRIES 150   // minimal size for state stack array
#define SST_MAX_ENTRIES 1000   // maximal size for state stack array
#define SST_FIX_STATES  7   // size of sst_stack[].
#define SST_DIST        16   // normal distance between entries
#define SST_INVALID   ((synstate_T *)-1)   // invalid syn_state pointer


// syn_state contains the syntax state stack for the start of one line. Used by array[].
typedef struct SyntaxState SyntaxState;

struct SyntaxState {
   SyntaxState   *next; // next entry in used or free list
   LineNr   lnum;   // line number for this state
   union {
      BufState   stack[SST_FIX_STATES]; // short state stack
      ArrayList   arrayList;   // growarray for long state stack
   } sst_union;
   Unt      next_flags; // flags for next_list
   int      stacksize;  // number of states on the stack
   Short   *next_list;  // "nextgroup" list in this state (this is a copy, don't free it!)
   DisplayTick   tick;   // tick when last displayed
   LineNr   invalidatingChangeLnum;// when non-zero, change in this line may have made the state invalid
};


// struct passed to in_id_list()
typedef struct {
   int   inc_tag;   // ":syn include" unique tag
   Short   hiId;      // highlight group ID of item
   Short* containedInHiId;   // cont.in group IDs, if non-zero
} SyntaxInfo;

// Each keyword has one keyentry, which is linked in a hash list.
typedef struct KeyEntry KeyEntry;

struct KeyEntry {
   KeyEntry   *next;   // next entry with identical "keyword[]"
   SyntaxInfo syntax;   // struct passed to in_id_list()
   Short* next_list;   // ID list for next match (if non-zero)
   Unt flags;
   Byte keyword[1];   // actually longer
};

// different types of offsets that are possible
#define SPO_MS_OFF   0   // match  start offset
#define SPO_ME_OFF   1   // match  end   offset
#define SPO_HS_OFF   2   // hilite start offset
#define SPO_HE_OFF   3   // hilite end   offset
#define SPO_RS_OFF   4   // region start offset
#define SPO_RE_OFF   5   // region end   offset
#define SPO_LC_OFF   6   // leading context offset
#define SPO_COUNT    7

private CS (spo_name_tab[SPO_COUNT]) = {
   SMAP((CS), "ms=", "me=", "hs=", "he=", "rs=", "re=", "lc=")
};

//The patterns that are being searched for are stored in a syn_pattern.
//A match item consists of one pattern.
//A start/end item consists of n start patterns and m end patterns.
//A start/skip/end item consists of n start patterns, one skip pattern and m
//end patterns.
//For the latter two, the patterns are always consecutive: start-skip-end.
//
//A character offset can be given for the matched text (_m_start and _m_end)
//and for the actually highlighted text (_h_start and _h_end).
//
//Note that ordering of members is optimized to reduce padding.
typedef struct syn_pattern {
   char sp_type;      // see SPTYPE_ defines below
   char syncing;      // this item used for syncing
   Short patternHiId; // highlight group ID of pattern
   Short sp_off_flags; // see below
   int sp_offsets[SPO_COUNT];   // offsets
   Unt sp_flags;      // see HL_ defines below
   Boole sp_ic;         // ignore-case flag for prog
   int sp_sync_idx;      // sync item index (syncing only)
   int sp_line_id;      // ID of last line where tried
   int sp_startcol;      // next match in sp_line_id line
   Short* sp_containsHiId;      // cont. group IDs, if non-zero
   Short* sp_next_list;      // next group IDs, if non-zero
   SyntaxInfo syntax;      // struct passed to in_id_list()
   CS pattern;      // regexp to match, pattern
   RegProg* prog;      // regexp to match, program
} SyntaxPattern;

// The sp_off_flags are computed like this:
// offset from the start of the matched text: (1 << SPO_XX_OFF)
// offset from the end    of the matched text: (1 << (SPO_XX_OFF + SPO_COUNT))
// When both are present, only one is used.

#define SPTYPE_MATCH 1 //match keyword with this group ID
#define SPTYPE_START 2 //match a regexp, start of item
#define SPTYPE_END   3 //match a regexp, end of item
#define SPTYPE_SKIP  4 //match a regexp, skip within item


#define SYN_ITEMS(buf)   ((SyntaxPattern *)((buf)->syntaxPatterns.c))

#define NONE_IDX   (-2)   // value of sp_sync_idx for "NONE"

// Flags for syncFlags:
#define SF_CCOMMENT 0x01  // sync on a C-style comment
#define SF_MATCH    0x02  // sync by matching a pattern

#define SYN_STATE_P(ssp)    ((BufState *)((ssp)->c))

#define MAXKEYWLEN   80   // maximum length of a keyword

// The attributes of the syntax item that has been recognized.
private int current_id = 0;       // ID of current char for syn_get_id()
private int current_trans_id = 0; // idem, transparency removed
private int current_flags = 0;
private int current_seqnr = 0;

typedef struct syn_cluster_S {
   CS name;      // syntax cluster name
   CS nameUpper; // uppercase of name
   Arr(Short) hiIds;    // IDs in this syntax cluster
} SynCluster;

// Methods of combining two clusters
#define CLUSTER_REPLACE   1   // replace first list with second
#define CLUSTER_ADD       2   // add second list to first
#define CLUSTER_SUBTRACT  3   // subtract second list from first

#define SYN_CLSTR(buf)   ((SynCluster *)((buf)->syntaxClusters.c))

//Syntax group IDs have different types:
//    0 - 19999  normal syntax groups
//20000 - 20999  ALLBUT indicator (current_syn_inc_tag added)
//21000 - 21999  TOP indicator (current_syn_inc_tag added)
//22000 - 22999  CONTAINED indicator (current_syn_inc_tag added)
//23000 - 32767  cluster IDs (subtract SYNID_CLUSTER for the cluster ID)
#define SYNID_ALLBUT    SHORT // syntax group ID for contains=ALLBUT
#define SYNID_TOP       21000        // syntax group ID for contains=TOP
#define SYNID_CONTAINED 22000  // syntax group ID for contains=CONTAINED
#define SYNID_CLUSTER   23000    // first syntax group ID for clusters

#define MAX_SYN_INC_TAG   999    // maximum before the above overflow
#define MAX_CLUSTER_ID  (32767 - SYNID_CLUSTER)

// Annoying Hack(TM):  ":syn include" needs this pointer to pass to
// expand_filename().  Most of the other syntax commands don't need it, so
// instead of passing it to them, we stow it here.
private Byte** synCommline;

// Another Annoying Hack(TM):  To prevent rules from other ":syn include"'d files from leaking 
// into ALLBUT lists, we assign a unique ID to the rules in each ":syn include"'d file.
private int current_syn_inc_tag = 0;
private int running_syn_inc_tag = 0;

//In a EeSet item "hi_key" points to "keyword" in a keyentry.
//This avoids adding a pointer to the EeSet item.
//KE2HIKEY() converts a var pointer to a EeSetItem key pointer.
//HIKEY2KE() converts a EeSetItem key pointer to a var pointer.
//HI2KE() converts a EeSetItem pointer to a var pointer.
private KeyEntry dumkey;
#define KE2HIKEY(kp)  ((kp)->keyword)
#define HIKEY2KE(p)   ((KeyEntry *)((p) - (dumkey.keyword - (CS)&dumkey)))
#define HI2KE(hi)      HIKEY2KE((hi)->hi_key)

// To reduce the time spent in keepend(), remember at which level in the state
// stack the first item with "keepend" is present.  When "-1", there is no "keepend" on the stack.
private int keepend_level = -1;

private Byte msg_no_items[] = "No Syntax items defined for this buffer";

//For the current state we need to remember more than just the idx.
//When matchEndPos.lnum is 0, the items other than si_idx are unknown.
//(The end positions have the column number of the next char)
typedef struct state_item {
   int si_idx;         // index of syntax pattern or KEYWORD_IDX
   Short hiId;         // highlight group ID for keywords
   int transparentHiId;      // idem, transparency removed
   int matchLnum;      // lnum of the match
   int matchStartCol;      // starting column of the match
   PosNoVirt matchEndPos;      // just after end posn of the match
   PosNoVirt hiStartPos;      // start position of the highlighting
   PosNoVirt hiEndPos;      // end position of the highlighting
   PosNoVirt endPattEndPos;      // end position of end pattern
   int si_end_idx;      // group ID for end pattern or zero
   int si_ends;      // if match ends before matchEndPos
   char flags;      // decorations in this state
   long si_flags;      // HL_HAS_EOL flag in this state, and HL_SKIP* for nextList
   Short* si_containsHiId;      // list of contained groups
   Short* nextList;      // nextgroup IDs after this item ends
   RegExternalMatch *si_extmatch;   // \z(...\) matches from start pattern
} StateItem;

#define KEYWORD_IDX   (-1)       // value of si_idx for keywords
#define ID_LIST_ALL   ((Short *)-1) // valid of si_containsHiId for containing all
                                    // but contained groups

// Struct to reduce the number of arguments to get_syn_options(), it's used very often.
typedef struct {
   int flags;      // flags for contained and transparent
   int keyword;   // TRUE for ":syn keyword"
   int* sync_idx;   // syntax item for "grouphere" argument, NULL if not allowed
   Boole has_containsHiId;   // TRUE if "containsHiId" can be used
   Short* containsHiId;   // group IDs for "contains" argument
   Short* containedInHiId;   // group IDs for "containedin" argument
   Short* next_list;   // group IDs for "nextgroup" argument
} SynOptArg;

//The next possible match in the current line for any pattern is remembered,
//to avoid having to try for a match in each column.
//If nextMatchIdx == -1, not tried (in this line) yet.
//If nextMatchCol == MAXCOL, no match found in this line.
//(All end positions have the column of the char after the end)
private int nextMatchCol;      // column for start of next match
private PosNoVirt next_match_m_endpos;   // position for end of next match
private PosNoVirt next_match_h_startpos;   // pos. for highl. start of next match
private PosNoVirt next_match_h_endpos;   // pos. for highl. end of next match
private int nextMatchIdx;      // index of matched item
private long next_match_flags;      // flags for next match
private PosNoVirt next_match_eos_pos;   // end of start pattn (start region)
private PosNoVirt next_match_eoe_pos;   // pos. for end of end pattern
private int next_match_end_idx;      // ID of group for end pattn or zero
private RegExternalMatch *next_match_extmatch = NULL;

//A state stack is an array of integers or StateItem, stored in an
//ArrayList. A state stack is invalid if its itemsize entry is zero.
#define INVALID_STATE(ssp)  ((ssp)->ga_itemsize == 0)
#define VALID_STATE(ssp)    ((ssp)->ga_itemsize != 0)

#define FOR_ALL_SYNSTATES(sb, sst) \
    for ((sst) = (sb)->first; (sst) != NULL; (sst) = (sst)->next)

//The current state (within the line) of the recognition engine.
//When current_state.ga_itemsize is 0 the current state is invalid.
private Portal* syntPortS;      // current portal for hiliting
private Book* synBookS;      // current buffer for hiliting
private SyntaxBlock* synBlockS; // current buffer for hiliting
private LineNr currLnumS = 0;   // lnum of current state
private ColNr currColS = 0;   // column of current state
private Boole currentStateStoredS = false; // if stored current state after setting currentFinishedS
private Boole currentFinishedS = false;   // current line has been finished
private ArrayList current_state = {0, 0, 0, 0, NULL}; // current stack of state_items
private Short* current_next_list = NULL; // when non-zero, nextgroup list
private Unt current_next_flags = 0; // flags for current_next_list
private int current_line_id = 0;   // unique number for current line

#define CUR_STATE(idx)   ((StateItem *)(current_state.c))[idx]

private void syn_sync(Portal *po, LineNr lnum, SyntaxState *last_valid);
private int syn_match_linecont(LineNr lnum);
private void syn_start_line(void);
private void syn_update_ends(int startofline);
private void syn_stack_alloc(void);
private int syn_stack_cleanup(void);
private void syn_stack_free_entry(SyntaxBlock *block, SyntaxState *p);
private SyntaxState *syn_stack_find_entry(LineNr lnum);
private SyntaxState *store_current_state(void);
private void load_current_state(SyntaxState *from);
private void invalidate_current_state(void);
private int syn_stack_equal(SyntaxState *sp);
private void validate_current_state(void);
private int syn_finish_line(int syncing);
private Decoration getCurrentDeco(Boole syncing, Boole displaying, Boole keep_state);
private int did_match_already(int idx, ArrayList *gap);
private StateItem *push_next_match(StateItem *currStateItem);
private void check_state_ends(void);
private void update_si_attr(int idx);
private void check_keepend(void);
private void update_si_end(StateItem *sip, int startcol, Boole force);
private Short* copy_id_list(Short *list);
private int in_id_list(StateItem *item, Arr(Short) containsHiId, SyntaxInfo* ssp, int flags);
private int push_current_state(int idx);
private void pop_current_state(void);
#define IF_SYN_TIME(p) NULL
typedef int syn_Time;

private void syn_stack_apply_changes_block(SyntaxBlock *block, Book* book);
private void find_endpos(
      int idx, PosNoVirt *startpos, PosNoVirt *m_endpos, PosNoVirt *hl_endpos, long *flagsp, 
      PosNoVirt *end_endpos, int *end_idx, RegExternalMatch *start_ext
);

private void limit_pos(PosNoVirt *pos, PosNoVirt *limit);
private void limit_pos_zero(PosNoVirt *pos, PosNoVirt *limit);
private void syn_add_end_off(PosNoVirt *result, RegMultilineMatch *regmatch, SyntaxPattern *spp, int idx, int extra);
private void syn_add_start_off(PosNoVirt *result, RegMultilineMatch *regmatch, SyntaxPattern *spp, int idx, int extra);
private CS syn_getcurline(void);
private ColNr syn_getcurline_len(void);
private int syn_regexec(RegMultilineMatch *rmp, LineNr lnum, ColNr col, syn_Time *st);
private Short check_keyword_id(
      CS line, int startcol, int *endcol, long *flags, Short **next_list, StateItem *currStateItem,
      int *ccharp
);
private void syn_remove_pattern(SyntaxBlock *block, int idx);
private void syn_clear_pattern(SyntaxBlock *block, int i);
private void syn_clear_cluster(SyntaxBlock *block, int i);
private void syn_clear_one(Short id, int syncing);
private void callScriptForSubcommand(Invocation* invo, char *name);
private void syn_lines_msg(void);
private void syn_match_msg(void);
private void syn_list_one(int id, int syncing, int link_only);
private void syn_list_cluster(int id);
private void put_id_list(CS name, Short *list, int deco);
private void put_pattern(CS s, int c, SyntaxPattern *spp, int deco);
private int syn_list_keywords(int id, EeSet *ht, int did_header, int deco);
private void syn_clear_keyword(int id, EeSet *ht);
private void clearKeywordTable(EeSet *ht);
private int syn_check_cluster(CS pp, int len);
private int addCluster(CS name);
private void init_syn_patterns(void);
private CS getSyntPattern(CS arg, SyntaxPattern *ci, OUT Boole* hadEol);
private int get_id_list(Byte **arg, int keylen, OUT Short **list, int skip);
private void syn_combine_list(Short **clstr1, Short **clstr2, int list_op);

//Start the syntax recognition for a line.  This function is normally called
//from the screen updating, once for each displayed line.
//The buffer is remembered in synBookS, because syntGetDeco() doesn't get
//it.   Careful: curBook and curPor are likely to point to another buffer and portal.
void
syntaxStartLine(Portal *wp, LineNr lnum) {
   SyntaxState   *p;
   SyntaxState   *last_valid = NULL;
   SyntaxState   *last_min_valid = NULL;
   SyntaxState   *sp, *prev = NULL;
   LineNr   parsed_lnum;
   LineNr   first_stored;
   int      dist;
   static Long changedtick = 0;   // remember the last change ID

   // After switching books, invalidate current_state.
   // Also do this when a change was made, the current state may be invalid then.
   if (synBlockS != wp->ownSyntax || synBookS != wp->book || changedtick != CHANGEDTICK(synBookS)) {
      invalidate_current_state();
      synBookS = wp->book;
      synBlockS = wp->ownSyntax;
   }
   changedtick = CHANGEDTICK(synBookS);
   syntPortS = wp;

   // Allocate syntax stack when needed.
   syn_stack_alloc();
   if (synBlockS->array == NULL)
      return;      // out of memory
   synBlockS->lastDisplayTick = display_tick;

   // If the state of the end of the previous line is useful, store it.
   if (VALID_STATE(&current_state)
       && currLnumS < lnum
       && currLnumS < synBookS->mem.lineCount
   ) {
      (void)syn_finish_line(FALSE);
      if (!currentStateStoredS) {
         ++currLnumS;
         (void)store_current_state();
      }

      // If the currLnumS is now the same as "lnum", keep the current state (this happens very 
      // often!).  Otherwise invalidate current_state and figure it out below.
      if (currLnumS != lnum)
         invalidate_current_state();
   } else
      invalidate_current_state();

   //Try to synchronize from a saved state in array[].
   //Only do this if lnum is not before and not to far beyond a saved state.
   if (INVALID_STATE(&current_state) && synBlockS->array != NULL) {
      // Find last valid saved state before start_lnum.
      FOR_ALL_SYNSTATES(synBlockS, p) {
          if (p->lnum > lnum)
         break;
          if (p->lnum <= lnum && p->invalidatingChangeLnum == 0) {
         last_valid = p;
         if (p->lnum >= lnum - synBlockS->b_syn_sync_minlines)
             last_min_valid = p;
          }
      }
      if (last_min_valid != NULL)
          load_current_state(last_min_valid);
   }

   // If "lnum" is before or far beyond a line with a saved state, need to re-synchronize.
   if (INVALID_STATE(&current_state)) {
      syn_sync(wp, lnum, last_valid);
      if (currLnumS == 1)
         // First line is always valid, no matter "minlines".
         first_stored = 1;
      else
         // Need to parse "minlines" lines before state can be considered valid to store.
         first_stored = currLnumS + synBlockS->b_syn_sync_minlines;
   } else
      first_stored = currLnumS;

   // Advance from the sync point or saved state until the current line.
   // Save some entries for syncing with later on.
   if (synBlockS->len <= visibleRowsG)
      dist = 999999;
   else
      dist = synBookS->mem.lineCount / (synBlockS->len - visibleRowsG) + 1;
   while (currLnumS < lnum) {
      syn_start_line();
      (void)syn_finish_line(FALSE);
      ++currLnumS;

      // If we parsed at least "minlines" lines or started at a valid
      // state, the current state is considered valid.
      if (currLnumS >= first_stored) {
         // Check if the saved state entry is for the current line and is
         // equal to the current state.  If so, then validate all saved
         // states that depended on a change before the parsed line.
         if (prev == NULL)
            prev = syn_stack_find_entry(currLnumS - 1);
         if (prev == NULL)
            sp = synBlockS->first;
         else
            sp = prev;
         while (sp != NULL && sp->lnum < currLnumS)
            sp = sp->next;
         if (sp != NULL
             && sp->lnum == currLnumS
             && syn_stack_equal(sp)
         ) {
            parsed_lnum = currLnumS;
            prev = sp;
            while (sp != NULL && sp->invalidatingChangeLnum <= parsed_lnum) {
               if (sp->lnum <= lnum)
                  // valid state before desired line, use this one
                  prev = sp;
               ei (sp->invalidatingChangeLnum == 0)
                  // past saved states depending on change, break here.
                  break;
               sp->invalidatingChangeLnum = 0;
               sp = sp->next;
            }
            load_current_state(prev);
         }
         // Store the state at this line when it's the first one, the line
         // where we start parsing, or some distance from the previously
         // saved state.  But only when parsed at least 'minlines'.
         ei (prev == NULL
               || currLnumS == lnum
               || currLnumS >= prev->lnum + dist)
            prev = store_current_state();
      }

      // This can take a long time: break when CTRL-C pressed. The current state will be wrong then
      line_breakcheck();
      if (gotInterruptG) {
         currLnumS = lnum;
         break;
      }
    }

    syn_start_line();
}

//We cannot simply discard growarrays full of state_items or buf_states; we
//have to manually release their extmatch pointers first.
private void
clear_syn_state(SyntaxState *p) {
   int      i;
   ArrayList   *gap;

   if (p->stacksize > SST_FIX_STATES) {
      gap = &(p->sst_union.arrayList);
      for (i = 0; i < gap->len; i++)
         unref_extmatch(SYN_STATE_P(gap)[i].bs_extmatch);
      ga_clear(gap);
   } else {
      for (i = 0; i < p->stacksize; i++)
         unref_extmatch(p->sst_union.stack[i].bs_extmatch);
   }
}

// Cleanup the current_state stack.
private void
clear_current_state(void) {
   int      i;
   StateItem   *sip;

   sip = (StateItem *)(current_state.c);
   for (i = 0; i < current_state.len; i++)
      unref_extmatch(sip[i].si_extmatch);
   ga_clear(&current_state);
}

//Try to find a synchronisation point for line "lnum".
//
//This sets currLnumS and the current state.  One of three methods is used:
//1. Search backwards for the end of a C-comment.
//2. Search backwards for given sync patterns.
//3. Simply start on a given number of lines above "lnum".
private void
syn_sync(Portal   *wp, LineNr   start_lnum, SyntaxState   *last_valid){
   Book   *curbuf_save;
   Portal   *curPor_save;
   Pos   cursor_save;
   int      idx;
   LineNr   lnum;
   LineNr   end_lnum;
   LineNr   break_lnum;
   int      had_sync_point;
   StateItem   *currStateItem;
   SyntaxPattern   *spp;
   int      found_flags = 0;
   int      found_match_idx = 0;
   LineNr   found_current_lnum = 0;
   int      found_current_col= 0;
   PosNoVirt   found_m_endpos;
   ColNr   prev_current_col;

   // Clear any current state that might be hanging around.
   invalidate_current_state();

   //Start at least "minlines" back.  Default starting point for parsing is there.
   //Start further back, to avoid that scrolling backwards will result in
   //resyncing for every line.  Now it resyncs only one out of N lines,
   //where N is minlines * 1.5, or minlines * 2 if minlines is small.
   //Watch out for overflow when minlines is MAXLNUM.
   if (synBlockS->b_syn_sync_minlines > start_lnum)
      start_lnum = 1;
   else {
      if (synBlockS->b_syn_sync_minlines == 1)
         lnum = 1;
      ei (synBlockS->b_syn_sync_minlines < 10)
         lnum = synBlockS->b_syn_sync_minlines * 2;
      else
         lnum = synBlockS->b_syn_sync_minlines * 3 / 2;
      if (synBlockS->b_syn_sync_maxlines != 0 && lnum > synBlockS->b_syn_sync_maxlines)
         lnum = synBlockS->b_syn_sync_maxlines;
      if (lnum >= start_lnum)
         start_lnum = 1;
      else
         start_lnum -= lnum;
   }
   currLnumS = start_lnum;

   // 1. Search backwards for the end of a C-style comment.
   if (synBlockS->syncFlags & SF_CCOMMENT) {
      // Need to make synBookS the current buffer for a moment to be able to use find_start_comment()
      curPor_save = curPor;
      curPor = wp;
      curbuf_save = curBook;
      curBook = synBookS;

      // Skip lines that end in a backslash.
      for ( ; start_lnum > 1; --start_lnum) {
         CS l = ml_get(start_lnum - 1);
         if (*l == ZERO || *(l + ml_get_len(start_lnum - 1) - 1) != '\\')
            break;
      }
      currLnumS = start_lnum;

      // set cursor to start of search
      cursor_save = wp->cursor;
      wp->cursor.lnum = start_lnum;
      wp->cursor.col = 0;

      // If the line is inside a comment, need to find the syntax item that defines the comment.
      // Restrict the search for the end of a comment to b_syn_sync_maxlines.
      if (find_start_comment((int)synBlockS->b_syn_sync_maxlines) != NULL) {
         for (idx = synBlockS->syntaxPatterns.len; --idx >= 0; ) {
            if (SYN_ITEMS(synBlockS)[idx].syntax.hiId == synBlockS->syncHiId
               && SYN_ITEMS(synBlockS)[idx].sp_type == SPTYPE_START
            ){
                validate_current_state();
                if (push_current_state(idx) == OK)
               update_si_attr(current_state.len - 1);
                break;
            }
         } 
      }

      // restore cursor and buffer
      wp->cursor = cursor_save;
      curPor = curPor_save;
      curBook = curbuf_save;
   }

   //2. Search backwards for given sync patterns.
   ei (synBlockS->syncFlags & SF_MATCH) {
      if (synBlockS->b_syn_sync_maxlines != 0 && start_lnum > synBlockS->b_syn_sync_maxlines)
         break_lnum = start_lnum - synBlockS->b_syn_sync_maxlines;
      else
         break_lnum = 0;

      found_m_endpos.lnum = 0;
      found_m_endpos.col = 0;
      end_lnum = start_lnum;
      lnum = start_lnum;
      while (--lnum > break_lnum) {
         // This can take a long time: break when CTRL-C pressed.
         line_breakcheck();
         if (gotInterruptG) {
            invalidate_current_state();
            currLnumS = start_lnum;
            break;
         }

         // Check if we have run into a valid saved state stack now.
         if (last_valid != NULL && lnum == last_valid->lnum) {
            load_current_state(last_valid);
            break;
         }

         // Check if the previous line has the line-continuation pattern.
         if (lnum > 1 && syn_match_linecont(lnum - 1))
            continue;

         // Start with nothing on the state stack
         validate_current_state();

         for (currLnumS = lnum; currLnumS < end_lnum; ++currLnumS) {
            syn_start_line();
            for (;;) {
                had_sync_point = syn_finish_line(TRUE);
                /*
                 * When a sync point has been found, remember where, and
                 * continue to look for another one, further on in the line.
                 */
                if (had_sync_point && current_state.len) {
               currStateItem = &CUR_STATE(current_state.len - 1);
               if (currStateItem->matchEndPos.lnum > start_lnum) {
                   // ignore match that goes to after where started
                   currLnumS = end_lnum;
                   break;
               }
               if (currStateItem->si_idx < 0) {
                   // Cannot happen?
                   found_flags = 0;
                   found_match_idx = KEYWORD_IDX;
               } else {
                   spp = &(SYN_ITEMS(synBlockS)[currStateItem->si_idx]);
                   found_flags = spp->sp_flags;
                   found_match_idx = spp->sp_sync_idx;
               }
               found_current_lnum = currLnumS;
               found_current_col = currColS;
               found_m_endpos = currStateItem->matchEndPos;
               // Continue after the match (be aware of a zero-length match).
               if (found_m_endpos.lnum > currLnumS) {
                   currLnumS = found_m_endpos.lnum;
                   currColS = found_m_endpos.col;
                   if (currLnumS >= end_lnum)
                  break;
               } ei (found_m_endpos.col > currColS)
                  currColS = found_m_endpos.col;
               else
                  ++currColS;

               //getCurrentDeco() will have skipped the check for an item that ends here, need to 
               //do that now. Be careful not to go past the ZERO.
               prev_current_col = currColS;
               if (syn_getcurline()[currColS] != ZERO)
                  ++currColS;
               check_state_ends();
               currColS = prev_current_col;
               } else
                  break;
            }
         }

         // If a sync point was encountered, break here.
         if (found_flags) {
            // Put the item that was specified by the sync point on the
            // state stack.  If there was no item specified, make the state stack empty.
            clear_current_state();
            if (found_match_idx >= 0
               && push_current_state(found_match_idx) == OK)
                update_si_attr(current_state.len - 1);

            //When using "grouphere", continue from the sync point match, until the end of the 
            //line. Parsing starts at the next line. For "groupthere" the parsing starts at 
            //start_lnum.
            if (found_flags & HL_SYNC_HERE) {
               if (current_state.len) {
                  currStateItem = &CUR_STATE(current_state.len - 1);
                  currStateItem->hiStartPos.lnum = found_current_lnum;
                  currStateItem->hiStartPos.col = found_current_col;
                  update_si_end(currStateItem, (int)currColS, TRUE);
                  check_keepend();
               }
               currColS = found_m_endpos.col;
               currLnumS = found_m_endpos.lnum;
               (void)syn_finish_line(FALSE);
               ++currLnumS;
            } else
               currLnumS = start_lnum;

            break;
          }

          end_lnum = lnum;
          invalidate_current_state();
      }

      // Ran into start of the file or exceeded maximum number of lines
      if (lnum <= break_lnum) {
         invalidate_current_state();
         currLnumS = break_lnum + 1;
      }
   }

   validate_current_state();
}

private void
save_chartab(CS chartab) {
   if (!synBookS->o.isKeyword)
      return;

   mch_memmove(chartab, synBookS->charsForKeywords, (Unt)32);
   mch_memmove(synBookS->charsForKeywords, syntPortS->ownSyntax->b_syn_chartab, (Unt)32);
}

private void
restoreKeywordChars(CS chartab) {
   if (synBookS->o.isKeyword)
      mch_memmove(synBookS->charsForKeywords, chartab, (Unt)32);
}

// Return TRUE if the line-continuation pattern matches in line "lnum".
private int
syn_match_linecont(LineNr lnum) {
   RegMultilineMatch regmatch;
   Byte bookKeywordChars[32];  // chartab array for syn iskeyword

   if (synBlockS->lineContinProg == NULL)
      return FALSE;

   // use syntax @iskeyword option
   save_chartab(bookKeywordChars);
   regmatch.rmm_ic = synBlockS->lineContinIgnoreCase;
   regmatch.regprog = synBlockS->lineContinProg;
   int r = syn_regexec(&regmatch, lnum, (ColNr)0,
      IF_SYN_TIME(&synBlockS->b_syn_linecont_time));
   synBlockS->lineContinProg = regmatch.regprog;
   restoreKeywordChars(bookKeywordChars);
   return r;
}

// Prepare the current state for the start of a line.
private void
syn_start_line(void) {
   currentFinishedS = false;
   currColS = 0;

   //Need to update the end of a start/skip/end that continues from the
   //previous line and regions that have "keepend".
   if (current_state.len > 0) {
      syn_update_ends(TRUE);
      check_state_ends();
   }

   nextMatchIdx = -1;
   ++current_line_id;
}

//Check for items in the stack that need their end updated.
//When "startofline" is TRUE the last item is always updated.
//When "startofline" is FALSE the item with "keepend" is forcefully updated.
private void
syn_update_ends(int startofline) {
   StateItem   *currStateItem;
   int      i;
   int      seen_keepend;

   if (startofline) {
      // Check for a match carried over from a previous line with a
      // contained region.  The match ends as soon as the region ends.
      for (i = 0; i < current_state.len; ++i) {
         currStateItem = &CUR_STATE(i);
         if (currStateItem->si_idx >= 0
             && (SYN_ITEMS(synBlockS)[currStateItem->si_idx]).sp_type == SPTYPE_MATCH
             && currStateItem->matchEndPos.lnum < currLnumS
         ) {
            currStateItem->si_flags |= HL_MATCHCONT;
            currStateItem->matchEndPos.lnum = 0;
            currStateItem->matchEndPos.col = 0;
            currStateItem->hiEndPos = currStateItem->matchEndPos;
            currStateItem->si_ends = TRUE;
         }
      }
   }

   //Need to update the end of a start/skip/end that continues from the previous line. And regions 
   //that have "keepend", because they may influence contained items.  If we've just removed 
   //"extend" (startofline == 0) then we should update ends of normal regions contained inside 
   //"keepend" because "extend" could have extended these "keepend" regions as well as contained 
   //normal regions. Then check for items ending in column 0.
   i = current_state.len - 1;
   if (keepend_level >= 0)
   for ( ; i > keepend_level; --i)
      if (CUR_STATE(i).si_flags & HL_EXTEND)
         break;

   seen_keepend = FALSE;
   for ( ; i < current_state.len; ++i) {
      currStateItem = &CUR_STATE(i);
      if ((currStateItem->si_flags & HL_KEEPEND)
                || (seen_keepend && !startofline)
                || (i == current_state.len - 1 && startofline))
      {
         currStateItem->hiStartPos.col = 0;   // start highl. in col 0
         currStateItem->hiStartPos.lnum = currLnumS;

         if (!(currStateItem->si_flags & HL_MATCHCONT))
            update_si_end(currStateItem, (int)currColS, !startofline);

         if (!startofline && (currStateItem->si_flags & HL_KEEPEND))
            seen_keepend = TRUE;
      }
   }
    check_keepend();
}

/////////////////////////////////////////
// Handling of the state stack cache.

//
//EXPLANATION OF THE SYNTAX STATE STACK CACHE
//
//To speed up syntax highlighting, the state stack for the start of some lines is cached. These 
//entries can be used to start parsing at that point.
//The stack is kept in array[] for each buffer.  There is a list of valid entries. first points 
//to the first one, then follow next. The entries are sorted on line number. The first entry 
//is often for line 2 (line 1 always starts with an empty stack). There is also a list for free 
//entries. This construction is used to avoid having to allocate and free memory blocks too often.
//
//When making changes to the buffer, this is logged in b_mod_*.  When calling drawUpdateScreen() to 
//update the display, it will call syn_stack_apply_changes() for each displayed buffer to adjust 
//the cached entries.  The entries which are inside the changed area are removed, because they must
//be recomputed. Entries below the changed have their line number adjusted for deleted/inserted 
//lines, and have their invalidatingChangeLnum set to indicate that a check must be made if the changed 
//lines would change the cached entry.
//
//When later displaying lines, an entry is stored for each line. Displayed lines are likely to be 
//displayed again, in which case the state at the start of the line is needed. For undisplayed 
//lines, an entry is stored for every so many lines. These entries will be used e.g., when 
//scrolling backwards. The distance between entries depends on the number of lines in the buffer.
//For small buffers the distance is fixed at SST_DIST, for large buffers there is a fixed
//number of entries SST_MAX_ENTRIES, and the distance is computed.

//Used when syntax items changed to force resyncing everywhere.
void
synFreeBlock(SyntaxBlock *block) {
   SyntaxState   *p;

   if (block->array == NULL)
      return;

   FOR_ALL_SYNSTATES(block, p) {
      clear_syn_state(p);
   } 
   EE_CLEAR(block->array);
   block->first = NULL;
   block->len = 0;
}

//Allocate the syntax state stack for synBookS when needed.
//If the number of entries in array[] is much too big or a bit too small, reallocate it.
//Also used to allocate array[] for the first time.
private void
syn_stack_alloc(void) {
   long   len;
   SyntaxState   *to, *from;
   SyntaxState   *state;

   len = synBookS->mem.lineCount / SST_DIST + visibleRowsG * 2;
   if (len < SST_MIN_ENTRIES)
      len = SST_MIN_ENTRIES;
   ei (len > SST_MAX_ENTRIES)
      len = SST_MAX_ENTRIES;
   if (synBlockS->len > len * 2 || synBlockS->len < len) {
      // Allocate 50% too much, to avoid reallocating too often.
      len = synBookS->mem.lineCount;
      len = (len + len / 2) / SST_DIST + visibleRowsG * 2;
      if (len < SST_MIN_ENTRIES)
          len = SST_MIN_ENTRIES;
      ei (len > SST_MAX_ENTRIES)
          len = SST_MAX_ENTRIES;

      if (synBlockS->array != NULL) {
         // When shrinking the array, cleanup the existing stack.
         // Make sure that all valid entries fit in the new array.
         while (synBlockS->len - synBlockS->freeCount + 2 > len && syn_stack_cleanup())
            {}
         if (len < synBlockS->len - synBlockS->freeCount + 2)
            len = synBlockS->len - synBlockS->freeCount + 2;
      }

      state = ALLOC_CLEAR_MULT(SyntaxState, len);
      if (!state)   // out of memory!
         return;

      to = state - 1;
      if (synBlockS->array) {
         // Move the states from the old array to the new one.
         for (from = synBlockS->first; from != NULL; from = from->next) {
            ++to;
            *to = *from;
            to->next = to + 1;
         }
      }
      if (to != state - 1) {
          to->next = NULL;
          synBlockS->first = state;
          synBlockS->freeCount = len - (int)(to - state) - 1;
      } else {
          synBlockS->first = NULL;
          synBlockS->freeCount = len;
      }

      // Create the list of free entries.
      synBlockS->firstFree = to + 1;
      while (++to < state + len)
         to->next = to + 1;
      (state + len - 1)->next = NULL;

      eeglFree(synBlockS->array);
      synBlockS->array = state;
      synBlockS->len = len;
   }
}

//Check for changes in a buffer to affect stored syntax states.  Uses the b_mod_* fields.
//Called from drawUpdateScreen(), before screen is being updated, once for each displayed buffer.
void
syn_stack_apply_changes(Book* book) {
   Portal   *wp;

   syn_stack_apply_changes_block(&book->syntax, book);

   FOR_ALL_PORTALS(wp) {
      if ((wp->book == book) && (wp->ownSyntax != &book->syntax))
          syn_stack_apply_changes_block(wp->ownSyntax, book);
   }
}

private void
syn_stack_apply_changes_block(SyntaxBlock *block, Book *book) {
   SyntaxState   *p, *prev, *np;

   prev = NULL;
   for (p = block->first; p; ) {
      if (p->lnum + block->syncLinebreaks > book->needsRedrawTop) {
         LineNr n = p->lnum + book->lineCountDiff;
         if (n <= book->needsRedrawBott) {
            // this state is inside the changed area, remove it
            np = p->next;
            if (prev == NULL)
               block->first = np;
            else
               prev->next = np;
            syn_stack_free_entry(block, p);
            p = np;
            continue;
         }
         // This state is below the changed area.  Remember the line
         // that needs to be parsed before this entry can be made valid again.
         if (p->invalidatingChangeLnum != 0 && p->invalidatingChangeLnum > book->needsRedrawTop) {
            if (p->invalidatingChangeLnum + book->lineCountDiff > book->needsRedrawTop)
               p->invalidatingChangeLnum += book->lineCountDiff;
            else
               p->invalidatingChangeLnum = book->needsRedrawTop;
         }
         if (p->invalidatingChangeLnum == 0 || p->invalidatingChangeLnum < book->needsRedrawBott)
            p->invalidatingChangeLnum = book->needsRedrawBott;

         p->lnum = n;
      }
      prev = p;
      p = p->next;
   }
}

// Reduce the number of entries in the state stack for synBookS.
// Return TRUE if at least one entry was freed.
private int
syn_stack_cleanup(void) {
   SyntaxState   *p, *prev;
   DisplayTick   tick;
   int      above;
   int      dist;
   int      retval = FALSE;

   if (synBlockS->first == NULL)
      return retval;

   // Compute normal distance between non-displayed entries.
   if (synBlockS->len <= visibleRowsG)
      dist = 999999;
   else
      dist = synBookS->mem.lineCount / (synBlockS->len - visibleRowsG) + 1;

   /*
    * Go through the list to find the "tick" for the oldest entry that can
    * be removed.  Set "above" when the "tick" for the oldest entry is above
    * "lastDisplayTick" (the display tick wraps around).
    */
   tick = synBlockS->lastDisplayTick;
   above = FALSE;
   prev = synBlockS->first;
   for (p = prev->next; p != NULL; prev = p, p = p->next) {
      if (prev->lnum + dist > p->lnum) {
         if (p->tick > synBlockS->lastDisplayTick) {
            if (!above || p->tick < tick)
               tick = p->tick;
            above = TRUE;
         } ei (!above && p->tick < tick)
            tick = p->tick;
      }
   }

   // Go through the list to make the entries for the oldest tick at an interval of several lines.
   prev = synBlockS->first;
   for (p = prev->next; p != NULL; prev = p, p = p->next) {
      if (p->tick == tick && prev->lnum + dist > p->lnum) {
         // Move this entry from used list to free list
         prev->next = p->next;
         syn_stack_free_entry(synBlockS, p);
         p = prev;
         retval = TRUE;
      }
   }
   return retval;
}

//Free the allocated memory for a syn_state item. Move the entry into the free list.
private void
syn_stack_free_entry(SyntaxBlock *block, SyntaxState *p) {
   clear_syn_state(p);
   p->next = block->firstFree;
   block->firstFree = p;
   ++block->freeCount;
}

/*
 * Find an entry in the list of state stacks at or before "lnum".
 * Returns NULL when there is no entry or the first entry is after "lnum".
 */
private SyntaxState *
syn_stack_find_entry(LineNr lnum) {
   SyntaxState   *p, *prev;

   prev = NULL;
   for (p = synBlockS->first; p != NULL; prev = p, p = p->next) {
      if (p->lnum == lnum)
          return p;
      if (p->lnum > lnum)
          break;
   }
   return prev;
}

// Try saving the current state in array[].
// The current state must be valid for the start of the currLnumS line!
private SyntaxState *
store_current_state(void) {
   int      i;
   SyntaxState   *p;
   BufState   *bp;
   StateItem   *currStateItem;
   SyntaxState   *sp = syn_stack_find_entry(currLnumS);

   //If the current state contains a start or end pattern that continues
   //from the previous line, we can't use it.  Don't store it then.
   for (i = current_state.len - 1; i >= 0; --i) {
      currStateItem = &CUR_STATE(i);
      if (currStateItem->hiStartPos.lnum >= currLnumS
            || currStateItem->matchEndPos.lnum >= currLnumS
            || currStateItem->hiEndPos.lnum >= currLnumS
            || (currStateItem->si_end_idx
                && currStateItem->endPattEndPos.lnum >= currLnumS))
         break;
   }
   if (i >= 0) {
      if (sp) {
         // find "sp" in the list and remove it
         if (synBlockS->first == sp)
            // it's the first entry
            synBlockS->first = sp->next;
         else {
            // find the entry just before this one to adjust next
            FOR_ALL_SYNSTATES(synBlockS, p) {
               if (p->next == sp)
                  break;
            } 
            if (p)   // just in case
               p->next = sp->next;
         }
         syn_stack_free_entry(synBlockS, sp);
         sp = NULL;
      }
   } ei (sp == NULL || sp->lnum != currLnumS) {
      // Add a new entry
      // If no free items, cleanup the array first.
      if (synBlockS->freeCount == 0) {
          (void)syn_stack_cleanup();
          // "sp" may have been moved to the freelist now
          sp = syn_stack_find_entry(currLnumS);
      }
      // Still no free items?  Must be a strange problem...
      if (synBlockS->freeCount == 0)
         sp = NULL;
      else {
         // Take the first item from the free list and put it in the used
         // list, after *sp
         p = synBlockS->firstFree;
         synBlockS->firstFree = p->next;
         --synBlockS->freeCount;
         if (sp == NULL) {
            // Insert in front of the list
            p->next = synBlockS->first;
            synBlockS->first = p;
         } else {
            // insert in list after *sp
            p->next = sp->next;
            sp->next = p;
         }
         sp = p;
         sp->stacksize = 0;
         sp->lnum = currLnumS;
      }
   }
   if (sp) {
      // When overwriting an existing state stack, clear it first
      clear_syn_state(sp);
      sp->stacksize = current_state.len;
      if (current_state.len > SST_FIX_STATES) {
          // Need to clear it, might be something remaining from when the
          // length was less than SST_FIX_STATES.
          ga_init2(&sp->sst_union.arrayList, sizeof(BufState), 1);
          if (ga_grow(&sp->sst_union.arrayList, current_state.len) == FAIL)
         sp->stacksize = 0;
          else
         sp->sst_union.arrayList.len = current_state.len;
          bp = SYN_STATE_P(&(sp->sst_union.arrayList));
      } else
          bp = sp->sst_union.stack;
      for (i = 0; i < sp->stacksize; ++i) {
          bp[i].bs_idx = CUR_STATE(i).si_idx;
          bp[i].bs_flags = CUR_STATE(i).si_flags;
          bp[i].bs_extmatch = ref_extmatch(CUR_STATE(i).si_extmatch);
      }
      sp->next_flags = current_next_flags;
      sp->next_list = current_next_list;
      sp->tick = display_tick;
      sp->invalidatingChangeLnum = 0;
   }
   currentStateStoredS = true;
   return sp;
}

//Copy a state stack from "from" in array[] to current_state;
private void
load_current_state(SyntaxState *from) {
   int      i;
   BufState   *bp;

   clear_current_state();
   validate_current_state();
   keepend_level = -1;
   if (from->stacksize && ga_grow(&current_state, from->stacksize) == OK) {
      if (from->stacksize > SST_FIX_STATES)
         bp = SYN_STATE_P(&(from->sst_union.arrayList));
      else
         bp = from->sst_union.stack;
      for (i = 0; i < from->stacksize; ++i) {
         CUR_STATE(i).si_idx = bp[i].bs_idx;
         CUR_STATE(i).si_flags = bp[i].bs_flags;
         CUR_STATE(i).si_extmatch = ref_extmatch(bp[i].bs_extmatch);
         if (keepend_level < 0 && (CUR_STATE(i).si_flags & HL_KEEPEND))
            keepend_level = i;
         CUR_STATE(i).si_ends = FALSE;
         CUR_STATE(i).matchLnum = 0;
         if (CUR_STATE(i).si_idx >= 0)
            CUR_STATE(i).nextList = (SYN_ITEMS(synBlockS)[CUR_STATE(i).si_idx]).sp_next_list;
         else
            CUR_STATE(i).nextList = NULL;
         update_si_attr(i);
      }
      current_state.len = from->stacksize;
   }
   current_next_list = from->next_list;
   current_next_flags = from->next_flags;
   currLnumS = from->lnum;
}

// Compare saved state stack "*sp" with the current state. Return TRUE when they are equal.
private int
syn_stack_equal(SyntaxState *sp) {
   int      i, j;
   BufState   *bp;
   RegExternalMatch   *six, *bsx;

   // First a quick check if the stacks have the same size end nextlist.
   if (sp->stacksize != current_state.len || sp->next_list != current_next_list)
      return FALSE;

   // Need to compare all states on both stacks.
   if (sp->stacksize > SST_FIX_STATES)
      bp = SYN_STATE_P(&(sp->sst_union.arrayList));
   else
      bp = sp->sst_union.stack;

   for (i = current_state.len; --i >= 0; ) {
      // If the item has another index the state is different.
      if (bp[i].bs_idx != CUR_STATE(i).si_idx)
         break;
      if (bp[i].bs_extmatch == CUR_STATE(i).si_extmatch)
         continue;
      // When the extmatch pointers are different, the strings in them can
      // still be the same.  Check if the extmatch references are equal.
      bsx = bp[i].bs_extmatch;
      six = CUR_STATE(i).si_extmatch;
      // If one of the extmatch pointers is NULL the states are different.
      if (bsx == NULL || six == NULL)
         break;
      for (j = 0; j < NSUBEXP; ++j) {
         // Check each referenced match string. They must all be equal.
         if (bsx->matches[j] != six->matches[j]) {
            // If the pointer is different it can still be the same text.
            // Compare the strings, ignore case when the start item has the sp_ic flag set.
            if (bsx->matches[j] == NULL || six->matches[j] == NULL)
                break;
            if ((SYN_ITEMS(synBlockS)[CUR_STATE(i).si_idx]).sp_ic
               ? caseInsensitiveCompareMaxCol(bsx->matches[j], six->matches[j]) != 0
               : STRCMP(bsx->matches[j], six->matches[j]) != 0)
                break;
         }
      }
      if (j != NSUBEXP)
         break;
   }
   return i < 0 ? TRUE : FALSE;
}

// We stop parsing syntax above line "lnum".  If the stored state at or below this line depended on
// a change before it, it now depends on the line below the last parsed line.
// The portal looks like this:
//          line which changed
//          displayed line
//          displayed line
// lnum ->  line below window
void
syntax_end_parsing(Portal *wp, LineNr lnum) {
   SyntaxState   *sp;

   if (synBlockS != wp->ownSyntax)
      return;  // not the right window
   sp = syn_stack_find_entry(lnum);
   if (sp != NULL && sp->lnum < lnum)
      sp = sp->next;

   if (sp != NULL && sp->invalidatingChangeLnum != 0)
      sp->invalidatingChangeLnum = lnum;
}

// * End of handling of the state stack.
//////////////////////////////////////////

private void
invalidate_current_state(void) {
   clear_current_state();
   current_state.ga_itemsize = 0;   // mark current_state invalid
   current_next_list = NULL;
   keepend_level = -1;
}

private void
validate_current_state(void) {
   current_state.ga_itemsize = sizeof(StateItem);
   current_state.ga_growsize = 3;
}

//Return TRUE if the syntax at start of lnum changed since last time.
//This will only be called just after syntGetDeco() for the previous
//line, to check if the next line needs to be redrawn too.
int
syntax_check_changed(LineNr lnum) {
   int      retval = TRUE;
   SyntaxState   *sp;

    /*
     * Check the state stack when:
     * - lnum is just below the previously syntaxed line.
     * - lnum is not before the lines with saved states.
     * - lnum is not past the lines with saved states.
     * - lnum is at or before the last changed line.
     */
   if (VALID_STATE(&current_state) && lnum == currLnumS + 1) {
      sp = syn_stack_find_entry(lnum);
      if (sp != NULL && sp->lnum == lnum) {
         // finish the previous line (needed when not all of the line was drawn)
         (void)syn_finish_line(FALSE);

         // Compare the current state with the previously saved state of the line.
         if (syn_stack_equal(sp))
            retval = FALSE;

         // Store the current state in array[] for later use.
         ++currLnumS;
         (void)store_current_state();
      }
   }

   return retval;
}

// Finish the current line.
// This doesn't return any attributes, it only gets the state at the end of
// the line.  It can start anywhere in the line, as long as the current state is valid.
private int
syn_finish_line(int       syncing) {     // called for syncing
   StateItem   *currStateItem;
   ColNr   prev_current_col;

   while (!currentFinishedS) {
      (void)getCurrentDeco(syncing, false, false);
      // When syncing, and found some item, need to check the item.
      if (syncing && current_state.len) {
         // Check for match with sync item.
         currStateItem = &CUR_STATE(current_state.len - 1);
         if (currStateItem->si_idx >= 0
                && (SYN_ITEMS(synBlockS)[currStateItem->si_idx].sp_flags 
                   & (HL_SYNC_HERE|HL_SYNC_THERE)))
            return TRUE;

         // getCurrentDeco() will have skipped the check for an item that ends here, need to do 
         // that now.  Be careful not to go past the ZERO.
         prev_current_col = currColS;
         if (syn_getcurline()[currColS] != ZERO)
            ++currColS;
         check_state_ends();
         currColS = prev_current_col;
      }
      ++currColS;
    }
    return FALSE;
}

//Return hilite decorations for next character. Must first call syntaxStartLine() once for the line.
//"col" is normally 0 for the first use in a line, and increments by one each
//time. It's allowed to skip characters and to stop before the end of the
//line, but not going back within the line (only a "col" after a previously used column is allowed).
Decoration
syntGetDeco(
   ColNr col,
   int keep_state   // keep state of char at "col"
){

   // check for out of memory situation
   if (!synBlockS->array)
      return EMPTY_DECO;

   // After 'synmaxcol' the attribute is always zero.
   if (col >= SYNTAX_MAX_COL) {
      clear_current_state();
      current_id = 0;
      current_trans_id = 0;
      current_flags = 0;
      current_seqnr = 0;
      return EMPTY_DECO;
   }

   // Make sure current_state is valid
   if (INVALID_STATE(&current_state))
      validate_current_state();

   // Skip from the current column to "col", get the attributes for "col".
   Decoration deco;
   while (currColS <= col) {
      deco = getCurrentDeco(false, true, currColS == col ? keep_state : false);
      ++currColS;
   }

   return deco;
}

// Get syntax decorations for currLnumS, currColS.
private Decoration
getCurrentDeco(
   Boole syncing,      // When 1: called for syncing
   Boole displaying,      // result will be displayed
   Boole keep_state      // keep syntax stack afterwards
){
   Short      hiId;
   PosNoVirt   endpos;      // was: Byte *endp;
   PosNoVirt   hl_startpos;   // was: int hl_startcol;
   PosNoVirt   hl_endpos;
   PosNoVirt   eos_pos;   // end-of-start match (start region)
   PosNoVirt   eoe_pos;   // end-of-end pattern
   int      end_idx;   // group ID for end pattern
   int      idx;
   SyntaxPattern   *spp;
   StateItem   *currStateItem, *sip = NULL;
   int      startcol;
   int      endcol;
   long   flags;
   int      cchar;
   Short* next_list;
   int      found_match;          // found usable match
   static int   try_next_column = FALSE;    // must try in next col
   int      do_keywords;
   RegMultilineMatch   regmatch;
   PosNoVirt   pos;
   int      lc_col;
   RegExternalMatch *cur_extmatch = NULL;
   Byte bookKeywordChars[32];  // chartab array for syn iskyeyword
   CS line; // current line.  NOTE: becomes invalid after looking for a pattern match!

   // variables for zero-width matches that have a "nextgroup" argument
   int      keep_next_list;
   int      zero_width_next_list = FALSE;
   ArrayList   zero_width_next_ga;

   // No character, no attributes! Past end of line?
   // Do try matching with an empty line (could be the start of a region).
   line = syn_getcurline();
   if (line[currColS] == ZERO && currColS != 0) {
      // If we found a match after the last column, use it.
      if (nextMatchIdx >= 0 && nextMatchCol >= (int)currColS && nextMatchCol != MAXCOL)
         (void)push_next_match(NULL);

      currentFinishedS = true;
      currentStateStoredS = false;
      return EMPTY_DECO;
   }

   // if the current or next character is ZERO, we will finish the line now
   if (line[currColS] == ZERO || line[currColS + 1] == ZERO) {
      currentFinishedS = true;
      currentStateStoredS = false;
   }

   // When in the previous column there was a match but it could not be used
   // (empty match or already matched in this column) need to try again in the next column.
   if (try_next_column) {
      nextMatchIdx = -1;
      try_next_column = FALSE;
   }

   // Only check for keywords when not syncing and there are some.
   do_keywords = !syncing 
      && (synBlockS->keywords.count > 0 || synBlockS->keywordsIgnoreCase.count > 0);

   // Init the list of zero-width matches with a nextlist.  This is used to
   // avoid matching the same item in the same position twice.
   ga_init2(&zero_width_next_ga, sizeof(int), 10);

   // use syntax iskeyword option
   save_chartab(bookKeywordChars);

   // Repeat matching keywords and patterns, to find contained items at the
   // same column.  This stops when there are no extra matches at the current column.
   do {
      found_match = FALSE;
      keep_next_list = FALSE;
      hiId = SHORT;

      // 1. Check for a current state. Only when there is no current state, or if the current state 
      // may contain other things, we need to check for keywords and patterns. Always need to check 
      // for contained items if some item has the "containedin" argument (takes extra time!).
      currStateItem = current_state.len != 0 ? &CUR_STATE(current_state.len - 1) : null;

      if (synBlockS->b_syn_containedin || currStateItem == NULL || currStateItem->si_containsHiId) {
         //2. Check for keywords, if on a keyword char after a non-keyword char. 
         //Don't do this when syncing.
         if (do_keywords) {
            line = syn_getcurline();
            if (eeIsWordPtr_buf(line + currColS, synBookS)
                  && (currColS == 0
                    || !eeIsWordPtr_buf(line + currColS - 1
                        - mb_head_off(line, line + currColS - 1), synBookS))
            ){
               hiId = check_keyword_id(
                  line, (int)currColS, &endcol, &flags, OUT &next_list, currStateItem, &cchar
               );
               if (hiId != SHORT) {
                  if (push_current_state(KEYWORD_IDX) == OK) {
                     currStateItem = &CUR_STATE(current_state.len - 1);
                     currStateItem->matchStartCol = currColS;
                     currStateItem->hiStartPos.lnum = currLnumS;
                     currStateItem->hiStartPos.col = 0;   // starts right away
                     currStateItem->matchEndPos.lnum = currLnumS;
                     currStateItem->matchEndPos.col = endcol;
                     currStateItem->hiEndPos.lnum = currLnumS;
                     currStateItem->hiEndPos.col = endcol;
                     currStateItem->si_ends = TRUE;
                     currStateItem->si_end_idx = 0;
                     currStateItem->si_flags = flags;
                     currStateItem->hiId = hiId;
                     currStateItem->transparentHiId = hiId;
                     if (flags & HL_TRANSP) {
                        if (current_state.len < 2) {
                           currStateItem->flags = 0;
                           currStateItem->transparentHiId = 0;
                        } else {
                           currStateItem->flags = CUR_STATE( current_state.len - 2).flags;
                           currStateItem->transparentHiId = CUR_STATE( current_state.len - 2).transparentHiId;
                        }
                     } else
                        currStateItem->flags = decorationsG[hiId].flags;
                     currStateItem->si_containsHiId = NULL;
                     currStateItem->nextList = next_list;
                     check_keepend();
                  } else
                     eeglFree(next_list);
               }
            }
         }

         // 3. Check for patterns (only if no keyword found).
         if (hiId == 0 && synBlockS->syntaxPatterns.len) {
            // If we didn't check for a match yet, or we are past it, seek any match with a pattern
            if (nextMatchIdx < 0 || nextMatchCol < (int)currColS) {
               // Check all relevant patterns for a match at this position.  This is complicated, 
               // because matching with a pattern takes quite a bit of time, thus we want to
               // avoid doing it when it's not needed.
               nextMatchIdx = 0;      // no match in this line yet
               nextMatchCol = MAXCOL;
               for (idx = synBlockS->syntaxPatterns.len; --idx >= 0; ) {
                  spp = &(SYN_ITEMS(synBlockS)[idx]);
                  if (      spp->syncing == syncing
                     && (displaying || !(spp->sp_flags & HL_DISPLAY))
                     && (spp->sp_type == SPTYPE_MATCH
                         || spp->sp_type == SPTYPE_START)
                     && (current_next_list != NULL
                         ? in_id_list(NULL, current_next_list, &spp->syntax, 0)
                         : (currStateItem == NULL
                        ? !(spp->sp_flags & HL_CONTAINED)
                        : in_id_list(currStateItem,
                            currStateItem->si_containsHiId, &spp->syntax,
                            spp->sp_flags))))
                  {
                     int r;

                     // If we already tried matching in this line, and
                     // there isn't a match before nextMatchCol, skip this item.
                     if (spp->sp_line_id == current_line_id && spp->sp_startcol >= nextMatchCol)
                        continue;
                     spp->sp_line_id = current_line_id;

                     lc_col = currColS - spp->sp_offsets[SPO_LC_OFF];
                     if (lc_col < 0)
                        lc_col = 0;

                     regmatch.rmm_ic = spp->sp_ic;
                     regmatch.regprog = spp->prog;
                     r = syn_regexec(
                           &regmatch, currLnumS, (ColNr)lc_col, IF_SYN_TIME(&spp->sp_time)
                     );
                     spp->prog = regmatch.regprog;
                     if (!r) {
                        // no match in this line, try another one
                        spp->sp_startcol = MAXCOL;
                        continue;
                     }

                     // Compute the first column of the match.
                     syn_add_start_off(&pos, &regmatch, spp, SPO_MS_OFF, -1);
                     if (pos.lnum > currLnumS) {
                        // must have used end of match in a next line, we can't handle that
                        spp->sp_startcol = MAXCOL;
                        continue;
                     }
                     startcol = pos.col;

                     // remember the next column where this pattern matches in the current line
                     spp->sp_startcol = startcol;

                     // If a previously found match starts at a lower column number, don't use 
                     // this one
                     if (startcol >= nextMatchCol)
                        continue;

                     // If we matched this pattern at this position before, skip it.  Must retry 
                     // in the next column, because it may match from there.
                     if (did_match_already(idx, &zero_width_next_ga)) {
                        try_next_column = TRUE;
                        continue;
                     }

                     endpos.lnum = regmatch.endpos[0].lnum;
                     endpos.col = regmatch.endpos[0].col;

                     // Compute the highlight start.
                     syn_add_start_off(&hl_startpos, &regmatch, spp, SPO_HS_OFF, -1);

                     // Compute the region start. Default is to use the end of the match.
                     syn_add_end_off(&eos_pos, &regmatch, spp, SPO_RS_OFF, 0);

                     // Grab the external submatches before they get overwritten.  Reference count 
                     // doesn't change.
                     unref_extmatch(cur_extmatch);
                     cur_extmatch = re_extmatch_out;
                     re_extmatch_out = NULL;

                     flags = 0;
                     eoe_pos.lnum = 0;   // avoid warning
                     eoe_pos.col = 0;
                     end_idx = 0;
                     hl_endpos.lnum = 0;

                     // For a "oneline" the end must be found in the same line too.  Search for 
                     // it after the end of the match with the start pattern.  Set the
                     // resulting end positions at the same time.
                     if (spp->sp_type == SPTYPE_START && (spp->sp_flags & HL_ONELINE)) {
                        PosNoVirt   startpos;

                        startpos = endpos;
                        find_endpos(
                           idx, &startpos, &endpos, &hl_endpos, &flags, &eoe_pos, &end_idx, 
                           cur_extmatch
                        );
                        if (endpos.lnum == 0)
                           continue;       // not found
                     }

                     // For a "match" the size must be > 0 after the
                     // end offset needs has been added.  Except when syncing.
                     ei (spp->sp_type == SPTYPE_MATCH) {
                        syn_add_end_off(&hl_endpos, &regmatch, spp, SPO_HE_OFF, 0);
                        syn_add_end_off(&endpos, &regmatch, spp, SPO_ME_OFF, 0);
                        if (endpos.lnum == currLnumS && (int)endpos.col + syncing < startcol) {
                           // If an empty string is matched, may need
                           // to try matching again at next column.
                           if (regmatch.startpos[0].col == regmatch.endpos[0].col)
                              try_next_column = TRUE;
                           continue;
                        }
                     }

                     // keep the best match so far in next_match_*
                     // Highlighting must start after startpos and end before endpos.
                     if (hl_startpos.lnum == currLnumS && (int)hl_startpos.col < startcol)
                        hl_startpos.col = startcol;
                     limit_pos_zero(&hl_endpos, &endpos);

                     nextMatchIdx = idx;
                     nextMatchCol = startcol;
                     next_match_m_endpos = endpos;
                     next_match_h_endpos = hl_endpos;
                     next_match_h_startpos = hl_startpos;
                     next_match_flags = flags;
                     next_match_eos_pos = eos_pos;
                     next_match_eoe_pos = eoe_pos;
                     next_match_end_idx = end_idx;
                     unref_extmatch(next_match_extmatch);
                     next_match_extmatch = cur_extmatch;
                     cur_extmatch = NULL;
                  }
               }
            }

            // If we found a match at the current column, use it.
            if (nextMatchIdx >= 0 && nextMatchCol == (int)currColS) {

               // When a zero-width item matched which has a nextgroup,
               // don't push the item but set nextgroup.
               SyntaxPattern* lspp = &(SYN_ITEMS(synBlockS)[nextMatchIdx]);
               if (next_match_m_endpos.lnum == currLnumS
                   && next_match_m_endpos.col == currColS
                   && lspp->sp_next_list
               ){
                  current_next_list = lspp->sp_next_list;
                  current_next_flags = lspp->sp_flags;
                  keep_next_list = TRUE;
                  zero_width_next_list = TRUE;

                  // Add the index to a list, so that we can check later that we don't match it 
                  // again (and cause an endless loop).
                  if (ga_grow(&zero_width_next_ga, 1) == OK) {
                     ((int *)(zero_width_next_ga.c))[zero_width_next_ga.len] = nextMatchIdx;
                     zero_width_next_ga.len++;
                  }
                  nextMatchIdx = -1;
               } else
                  currStateItem = push_next_match(currStateItem);
               found_match = TRUE;
            }
         }
      }

      // Handle searching for nextgroup match.
      if (current_next_list && !keep_next_list) {
         // If a nextgroup was not found, continue looking for one if:
         // - this is an empty line and the "skipempty" option was given
         // - we are on white space and the "skipwhite" option was given
         if (!found_match) {
            line = syn_getcurline();
            if (((current_next_flags & HL_SKIPWHITE) != 0
                   && SPACE_OR_TAB(line[currColS]))
                  || ((current_next_flags & HL_SKIPEMPTY) != 0 && *line == ZERO)
            )
                break;
         }

         // If a nextgroup was found: Use it, and continue looking for contained matches.
         // If a nextgroup was not found: Continue looking for a normal match.
         // When did set current_next_list for a zero-width item and no match was found don't loop 
         // (would get stuck).
         current_next_list = NULL;
         nextMatchIdx = -1;
         if (!zero_width_next_list)
            found_match = TRUE;
      }

   } while (found_match);

   restoreKeywordChars(bookKeywordChars);

   // Use decorations from the current state, if within its highlighting.
   // If not, use decorations from the current-but-one state, etc.
   Decoration currDeco = EMPTY_DECO;
   current_id = 0;
   current_trans_id = 0;
   current_flags = 0;
   current_seqnr = 0;
   if (currStateItem) {
      for (idx = current_state.len - 1; idx >= 0; --idx)    {
         sip = &CUR_STATE(idx);
         if ((currLnumS > sip->hiStartPos.lnum
            || (currLnumS == sip->hiStartPos.lnum && currColS >= sip->hiStartPos.col))
             && (sip->hiEndPos.lnum == 0
               || currLnumS < sip->hiEndPos.lnum
               || (currLnumS == sip->hiEndPos.lnum && currColS < sip->hiEndPos.col))
         ){
            currDeco = sip->hiId < SHORT ? decorationsG[sip->hiId] : EMPTY_DECO;
            current_id = sip->hiId;
            current_trans_id = sip->transparentHiId;
            break;
         }
      }

      //Check for end of current state (and the states before it) at the next column. Don't do 
      //this for syncing, because we would miss a single character match.
      //First check if the current state ends at the current column.  It may be for an empty 
      //match and a containing item might end in the current column.
      if (!syncing && !keep_state) {
         check_state_ends();
         if (current_state.len > 0 && syn_getcurline()[currColS] != ZERO) {
            ++currColS;
            check_state_ends();
            --currColS;
         }
      }
   }   // nextgroup ends at end of line, unless "skipnl" or "skipempty" present
   if (current_next_list
          && (line = syn_getcurline())[currColS] != ZERO
          && line[currColS + 1] == ZERO
          && (current_next_flags & (HL_SKIPNL | HL_SKIPEMPTY)) == 0
   )
      current_next_list = NULL;

   if (zero_width_next_ga.len > 0)
      ga_clear(&zero_width_next_ga);

   // No longer need external matches.  But keep next_match_extmatch.
   unref_extmatch(re_extmatch_out);
   re_extmatch_out = NULL;
   unref_extmatch(cur_extmatch);

   return currDeco;
}


// Check if we already matched pattern "idx" at the current column.
private int
did_match_already(int idx, ArrayList *gap) {
   for (int i = current_state.len; --i >= 0; ) {
      if (CUR_STATE(i).matchStartCol == (int)currColS
            && CUR_STATE(i).matchLnum == (int)currLnumS
            && CUR_STATE(i).si_idx == idx)
         return TRUE;
   } 

   // Zero-width matches with a nextgroup argument are not put on the syntax
   // stack, and can only be matched once anyway.
   for (int i = gap->len; --i >= 0; ) {
      if (((int *)(gap->c))[i] == idx)
         return TRUE;
   } 

   return FALSE;
}

// Push the next match onto the stack.
private StateItem *
push_next_match(StateItem *currStateItem) {
   SyntaxPattern   *spp;

   spp = &(SYN_ITEMS(synBlockS)[nextMatchIdx]);

   // Push the item into current_state stack;
   if (push_current_state(nextMatchIdx) == OK) {
      // If it's a start-skip-end type that crosses lines, figure out how
      // much it continues in this line.  Otherwise just fill in the length.
      currStateItem = &CUR_STATE(current_state.len - 1);
      currStateItem->hiStartPos = next_match_h_startpos;
      currStateItem->matchStartCol = currColS;
      currStateItem->matchLnum = currLnumS;
      currStateItem->si_flags = spp->sp_flags;
      currStateItem->nextList = spp->sp_next_list;
      currStateItem->si_extmatch = ref_extmatch(next_match_extmatch);
      if (spp->sp_type == SPTYPE_START && !(spp->sp_flags & HL_ONELINE)) {
         // Try to find the end pattern in the current line
         update_si_end(currStateItem, (int)(next_match_m_endpos.col), TRUE);
         check_keepend();
      } else {
         currStateItem->matchEndPos = next_match_m_endpos;
         currStateItem->hiEndPos = next_match_h_endpos;
         currStateItem->si_ends = TRUE;
         currStateItem->si_flags |= next_match_flags;
         currStateItem->endPattEndPos = next_match_eoe_pos;
         currStateItem->si_end_idx = next_match_end_idx;
      }
      if (keepend_level < 0 && (currStateItem->si_flags & HL_KEEPEND))
         keepend_level = current_state.len - 1;
      check_keepend();
      update_si_attr(current_state.len - 1);

      //If the start pattern has another highlight group, push another item
      //on the stack for the start pattern.
      if (      spp->sp_type == SPTYPE_START
         && spp->patternHiId != 0
         && push_current_state(nextMatchIdx) == OK
      ) {
         currStateItem = &CUR_STATE(current_state.len - 1);
         currStateItem->hiStartPos = next_match_h_startpos;
         currStateItem->matchStartCol = currColS;
         currStateItem->matchLnum = currLnumS;
         currStateItem->matchEndPos = next_match_eos_pos;
         currStateItem->hiEndPos = next_match_eos_pos;
         currStateItem->si_ends = TRUE;
         currStateItem->si_end_idx = 0;
         currStateItem->si_flags = HL_MATCH;
         currStateItem->nextList = NULL;
         check_keepend();
         update_si_attr(current_state.len - 1);
      }
   }

   nextMatchIdx = -1;   // try other match next time

   return currStateItem;
}

// Check for end of current state (and the states before it).
private void
check_state_ends(void) {
   StateItem   *currStateItem;
   int      had_extend;

   currStateItem = &CUR_STATE(current_state.len - 1);
   for (;;) {
      if (currStateItem->si_ends
         && (currStateItem->matchEndPos.lnum < currLnumS
             || (currStateItem->matchEndPos.lnum == currLnumS
            && currStateItem->matchEndPos.col <= currColS)))
      {
         //If there is an end pattern group ID, highlight the end pattern
         //now. No need to pop the current item from the stack.
         //Only do this if the end pattern continues beyond the current position.
         if (currStateItem->si_end_idx
             && (currStateItem->endPattEndPos.lnum > currLnumS
            || (currStateItem->endPattEndPos.lnum == currLnumS
                && currStateItem->endPattEndPos.col > currColS))
         ){
            currStateItem->si_idx = currStateItem->si_end_idx;
            currStateItem->si_end_idx = 0;
            currStateItem->matchEndPos = currStateItem->endPattEndPos;
            currStateItem->hiEndPos = currStateItem->endPattEndPos;
            currStateItem->si_flags |= HL_MATCH;
            update_si_attr(current_state.len - 1);

            // nextgroup= should not match in the end pattern
            current_next_list = NULL;

            // what matches next may be different now, clear it
            nextMatchIdx = 0;
            nextMatchCol = MAXCOL;
            break;
         }

         // handle next_list, unless at end of line and no "skipnl" or "skipempty"
         current_next_list = currStateItem->nextList;
         current_next_flags = currStateItem->si_flags;
         if ((current_next_flags & (HL_SKIPNL | HL_SKIPEMPTY)) == 0
                && syn_getcurline()[currColS] == ZERO)
            current_next_list = NULL;

         // When the ended item has "extend", another item with
         // "keepend" now needs to check for its end.
         had_extend = (currStateItem->si_flags & HL_EXTEND);

         pop_current_state();

         if (current_state.len == 0)
            break;

         if (had_extend && keepend_level >= 0) {
            syn_update_ends(FALSE);
            if (current_state.len == 0)
               break;
         }

         currStateItem = &CUR_STATE(current_state.len - 1);

         // Only for a region the search for the end continues after
         // the end of the contained item.  If the contained match
         // included the end-of-line, break here, the region continues.
         // Don't do this when:
         //  - "keepend" is used for the contained item
         //  - not at the end of the line (could be end="x$"me=e-1).
         //  - "excludenl" is used (HL_HAS_EOL won't be set)
         if (currStateItem->si_idx >= 0
             && SYN_ITEMS(synBlockS)[currStateItem->si_idx].sp_type == SPTYPE_START
             && !(currStateItem->si_flags & (HL_MATCH | HL_KEEPEND))
         ){
            update_si_end(currStateItem, (int)currColS, TRUE);
            check_keepend();
            if ((current_next_flags & HL_HAS_EOL) != 0
                  && keepend_level < 0
                  && syn_getcurline()[currColS] == ZERO)
               break;
         }
      } else
         break;
    }
}

// Update an entry in the current_state stack for a match or region. This fills in deco, 
// nextList and si_containsHiId.
private void
update_si_attr(int idx) {
   StateItem   *sip = &CUR_STATE(idx);
   SyntaxPattern   *spp;

   // This should not happen...
   if (sip->si_idx < 0)
      return;

   spp = &(SYN_ITEMS(synBlockS)[sip->si_idx]);
   if (sip->si_flags & HL_MATCH)
      sip->hiId = spp->patternHiId;
   else
      sip->hiId = spp->syntax.hiId;
   sip->flags = getDecoFlags(sip->hiId);
   sip->transparentHiId = sip->hiId;
   if (sip->si_flags & HL_MATCH)
      sip->si_containsHiId = NULL;
   else
      sip->si_containsHiId = spp->sp_containsHiId;

   //For transparent items, take deco from outer item. Also take containsHiId, if there is none.
   //Don't do this for the matchgroup of a start or end pattern.
   if ((spp->sp_flags & HL_TRANSP) && !(sip->si_flags & HL_MATCH)) {
      if (idx == 0) {
         sip->flags = 0;
         sip->transparentHiId = 0;
         if (sip->si_containsHiId == NULL)
            sip->si_containsHiId = ID_LIST_ALL;
      } else {
         sip->flags = CUR_STATE(idx - 1).flags;
         sip->transparentHiId = CUR_STATE(idx - 1).transparentHiId;
         if (sip->si_containsHiId == NULL) {
            sip->si_flags |= HL_TRANS_CONT;
            sip->si_containsHiId = CUR_STATE(idx - 1).si_containsHiId;
         }
      }
   }
}

//Check the current stack for patterns with "keepend" flag.
//Propagate the match-end to contained items, until a "skipend" item is found.
private void
check_keepend(void) {
   int      i;
   PosNoVirt   maxpos;
   PosNoVirt   maxpos_h;
   StateItem   *sip;

   // This check can consume a lot of time; only do it from the level with a keepend
   if (keepend_level < 0)
      return;

   //Find the last index of an "extend" item.  "keepend" items before that
   //won't do anything.  If there is no "extend" item "i" will be
   //"keepend_level" and all "keepend" items will work normally.
   for (i = current_state.len - 1; i > keepend_level; --i) {
      if (CUR_STATE(i).si_flags & HL_EXTEND)
         break;
   } 

   maxpos.lnum = 0;
   maxpos.col = 0;
   maxpos_h.lnum = 0;
   maxpos_h.col = 0;
   for ( ; i < current_state.len; ++i) {
      sip = &CUR_STATE(i);
      if (maxpos.lnum != 0) {
         limit_pos_zero(&sip->matchEndPos, &maxpos);
         limit_pos_zero(&sip->hiEndPos, &maxpos_h);
         limit_pos_zero(&sip->endPattEndPos, &maxpos);
         sip->si_ends = TRUE;
      }
      if (sip->si_ends && (sip->si_flags & HL_KEEPEND)) {
         if (maxpos.lnum == 0
                || maxpos.lnum > sip->matchEndPos.lnum
                || (maxpos.lnum == sip->matchEndPos.lnum
               && maxpos.col > sip->matchEndPos.col))
            maxpos = sip->matchEndPos;
         if (maxpos_h.lnum == 0
                || maxpos_h.lnum > sip->hiEndPos.lnum
                || (maxpos_h.lnum == sip->hiEndPos.lnum
               && maxpos_h.col > sip->hiEndPos.col))
            maxpos_h = sip->hiEndPos;
      }
   }
}

//Update an entry in the current_state stack for a start-skip-end pattern.
//This finds the end of the current item, if it's in the current line.
//
//Return the flags for the matched END.
private void
update_si_end(
   StateItem   *sip,
   int  startcol,   // where to start searching for the end
   Boole force)       // when TRUE overrule a previous end
{
   PosNoVirt   startpos;
   PosNoVirt   endpos;
   PosNoVirt   hl_endpos;
   PosNoVirt   end_endpos;
   int      end_idx;

   // return quickly for a keyword
   if (sip->si_idx < 0)
      return;

   // Don't update when it's already done.  Can be a match of an end pattern
   // that started in a previous line.  Watch out: can also be a "keepend"
   // from a containing item.
   if (!force && sip->matchEndPos.lnum >= currLnumS)
      return;

   // We need to find the end of the region.  It may continue in the next line.
   end_idx = 0;
   startpos.lnum = currLnumS;
   startpos.col = startcol;
   find_endpos(sip->si_idx, &startpos, &endpos, &hl_endpos,
         &(sip->si_flags), &end_endpos, &end_idx, sip->si_extmatch);

   if (endpos.lnum == 0) {
      // No end pattern matched.
      if (SYN_ITEMS(synBlockS)[sip->si_idx].sp_flags & HL_ONELINE) {
         // a "oneline" never continues in the next line
         sip->si_ends = TRUE;
         sip->matchEndPos.lnum = currLnumS;
         sip->matchEndPos.col = syn_getcurline_len();
      } else {
         // continues in the next line
         sip->si_ends = FALSE;
         sip->matchEndPos.lnum = 0;
      }
      sip->hiEndPos = sip->matchEndPos;
   } else {
   // match within this line
   sip->matchEndPos = endpos;
   sip->hiEndPos = hl_endpos;
   sip->endPattEndPos = end_endpos;
   sip->si_ends = TRUE;
   sip->si_end_idx = end_idx;
    }
}

//Add a new state to the current state stack. It is cleared and the index set to "idx".
//Return FAIL if it's not possible (out of memory).
private int
push_current_state(int idx) {
   if (ga_grow(&current_state, 1) == FAIL)
      return FAIL;
   CLEAR_POINTER(&CUR_STATE(current_state.len));
   CUR_STATE(current_state.len).si_idx = idx;
   ++current_state.len;
   return OK;
}

// Remove a state from the current_state stack.
private void
pop_current_state(void) {
   if (current_state.len) {
      unref_extmatch(CUR_STATE(current_state.len - 1).si_extmatch);
      --current_state.len;
   }
   // after the end of a pattern, try matching a keyword or pattern
   nextMatchIdx = -1;

   // if first state with "keepend" is popped, reset keepend_level
   if (keepend_level >= current_state.len)
      keepend_level = -1;
}

// Find the end of a start/skip/end syntax region after "startpos". Only checks one line.
// Also handles a match item that continued from a previous line.
// If not found, the syntax item continues in the next line.  m_endpos->lnum will be 0.
// If found, the end of the region and the end of the hiliting is computed.
private void
find_endpos(
   int      idx,      // index of the pattern
   PosNoVirt   *startpos,   // where to start looking for an END match
   PosNoVirt   *m_endpos,   // return: end of match
   PosNoVirt   *hl_endpos,   // return: end of highlighting
   long   *flagsp,   // return: flags of matching END
   PosNoVirt   *end_endpos,   // return: end of end pattern match
   int      *end_idx,   // return: group ID for end pat. match, or 0
   RegExternalMatch *start_ext)   // submatches from the start pattern
{
   ColNr   matchcol;
   SyntaxPattern   *spp, *spp_skip;
   int      start_idx;
   int      best_idx;
   RegMultilineMatch   regmatch;
   RegMultilineMatch   best_regmatch;       // startpos/endpos of best match
   PosNoVirt   pos;
   int      had_match = FALSE;
   Byte bookKeywordChars[32];  // chars for keywords array for syn option @iskeyword

   // just in case we are invoked for a keyword
   if (idx < 0)
      return;

   //Check for being called with a START pattern.
   //Can happen with a match that continues to the next line, because it
   //contained a region.
   spp = &(SYN_ITEMS(synBlockS)[idx]);
   if (spp->sp_type != SPTYPE_START) {
      *hl_endpos = *startpos;
      return;
   }

   // Find the SKIP or first END pattern after the last START pattern.
   for (;;) {
      spp = &(SYN_ITEMS(synBlockS)[idx]);
      if (spp->sp_type != SPTYPE_START)
         break;
      ++idx;
   }

   //   Lookup the SKIP pattern (if present)
   if (spp->sp_type == SPTYPE_SKIP) {
      spp_skip = spp;
      ++idx;
   } else
      spp_skip = NULL;

   // Setup external matches for syn_regexec().
   unref_extmatch(re_extmatch_in);
   re_extmatch_in = ref_extmatch(start_ext);

   matchcol = startpos->col;   // start looking for a match at sstart
   start_idx = idx;      // remember the first END pattern.
   best_regmatch.startpos[0].col = 0;      // avoid compiler warning

   // use syntax iskeyword option
   save_chartab(bookKeywordChars);

   for (;;) {
      // Find end pattern that matches first after "matchcol".
      best_idx = -1;
      for (idx = start_idx; idx < synBlockS->syntaxPatterns.len; ++idx) {
         int lc_col = matchcol;
         int r;

         spp = &(SYN_ITEMS(synBlockS)[idx]);
         if (spp->sp_type != SPTYPE_END)   // past last END pattern
            break;
         lc_col -= spp->sp_offsets[SPO_LC_OFF];
         if (lc_col < 0)
            lc_col = 0;

         regmatch.rmm_ic = spp->sp_ic;
         regmatch.regprog = spp->prog;
         r = syn_regexec(&regmatch, startpos->lnum, lc_col, IF_SYN_TIME(&spp->sp_time));
         spp->prog = regmatch.regprog;
         if (r) {
            if (best_idx == -1 || regmatch.startpos[0].col < best_regmatch.startpos[0].col) {
                best_idx = idx;
                best_regmatch.startpos[0] = regmatch.startpos[0];
                best_regmatch.endpos[0] = regmatch.endpos[0];
            }
         }
      }

      // If all end patterns have been tried, and there is no match, the
      // item continues until end-of-line.
      if (best_idx == -1)
         break;

      // If the skip pattern matches before the end pattern,
      // continue searching after the skip pattern.
      if (spp_skip) {
         int lc_col = matchcol - spp_skip->sp_offsets[SPO_LC_OFF];
         int r;

         if (lc_col < 0)
            lc_col = 0;
         regmatch.rmm_ic = spp_skip->sp_ic;
         regmatch.regprog = spp_skip->prog;
         r = syn_regexec(&regmatch, startpos->lnum, lc_col, IF_SYN_TIME(&spp_skip->sp_time));
         spp_skip->prog = regmatch.regprog;
         if (r && regmatch.startpos[0].col <= best_regmatch.startpos[0].col) {
            int line_len;

            // Add offset to skip pattern match
            syn_add_end_off(&pos, &regmatch, spp_skip, SPO_ME_OFF, 1);

            // If the skip pattern goes on to the next line, there is no
            // match with an end pattern in this line.
            if (pos.lnum > startpos->lnum)
                break;

            line_len = memGetBookLen(synBookS, startpos->lnum);

            // take care of an empty match or negative offset
            if (pos.col <= matchcol)
                ++matchcol;
            ei (pos.col <= regmatch.endpos[0].col)
                matchcol = pos.col;
            else {
               // Be careful not to jump over the ZERO at the end-of-line
               for (matchcol = regmatch.endpos[0].col; matchcol < line_len && matchcol < pos.col; 
                     ++matchcol)
                  {}
            } 

            // if the skip pattern includes end-of-line, break here
            if (matchcol >= line_len)
                break;

            continue;       // start with first end pattern again
         }
      }

      // Match from start pattern to end pattern. Correct for match and hilite offset of end pattern
      spp = &(SYN_ITEMS(synBlockS)[best_idx]);
      syn_add_end_off(m_endpos, &best_regmatch, spp, SPO_ME_OFF, 1);
      // can't end before the start
      if (m_endpos->lnum == startpos->lnum && m_endpos->col < startpos->col)
         m_endpos->col = startpos->col;

      syn_add_end_off(end_endpos, &best_regmatch, spp, SPO_HE_OFF, 1);
      // can't end before the start
      if (end_endpos->lnum == startpos->lnum && end_endpos->col < startpos->col)
         end_endpos->col = startpos->col;
      // can't end after the match
      limit_pos(end_endpos, m_endpos);

      // If the end group is highlighted differently, adjust the pointers.
      if (spp->patternHiId != spp->syntax.hiId && spp->patternHiId != 0) {
         *end_idx = best_idx;
         if (spp->sp_off_flags & (1 << (SPO_RE_OFF + SPO_COUNT))) {
            hl_endpos->lnum = best_regmatch.endpos[0].lnum;
            hl_endpos->col = best_regmatch.endpos[0].col;
         } else {
            hl_endpos->lnum = best_regmatch.startpos[0].lnum;
            hl_endpos->col = best_regmatch.startpos[0].col;
         }
         hl_endpos->col += spp->sp_offsets[SPO_RE_OFF];

         // can't end before the start
         if (hl_endpos->lnum == startpos->lnum && hl_endpos->col < startpos->col)
            hl_endpos->col = startpos->col;
         limit_pos(hl_endpos, m_endpos);

         // now the match ends where the hiliting ends, it is turned into the matchgroup for the end
         *m_endpos = *hl_endpos;
      } else {
          *end_idx = 0;
          *hl_endpos = *end_endpos;
      }

      *flagsp = spp->sp_flags;

      had_match = TRUE;
      break;
   }

   // no match for an END pattern in this line
   if (!had_match)
      m_endpos->lnum = 0;

   restoreKeywordChars(bookKeywordChars);

   // Remove external matches.
   unref_extmatch(re_extmatch_in);
   re_extmatch_in = NULL;
}

//Limit "pos" not to be after "limit".
private void
limit_pos(PosNoVirt* pos, PosNoVirt* limit) {
   if (pos->lnum > limit->lnum)
      *pos = *limit;
   ei (pos->lnum == limit->lnum && pos->col > limit->col)
      pos->col = limit->col;
}

// Limit "pos" not to be after "limit", unless pos->lnum is zero.
private void
limit_pos_zero( PosNoVirt   *pos, PosNoVirt   *limit) {
   if (pos->lnum == 0)
      *pos = *limit;
   else
      limit_pos(pos, limit);
}

// Add offset to matched text for end of match or highlight.
private void
syn_add_end_off(
   PosNoVirt   *result,   // returned position
   RegMultilineMatch   *regmatch,   // start/end of match
   SyntaxPattern   *spp,      // matched pattern
   int      idx,      // index of offset
   int      extra)      // extra chars for offset to start
{
   int      col;
   int      off;
   CS base;
   CS p;

   if (spp->sp_off_flags & (1 << idx)) {
      result->lnum = regmatch->startpos[0].lnum;
      col = regmatch->startpos[0].col;
      off = spp->sp_offsets[idx] + extra;
   } else {
      result->lnum = regmatch->endpos[0].lnum;
      col = regmatch->endpos[0].col;
      off = spp->sp_offsets[idx];
   }
   // Don't go past the end of the line.  Matters for "rs=e+2" when there
   // is a matchgroup. Watch out for match with last NL in the buffer.
   if (result->lnum > synBookS->mem.lineCount)
      col = 0;
   ei (off != 0) {
      base = memGetLine(synBookS, result->lnum, FALSE);
      p = base + col;
      if (off > 0) {
          while (off-- > 0 && *p != ZERO)
         MB_PTR_ADV(p);
      } ei (off < 0) {
         while (off++ < 0 && base < p)
            MB_PTR_BACK(base, p);
      }
      col = (int)(p - base);
   }
   result->col = col;
}

// Add offset to matched text for start of match or highlight.
// Avoid resulting column to become negative.
private void
syn_add_start_off(
   PosNoVirt   *result,   // returned position
   RegMultilineMatch   *regmatch,   // start/end of match
   SyntaxPattern   *spp,
   int      idx,
   int      extra       // extra chars for offset to end
){
   int      col;
   int      off;
   CS base;
   CS p;

   if (spp->sp_off_flags & (1 << (idx + SPO_COUNT))) {
      result->lnum = regmatch->endpos[0].lnum;
      col = regmatch->endpos[0].col;
      off = spp->sp_offsets[idx] + extra;
   } else {
      result->lnum = regmatch->startpos[0].lnum;
      col = regmatch->startpos[0].col;
      off = spp->sp_offsets[idx];
   }
   if (result->lnum > synBookS->mem.lineCount) {
      // a "\n" at the end of the pattern may take us below the last line
      result->lnum = synBookS->mem.lineCount;
      col = memGetBookLen(synBookS, result->lnum);
   }
   if (off != 0) {
      base = memGetLine(synBookS, result->lnum, FALSE);
      p = base + col;
      if (off > 0) {
         while (off-- && *p != ZERO)
            MB_PTR_ADV(p);
      } ei (off < 0) {
         while (off++ && base < p)
            MB_PTR_BACK(base, p);
      }
      col = (int)(p - base);
   }
   result->col = col;
}

// Get current line in syntax buffer.
private CS
syn_getcurline(void) {
   return memGetLine(synBookS, currLnumS, FALSE);
}

// Get length of current line in syntax buffer.
private ColNr
syn_getcurline_len(void) {
   return memGetBookLen(synBookS, currLnumS);
}

// Call eeRegexec() to find a match with "rmp" in "synBookS". Return TRUE when there is a match.
private int
syn_regexec(
   RegMultilineMatch   *rmp,
   LineNr   lnum,
   ColNr   col,
   syn_Time  *st UNUSED
) {
   int      r;
   int      timed_out = FALSE;

   if (rmp->regprog == NULL)
      // This can happen if a previous call to eeRegexec_multi() tried to
      // use the NFA engine, which resulted in NFA_TOO_EXPENSIVE, and
      // compiling the pattern with the other engine fails.
      return FALSE;

   rmp->rmm_maxcol = SYNTAX_MAX_COL;
   r = eeRegexec_multi(rmp, syntPortS, synBookS, lnum, col, &timed_out);

   if (timed_out && redrawtime_limit_set && !syntPortS->ownSyntax->redrawTime) {
      syntPortS->ownSyntax->redrawTime = TRUE;
      msg(_("'redrawtime' exceeded, syntax highlighting disabled"));
   }

   if (r > 0) {
      rmp->startpos[0].lnum += lnum;
      rmp->endpos[0].lnum += lnum;
      return TRUE;
   }
   return FALSE;
}

// Check one position in a line for a matching keyword. The caller must check if a keyword can 
// start at startcol. Return its ID if found, 0 otherwise.
private Short
check_keyword_id(
   CS line,
   int startcol,   // position in line to check for keyword
   int* endcolp,   // return: character after found keyword
   long* flagsp,   // return: flags of matching keyword
   Short** next_listp,   // return: next_list of matching keyword
   StateItem* currStateItem,   // item at the top of the stack
   int* ccharp UNUSED   // conceal substitution char
){
   CS kwp;
   int round;
   int kwlen;
   Byte keyword[MAXKEYWLEN + 1]; // assume max. keyword len is 80
   EeSet   *ht;
   EeSetItem   *hi;

   // Find first character after the keyword.  First character was already checked.
   kwp = line + startcol;
   kwlen = 0;
   do {
      kwlen += utfCharLen(kwp + kwlen);
   } while (eeIsWordPtr_buf(kwp + kwlen, synBookS));

   if (kwlen > MAXKEYWLEN)
      return 0;

   // Must make a copy of the keyword, so we can add a ZERO and make it lowercase.
   copySubstrToAllocation(keyword, (Text){kwp, kwlen});

   // Try twice:
   // 1. matching case
   // 2. ignoring case
   for (round = 1; round <= 2; ++round) {
      ht = round == 1 ? &synBlockS->keywords : &synBlockS->keywordsIgnoreCase;
      if (ht->count == 0)
         continue;
      if (round == 2)   // ignore case
         (void)str_foldcase(kwp, kwlen, keyword, MAXKEYWLEN + 1);

      // Find keywords that match.  There can be several with different attributes.
      // When current_next_list is non-zero accept only that group, otherwise:
      //  Accept a not-contained keyword at toplevel.
      //  Accept a keyword at other levels only if it is in the contains list.
      hi = hash_find(ht, mbText(keyword));
      if (!HASHITEM_EMPTY(hi))
         for (KeyEntry* kp = HI2KE(hi); kp; kp = kp->next) {
            if (current_next_list != 0
               ? in_id_list(NULL, current_next_list, &kp->syntax, 0)
               : (currStateItem == NULL
                   ? !(kp->flags & HL_CONTAINED)
                   : in_id_list(currStateItem, currStateItem->si_containsHiId, &kp->syntax, kp->flags))
            ){
               *endcolp = startcol + kwlen;
               *flagsp = kp->flags;
               *next_listp = kp->next_list;
               return kp->syntax.hiId;
            }
         }
    }
    return SHORT;
}

// Handle ":syntax case" command.
private void
caseSubcommand(Invocation* invo, int syncing UNUSED) {
   CS arg = invo->arg;

   invo->nextComm = find_nextcmd(arg);
   if (invo->skip)
      return;

   CS next = skiptowhite(arg);
   if (*arg == ZERO) {
   if (curPor->ownSyntax->b_syn_ic)
       msg((CS)"syntax case ignore");
   else
       msg((CS)"syntax case match");
   } ei (STRNICMP(arg, "match", 5) == 0 && next - arg == 5)
      curPor->ownSyntax->b_syn_ic = FALSE;
   ei (STRNICMP(arg, "ignore", 6) == 0 && next - arg == 6)
      curPor->ownSyntax->b_syn_ic = TRUE;
   else
      showErrFmtMsg(_(e_illegal_argument_str_2), arg);
}

// Handle ":syntax foldlevel" command.
private void
syn_cmd_foldlevel(Invocation* invo, int syncing UNUSED) {
   CS arg = invo->arg;

   invo->nextComm = find_nextcmd(arg);
   if (invo->skip)
      return;

   if (*arg == ZERO) {
      switch (curPor->ownSyntax->foldLevel) {
      case SYNFLD_START:   msg((CS)"syntax foldlevel start");   break;
      case SYNFLD_MINIMUM: msg((CS)"syntax foldlevel minimum"); break;
      default: break;
      }
      return;
   }

   CS arg_end = skiptowhite(arg);
   if (STRNICMP(arg, "start", 5) == 0 && arg_end - arg == 5)
      curPor->ownSyntax->foldLevel = SYNFLD_START;
   ei (STRNICMP(arg, "minimum", 7) == 0 && arg_end - arg == 7)
      curPor->ownSyntax->foldLevel = SYNFLD_MINIMUM;
   else {
      showErrFmtMsg(_(e_illegal_argument_str_2), arg);
      return;
   }

   arg = skipwhite(arg_end);
   if (*arg != ZERO) {
      showErrFmtMsg(_(e_illegal_argument_str_2), arg);
   }
}

// Handle ":syntax spell" command.
private void
syn_cmd_spell(Invocation* invo, int syncing UNUSED) {
   CS arg = invo->arg;

   invo->nextComm = find_nextcmd(arg);
   if (invo->skip)
      return;

   CS next = skiptowhite(arg);
   if (*arg == ZERO) {
   if (curPor->ownSyntax->synSpell == SYNSPL_TOP)
       msg((CS)"syntax spell toplevel");
   ei (curPor->ownSyntax->synSpell == SYNSPL_NOTOP)
       msg((CS)"syntax spell notoplevel");
   else
       msg((CS)"syntax spell default");
   } ei (STRNICMP(arg, "toplevel", 8) == 0 && next - arg == 8)
      curPor->ownSyntax->synSpell = SYNSPL_TOP;
   ei (STRNICMP(arg, "notoplevel", 10) == 0 && next - arg == 10)
      curPor->ownSyntax->synSpell = SYNSPL_NOTOP;
   ei (STRNICMP(arg, "default", 7) == 0 && next - arg == 7)
      curPor->ownSyntax->synSpell = SYNSPL_DEFAULT;
   else {
      showErrFmtMsg(_(e_illegal_argument_str_2), arg);
      return;
   }

   // assume spell checking changed, force a redraw
   redrawPortLater(curPor, UPD_NOT_VALID);
}

// Handle ":syntax iskeyword" command.
private void
syn_cmd_iskeyword(Invocation* invo, int syncing UNUSED) {
   Byte save_chartab[32];

   if (invo->skip)
      return;

   CS arg = skipwhite(invo->arg);
   if (*arg == ZERO) {
      msg_puts(S"\n");
      if (curPor->book->o.isKeyword != Em) {
          msg_puts((CS)"syntax iskeyword ");
          msg_outtrans(curPor->book->o.isKeyword);
      } else
         msg_outtrans((CS)_("syntax iskeyword not set"));
   } else {
      if (STRNICMP(arg, "clear", 5) == 0) {
         mch_memmove(curPor->ownSyntax->b_syn_chartab, curBook->charsForKeywords, (Unt)32);
         curPor->book->o.isKeyword = null;
      } else {
         mch_memmove(save_chartab, curBook->charsForKeywords, (Unt)32);
         CS save_isk = curBook->o.isKeyword;
         curBook->o.isKeyword = copyStr(arg);

         mch_memmove(curPor->ownSyntax->b_syn_chartab, curBook->charsForKeywords, (Unt)32);
         mch_memmove(curBook->charsForKeywords, save_chartab, (Unt)32);
         curPor->book->o.isKeyword = null;
         curBook->o.isKeyword = save_isk;
      }
   }
   redrawPortLater(curPor, UPD_NOT_VALID);
}

// Clear all syntax info for one buffer.
void
syntax_clear(SyntaxBlock *block) {
   block->b_syn_error = FALSE;       // clear previous error
   block->redrawTime = FALSE;       // clear previous timeout
   block->b_syn_ic = FALSE;       // Use case, by default
   block->foldLevel = SYNFLD_START;
   block->synSpell = SYNSPL_DEFAULT; // default spell checking
   block->b_syn_containedin = FALSE;

   // free the keywords
   clearKeywordTable(&block->keywords);
   clearKeywordTable(&block->keywordsIgnoreCase);

   // free the syntax patterns
   for (int i = block->syntaxPatterns.len; --i >= 0; )
      syn_clear_pattern(block, i);
   ga_clear(&block->syntaxPatterns);

   // free the syntax clusters
   for (int i = block->syntaxClusters.len; --i >= 0; )
      syn_clear_cluster(block, i);
   ga_clear(&block->syntaxClusters);
   block->spellClusterId = 0;
   block->noSpellClusterId = 0;

   block->syncFlags = 0;
   block->b_syn_sync_minlines = 0;
   block->b_syn_sync_maxlines = 0;
   block->syncLinebreaks = 0;

   eeRegFree(block->lineContinProg);
   block->lineContinProg = NULL;
   EE_CLEAR(block->lineContinuationPattern);
   block->b_syn_folditems = 0;

   // free the stored states
   synFreeBlock(block);
   invalidate_current_state();

   // Reset the counter for ":syn include"
   running_syn_inc_tag = 0;
}

// Get rid of ownsyntax for window "wp".
void
reset_synblock(Portal *wp) {
   if (wp->ownSyntax != &wp->book->syntax) {
      syntax_clear(wp->ownSyntax);
      eeglFree(wp->ownSyntax);
      wp->ownSyntax = &wp->book->syntax;
   }
}

// Clear syncing info for one buffer.
private void
syntax_sync_clear(void) {
   // free the syntax patterns
   for (int i = curPor->ownSyntax->syntaxPatterns.len; --i >= 0; ) {
      if (SYN_ITEMS(curPor->ownSyntax)[i].syncing)
          syn_remove_pattern(curPor->ownSyntax, i);
   } 

   curPor->ownSyntax->syncFlags = 0;
   curPor->ownSyntax->b_syn_sync_minlines = 0;
   curPor->ownSyntax->b_syn_sync_maxlines = 0;
   curPor->ownSyntax->syncLinebreaks = 0;

   eeRegFree(curPor->ownSyntax->lineContinProg);
   curPor->ownSyntax->lineContinProg = NULL;
   EE_CLEAR(curPor->ownSyntax->lineContinuationPattern);

   synFreeBlock(curPor->ownSyntax);   // Need to recompute all syntax.
}

// Remove one pattern from the buffer's pattern list.
private void
syn_remove_pattern( SyntaxBlock   *block, int      idx) {
   SyntaxPattern   *spp;

   spp = &(SYN_ITEMS(block)[idx]);
   if (spp->sp_flags & HL_FOLD)
      --block->b_syn_folditems;
   syn_clear_pattern(block, idx);
   mch_memmove(spp, spp + 1, sizeof(SyntaxPattern) * (block->syntaxPatterns.len - idx - 1));
   --block->syntaxPatterns.len;
}

// Clear and free one syntax pattern.  When clearing all, must be called from last to first!
private void
syn_clear_pattern(SyntaxBlock *block, int i) {
   eeglFree(SYN_ITEMS(block)[i].pattern);
   eeRegFree(SYN_ITEMS(block)[i].prog);
   // Only free sp_containsHiId and sp_next_list of first start pattern
   if (i == 0 || SYN_ITEMS(block)[i - 1].sp_type != SPTYPE_START) {
      eeglFree(SYN_ITEMS(block)[i].sp_containsHiId);
      eeglFree(SYN_ITEMS(block)[i].sp_next_list);
      eeglFree(SYN_ITEMS(block)[i].syntax.containedInHiId);
   }
}

// Clear and free one syntax cluster.
private void
syn_clear_cluster(SyntaxBlock *block, int i) {
   SynCluster* cluster = ((SynCluster *)(block->syntaxClusters.c)) + i;
   eeglFree(cluster->name);
   eeglFree(cluster->nameUpper);
   eeglFree(cluster->hiIds);
}

// Handle ":syntax clear" command.
private void
clearSubcommand(Invocation* invo, int syncing) {
   CS arg = invo->arg;
   CS arg_end;

   invo->nextComm = find_nextcmd(arg);
   if (invo->skip)
      return;

   if (endsComm(arg)) {
      //No argument: Clear all syntax items.
      if (syncing)
         syntax_sync_clear();
      else {
         syntax_clear(curPor->ownSyntax);
         if (curPor->ownSyntax == &curPor->book->syntax)
            unletImpl(BUFF_SYN_VAR, true);
         unletImpl(PORT_SYN_VAR, true);
      }
   } else {
      // Clear the group IDs that are in the argument.
      while (!endsComm(arg)) {
         Short     hiId;
         arg_end = skiptowhite(arg);
         if (*arg == '@') {
            hiId = syntaxClusterByName((Text){.c = arg + 1, .len = arg_end - arg - 1});
            if (hiId == SHORT) {
               showErrFmtMsg(_(e_no_such_syntax_cluster_str_1), arg);
               break;
            } else {
               // We can't physically delete a cluster without changing
               // the IDs of other clusters, so we do the next best thing and make it empty.
               Short scl_id = hiId - SYNID_CLUSTER;

               EE_CLEAR(SYN_CLSTR(curPor->ownSyntax)[scl_id].hiIds);
            }
         } else {
            hiId = syntaxClusterByName((Text){.c = arg, .len = arg_end - arg});
            if (hiId == SHORT) {
               showErrFmtMsg(_(e_no_such_highlight_group_name_str), arg);
               break;
            } else
               syn_clear_one(hiId, syncing);
          }
          arg = skipwhite(arg_end);
      }
   }
   drawCurBookLater(UPD_SOME_VALID);
   synFreeBlock(curPor->ownSyntax);      // Need to recompute all syntax.
}

// Clear one syntax group for the current buffer.
private void
syn_clear_one(Short hiId, int syncing) {
   // Clear keywords only when not ":syn sync clear group-name"
   if (!syncing) {
      (void)syn_clear_keyword(hiId, &curPor->ownSyntax->keywords);
      (void)syn_clear_keyword(hiId, &curPor->ownSyntax->keywordsIgnoreCase);
   }

   // clear the patterns for "id"
   for (int idx = curPor->ownSyntax->syntaxPatterns.len; --idx >= 0; ) {
      SyntaxPattern* spp = &(SYN_ITEMS(curPor->ownSyntax)[idx]);
      if (spp->syntax.hiId != hiId || spp->syncing != syncing)
         continue;
      syn_remove_pattern(curPor->ownSyntax, idx);
   }
}

//":syntax off" command. Clear all autocommands for the Syntax event, unlet "b:currentSyntax"
//on all buffers, and unlet "syntax_on" and "syntax_manual" vars.
private void
offSubcommand(Invocation* invo UNUSED, int syncing UNUSED) {
   autoEventImpl(
      EVENT_SYNTAX, null, 
      (AutoCommCreation){
         .group = AUGROUP_ALL, .commandBody = null, .deleteExisting = true, false, false
      }
   );
   unletImpl(S"syntax_on", true);
   unletImpl(S"syntax_manual", true);
   Book* book;
   FOR_ALL_BOOKS(book) {
      unletVarFromHashTable(S"currentSyntax", BUFF_SYN_VAR, &book->bVars->hashTable, true);
   }
}

// ":syntax on" command. Remove all autocommands for the syntax event, then turn syntax hiliting on
private void
theOnSubcommand(Invocation* invo, int syncing UNUSED) {
   offSubcommand(invo, false);
   
   Var tv = (Var){.tag = VAR_BOOL, .number = VVAL_TRUE}; 
   evalLetVarSimple(S"syntaxOn", &tv);
   evalLetVarSimple(S"syntaxManual", &tv);
   //callScriptForSubcommand(invo, "syntax");
}

// Handle ":syntax enable" command.
private void
syn_cmd_enable(Invocation* invo, int syncing UNUSED) {
   set_internal_string_var((CS)"g:syntaxCmd", (CS)"enable");
   callScriptForSubcommand(invo, "syntax");
   unletImpl(S"g:syntaxCmd", true);
}

// Handle ":syntax reset" command. It actually resets highlighting, not syntax.
private void
syn_cmd_reset(Invocation* invo, int syncing UNUSED) {
   set_nextcmd(invo, invo->arg);
   if (!invo->skip) {
      set_internal_string_var((CS)"g:syntaxCmd", (CS)"reset");
      executeCommLine((CS)"runtime! syntax/syncolor.vim");
      unletImpl(S"g:syntaxCmd", true);
   }
}

// Handle ":syntax manual" command.
private void
syn_cmd_manual(Invocation* invo, int syncing UNUSED) {
   callScriptForSubcommand(invo, "manual");
}

private void
callScriptForSubcommand(Invocation* invo, char *name) {
   Byte buf[100];

   set_nextcmd(invo, invo->arg);
   if (!invo->skip) {
      STRCPY(buf, "so ");
      eeSnprintf(buf + 3, sizeof(buf) - 3, SYNTAX_FNAME, name);
      executeCommLine(buf);
   }
}

// The ":syntax [list]" command: list current syntax words.
private void
syn_cmd_list(Invocation* invo, int syncing)  {     // when TRUE: list syncing items
   CS arg = invo->arg;
   Short      id;
   CS arg_end;

   invo->nextComm = find_nextcmd(arg);
   if (invo->skip)
      return;

   if (!syntax_present(curPor)) {
      msg(_(msg_no_items));
      return;
   }

   if (syncing) {
      if (curPor->ownSyntax->syncFlags & SF_CCOMMENT) {
         msg_puts(_("syncing on C-style comments"));
         syn_lines_msg();
         syn_match_msg();
         return;
      } ei (!(curPor->ownSyntax->syncFlags & SF_MATCH)) {
         if (curPor->ownSyntax->b_syn_sync_minlines == 0)
            msg_puts(_("no syncing"));
         else {
            if (curPor->ownSyntax->b_syn_sync_minlines == MAXLNUM)
               msg_puts(_("syncing starts at the first line"));
            else {
               msg_puts(_("syncing starts "));
               msg_outnum(curPor->ownSyntax->b_syn_sync_minlines);
               msg_puts(_(" lines before top line"));
            }
            syn_match_msg();
         }
         return;
      }
      msg_puts_title(_("\n--- Syntax sync items ---"));
      if (curPor->ownSyntax->b_syn_sync_minlines > 0
            || curPor->ownSyntax->b_syn_sync_maxlines > 0
            || curPor->ownSyntax->syncLinebreaks > 0
      ){
         msg_puts(_("\nsyncing on items"));
         syn_lines_msg();
         syn_match_msg();
      }
   } else
      msg_puts_title(_("\n--- Syntax items ---"));
   if (endsComm(arg)) {
      // No argument: List all group IDs and all syntax clusters.
      for (id = 0; id < countDecosG && !gotInterruptG; ++id)
         syn_list_one(id, syncing, FALSE);
      for (id = 0; id < curPor->ownSyntax->syntaxClusters.len && !gotInterruptG; ++id)
         syn_list_cluster(id);
   } else {
      // List the group IDs and syntax clusters that are in the argument.
      while (!endsComm(arg) && !gotInterruptG) {
         arg_end = skiptowhite(arg);
         if (*arg == '@') {
            id = syntaxClusterByName((Text){.c = arg + 1, .len = arg_end - arg - 1});
            if (id == 0)
               showErrFmtMsg(_(e_no_such_syntax_cluster_str_2), arg);
            else
               syn_list_cluster(id - SYNID_CLUSTER);
         } else {
            id = syntaxClusterByName((Text){.c = arg, .len = arg_end - arg});
            if (id == 0)
               showErrFmtMsg(_(e_no_such_highlight_group_name_str), arg);
            else
               syn_list_one(id, syncing, TRUE);
         }
         arg = skipwhite(arg_end);
      }
   }
   set_nextcmd(invo, arg);
}

private void
syn_lines_msg(void) {
   if (curPor->ownSyntax->b_syn_sync_maxlines > 0 || curPor->ownSyntax->b_syn_sync_minlines > 0) {
      msg_puts((CS)"; ");
      if (curPor->ownSyntax->b_syn_sync_minlines == MAXLNUM)
          msg_puts(_("from the first line"));
      else {
         if (curPor->ownSyntax->b_syn_sync_minlines > 0) {
            msg_puts(_("minimal "));
            msg_outnum(curPor->ownSyntax->b_syn_sync_minlines);
            if (curPor->ownSyntax->b_syn_sync_maxlines)
               msg_puts((CS)", ");
         }
         if (curPor->ownSyntax->b_syn_sync_maxlines > 0) {
            msg_puts(_("maximal "));
            msg_outnum(curPor->ownSyntax->b_syn_sync_maxlines);
         }
         msg_puts(_(" lines before top line"));
      }
   }
}

private void
syn_match_msg(void) {
   if (curPor->ownSyntax->syncLinebreaks > 0) {
      msg_puts(_("; match "));
      msg_outnum(curPor->ownSyntax->syncLinebreaks);
      msg_puts(_(" line breaks"));
   }
}

private int  last_matchgroup;

private void syn_list_flags(Kv *nlist, int nr_entries, int flags, char decoFlags);

// List one syntax item, for ":syntax" or "syntax list syntax_name".
private void
syn_list_one(
   int id,
   int syncing,       // when TRUE: list syncing items
   int link_only       // when TRUE; list link-only too
){
   int idx;
   int did_header = FALSE;
   SyntaxPattern* spp;
   static Kv namelist1[] = {
      KEYVALUE_ENTRY(HL_DISPLAY, "display"),
      KEYVALUE_ENTRY(HL_CONTAINED, "contained"),
      KEYVALUE_ENTRY(HL_ONELINE, "oneline"),
      KEYVALUE_ENTRY(HL_KEEPEND, "keepend"),
      KEYVALUE_ENTRY(HL_EXTEND, "extend"),
      KEYVALUE_ENTRY(HL_EXCLUDENL, "excludenl"),
      KEYVALUE_ENTRY(HL_TRANSP, "transparent"),
      KEYVALUE_ENTRY(HL_FOLD, "fold")
   };
   static Kv namelist2[] = {
      KEYVALUE_ENTRY(HL_SKIPWHITE, "skipwhite"),
      KEYVALUE_ENTRY(HL_SKIPNL, "skipnl"),
      KEYVALUE_ENTRY(HL_SKIPEMPTY, "skipempty")
   };

   char decoFlags = getDecoFlags(HLF_D);      // hilite like directories

   // list the keywords for "id"
   if (!syncing) {
      did_header = syn_list_keywords(id, &curPor->ownSyntax->keywords, FALSE, decoFlags);
      did_header = syn_list_keywords(id, &curPor->ownSyntax->keywordsIgnoreCase, did_header, decoFlags);
   }

   // list the patterns for "id"
   for (idx = 0; idx < curPor->ownSyntax->syntaxPatterns.len && !gotInterruptG; ++idx) {
      spp = &(SYN_ITEMS(curPor->ownSyntax)[idx]);
      if (spp->syntax.hiId != id || spp->syncing != syncing)
         continue;

      printHiliteHeader(did_header, 999, id);
      did_header = TRUE;
      last_matchgroup = 0;
      if (spp->sp_type == SPTYPE_MATCH) {
         put_pattern((CS)"match", ' ', spp, decoFlags);
         msg_putchar(' ');
      }
      ei (spp->sp_type == SPTYPE_START) {
         while (SYN_ITEMS(curPor->ownSyntax)[idx].sp_type == SPTYPE_START)
            put_pattern(S"start", '=', &SYN_ITEMS(curPor->ownSyntax)[idx++], decoFlags);
         if (SYN_ITEMS(curPor->ownSyntax)[idx].sp_type == SPTYPE_SKIP)
            put_pattern(S"skip", '=', &SYN_ITEMS(curPor->ownSyntax)[idx++], decoFlags);
         while (idx < curPor->ownSyntax->syntaxPatterns.len
              && SYN_ITEMS(curPor->ownSyntax)[idx].sp_type == SPTYPE_END)
         put_pattern((CS)"end", '=', &SYN_ITEMS(curPor->ownSyntax)[idx++], decoFlags);
         --idx;
         msg_putchar(' ');
      }
      syn_list_flags(namelist1, (int)ARRAY_LENGTH(namelist1), spp->sp_flags, decoFlags);

      if (spp->sp_containsHiId)
         put_id_list((CS)"contains", spp->sp_containsHiId, decoFlags);

      if (spp->syntax.containedInHiId)
         put_id_list((CS)"containedin", spp->syntax.containedInHiId, decoFlags);

      if (spp->sp_next_list) {
         put_id_list((CS)"nextgroup", spp->sp_next_list, decoFlags);
         syn_list_flags(namelist2, (int)ARRAY_LENGTH(namelist2), spp->sp_flags, decoFlags);
      }
      if (spp->sp_flags & (HL_SYNC_HERE|HL_SYNC_THERE)) {
         if (spp->sp_flags & HL_SYNC_HERE)
            msgPutsDeco((CS)"grouphere", decoFlags);
         else
            msgPutsDeco((CS)"groupthere", decoFlags);
         msg_putchar(' ');
         if (spp->sp_sync_idx >= 0)
            msg_outtrans(
               hiliteGroupName(SYN_ITEMS(curPor->ownSyntax)[spp->sp_sync_idx].syntax.hiId - 1).c
            );
         else
            msg_puts((CS)"NONE");
         msg_putchar(' ');
      }
   }

   // list the link, if there is one
   if (highlight_link_id(id - 1) && (did_header || link_only) && !gotInterruptG) {
      printHiliteHeader(did_header, 999, id);
      msgPutsDeco((CS)"links to", decoFlags);
      msg_putchar(' ');
      msg_outtrans(hiliteGroupName(highlight_link_id(id - 1) - 1).c);
   }
}

private void
syn_list_flags(Kv *nlist, int nr_entries, int flags, char decoFlags) {
   for (int i = 0; i < nr_entries; ++i) {
      if (flags & nlist[i].key) {
         msgPutsDeco(nlist[i].value.c, decoFlags);
         msg_putchar(' ');
      }
   } 
}

// List one syntax cluster, for ":syntax" or "syntax list syntax_name".
private void
syn_list_cluster(int id) {
   int endcol = 15;

   // slight hack:  roughly duplicate the guts of printHiliteHeader()
   msg_putchar('\n');
   msg_outtrans(SYN_CLSTR(curPor->ownSyntax)[id].name);

   if (msgColG >= endcol)   // output at least one space
      endcol = msgColG + 1;
   if (visibleColsG <= (long)endcol)   // avoid hang for tiny window
      endcol = (int)(visibleColsG - 1);

   msg_advance(endcol);
   if (SYN_CLSTR(curPor->ownSyntax)[id].hiIds != NULL) {
      put_id_list(
         (CS)"cluster", SYN_CLSTR(curPor->ownSyntax)[id].hiIds, getDecoFlags(HLF_D)
      );
   } else {
      msgPutsDeco((CS)"cluster", getDecoFlags(HLF_D));
      msg_puts((CS)"=NONE");
   }
}

private void
put_id_list(CS name, Short *list, int deco) {
   Short      *p;

   msgPutsDeco(name, deco);
   msg_putchar('=');
   for (p = list; *p; ++p) {
      if (*p >= SYNID_ALLBUT && *p < SYNID_TOP) {
         if (p[1])
            msg_puts((CS)"ALLBUT");
         else
            msg_puts((CS)"ALL");
      } ei (*p >= SYNID_TOP && *p < SYNID_CONTAINED) {
         msg_puts((CS)"TOP");
      } ei (*p >= SYNID_CONTAINED && *p < SYNID_CLUSTER) {
         msg_puts((CS)"CONTAINED");
      } ei (*p >= SYNID_CLUSTER) {
         Short scl_id = *p - SYNID_CLUSTER;

         msg_putchar('@');
         msg_outtrans(SYN_CLSTR(curPor->ownSyntax)[scl_id].name);
      } else
         msg_outtrans(hiliteGroupName(*p - 1).c);
      if (p[1])
         msg_putchar(',');
    }
    msg_putchar(' ');
}

private void
put_pattern(CS s, int c, SyntaxPattern   *spp, int deco) {
   long   n;
   int      mask;
   int      first;
   static char   *sepchars = "/+=-#@\"|'^&";
   int      i;

   // May have to write "matchgroup=group"
   if (last_matchgroup != spp->patternHiId) {
      last_matchgroup = spp->patternHiId;
      msgPutsDeco((CS)"matchgroup", deco);
      msg_putchar('=');
      if (last_matchgroup == 0)
         msg_outtrans((CS)"NONE");
      else
         msg_outtrans(hiliteGroupName(last_matchgroup - 1).c);
      msg_putchar(' ');
   }

   // output the name of the pattern and an '=' or ' '
   msgPutsDeco(s, deco);
   msg_putchar(c);

   // output the pattern, in between a char that is not in the pattern
   for (i = 0; firstOccurrence(spp->pattern, sepchars[i]) != NULL; ) {
      if (sepchars[++i] == ZERO) {
          i = 0;   // no good char found, just use the first one
          break;
      }
   } 
   msg_putchar(sepchars[i]);
   msg_outtrans(spp->pattern);
   msg_putchar(sepchars[i]);

   // output any pattern options
   first = TRUE;
   for (i = 0; i < SPO_COUNT; ++i) {
      mask = (1 << i);
      if (spp->sp_off_flags & (mask + (mask << SPO_COUNT))) {
         if (!first)
            msg_putchar(',');   // separate with commas
         msg_puts(spo_name_tab[i]);
         n = spp->sp_offsets[i];
         if (i != SPO_LC_OFF) {
            if (spp->sp_off_flags & mask)
               msg_putchar('s');
            else
               msg_putchar('e');
            if (n > 0)
               msg_putchar('+');
         }
         if (n || i == SPO_LC_OFF)
            msg_outnum(n);
         first = FALSE;
      }
    }
    msg_putchar(' ');
}

//List or clear the keywords for one syntax group.
//Return TRUE if the header has been printed.
private int
syn_list_keywords(
   int id,
   EeSet* ht,
   int did_header,      // header has already been printed
   int deco
) {
   int outlen;
   EeSetItem* hi;
   KeyEntry* kp;
   Unt prev_contained = 0;
   Short* prev_next_list = NULL;
   Short* prev_containedInHiId = NULL;
   Unt prev_skipnl = 0;
   Unt prev_skipwhite = 0;
   Unt prev_skipempty = 0;

   // Unfortunately, this list of keywords is not sorted on alphabet but on hash value...
   int todo = (int)ht->count;
   for (hi = ht->array; todo > 0 && !gotInterruptG; ++hi) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         for (kp = HI2KE(hi); kp != NULL && !gotInterruptG; kp = kp->next) {
            if (kp->syntax.hiId == id) {
                if (prev_contained != (kp->flags & HL_CONTAINED)
                      || prev_skipnl != (kp->flags & HL_SKIPNL)
                      || prev_skipwhite != (kp->flags & HL_SKIPWHITE)
                      || prev_skipempty != (kp->flags & HL_SKIPEMPTY)
                      || prev_containedInHiId != kp->syntax.containedInHiId
                      || prev_next_list != kp->next_list)
                  outlen = 9999;
               else
                  outlen = (int)STRLEN(kp->keyword);
               // output "contained" and "nextgroup" on each line
               if (printHiliteHeader(did_header, outlen, id)) {
                  prev_contained = 0;
                  prev_next_list = NULL;
                  prev_containedInHiId = NULL;
                  prev_skipnl = 0;
                  prev_skipwhite = 0;
                  prev_skipempty = 0;
               }
               did_header = TRUE;
               if (prev_contained != (kp->flags & HL_CONTAINED)) {
                  msgPutsDeco((CS)"contained", deco);
                  msg_putchar(' ');
                  prev_contained = (kp->flags & HL_CONTAINED);
               }
               if (kp->syntax.containedInHiId != prev_containedInHiId) {
                  put_id_list((CS)"containedin", kp->syntax.containedInHiId, deco);
                  msg_putchar(' ');
                  prev_containedInHiId = kp->syntax.containedInHiId;
               }
               if (kp->next_list != prev_next_list) {
                  put_id_list((CS)"nextgroup", kp->next_list, deco);
                  msg_putchar(' ');
                  prev_next_list = kp->next_list;
                  if (kp->flags & HL_SKIPNL) {
                     msgPutsDeco((CS)"skipnl", deco);
                     msg_putchar(' ');
                     prev_skipnl = (kp->flags & HL_SKIPNL);
                  }
                  if (kp->flags & HL_SKIPWHITE) {
                     msgPutsDeco((CS)"skipwhite", deco);
                     msg_putchar(' ');
                     prev_skipwhite = (kp->flags & HL_SKIPWHITE);
                  }
                  if (kp->flags & HL_SKIPEMPTY) {
                     msgPutsDeco((CS)"skipempty", deco);
                     msg_putchar(' ');
                     prev_skipempty = (kp->flags & HL_SKIPEMPTY);
                  }
               }
               msg_outtrans(kp->keyword);
            }
         }
      }
   }

   return did_header;
}

private void
syn_clear_keyword(int id, EeSet *ht) {
   EeSetItem   *hi;
   KeyEntry   *kp;
   KeyEntry   *kp_prev;
   KeyEntry   *kp_next;
   int      todo;

   hash_lock(ht);
   todo = (int)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         kp_prev = NULL;
         for (kp = HI2KE(hi); kp != NULL; ) {
            if (kp->syntax.hiId == id) {
               kp_next = kp->next;
               if (kp_prev == NULL) {
                  if (kp_next == NULL)
                     hash_remove(ht, hi, S"syntax clear keyword");
                  else
                     hi->hi_key = KE2HIKEY(kp_next);
               } else
                  kp_prev->next = kp_next;
               eeglFree(kp->next_list);
               eeglFree(kp->syntax.containedInHiId);
               eeglFree(kp);
               kp = kp_next;
            } else {
               kp_prev = kp;
               kp = kp->next;
            }
          }
      }
   }
   hash_unlock(ht);
}

// Clear a whole keyword table.
private void
clearKeywordTable(EeSet *ht) {
   EeSetItem   *hi;
   int      todo;
   KeyEntry   *kp;
   KeyEntry   *kp_next;

   todo = (int)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
          --todo;
         for (kp = HI2KE(hi); kp != NULL; kp = kp_next) {
            kp_next = kp->next;
            eeglFree(kp->next_list);
            eeglFree(kp->syntax.containedInHiId);
            eeglFree(kp);
         }
      }
   }
   hash_clear(ht);
   hash_init(ht);
}

// Add a keyword to the list of keywords.
private void
add_keyword(
   CS name,       // name of keyword
   Unt namelen,    // length of keyword (excluding the ZERO)
   int id,       // group ID for this keyword
   Unt flags,       // flags for this keyword
   Short* containedInHiId, // containedin for this keyword
   Short* next_list // nextgroup for this keyword
) {
   KeyEntry   *kp;
   EeSet   *ht;
   EeSetItem   *hi;
   CS name_ic;
   Unt   name_iclen;
   Ulong   hash;
   Byte name_folded[MAXKEYWLEN + 1];

   if (curPor->ownSyntax->b_syn_ic) {
      name_ic = str_foldcase(name, (int)namelen, name_folded, MAXKEYWLEN + 1);
      name_iclen = STRLEN(name_ic);
   } else {
      name_ic = name;
      name_iclen = namelen;
   }
   kp = alloc(offsetof(KeyEntry, keyword) + name_iclen + 1);
   STRCPY(kp->keyword, name_ic);
   kp->syntax.hiId = id;
   kp->syntax.inc_tag = current_syn_inc_tag;
   kp->flags = flags;
   kp->syntax.containedInHiId = copy_id_list(containedInHiId);
   if (containedInHiId != NULL)
      curPor->ownSyntax->b_syn_containedin = TRUE;
   kp->next_list = copy_id_list(next_list);

   if (curPor->ownSyntax->b_syn_ic)
      ht = &curPor->ownSyntax->keywordsIgnoreCase;
   else
      ht = &curPor->ownSyntax->keywords;

   Text keyw = mbText(kp->keyword);
   hash = calcHash(keyw);
   hi = hash_lookup(ht, keyw, hash);
   if (HASHITEM_EMPTY(hi)) {
      // new keyword, add to EeSet
      kp->next = NULL;
      hash_add_item(ht, hi, keyw, hash);
   } else {
      // keyword already exists, prepend to list
      kp->next = HI2KE(hi);
      hi->hi_key = KE2HIKEY(kp);
   }
}

//Get the start and end of the group name argument.
//Return a pointer to the first argument.
//Return NULL if the end of the command was found instead of further args.
private CS
get_group_name(
   CS arg,      // start of the argument
   OUT CS* name_end)   // pointer to end of the name
{
   *name_end = skiptowhite(arg);
   CS rest = skipwhite(*name_end);

   // Check if there are enough arguments.  The first argument may be a
   // pattern, where '|' is allowed, so only check for ZERO.
   return (endsComm(arg) || *rest == ZERO) ? null : rest;
}

//Check for syntax command option arguments.
//This can be called at any place in the list of arguments, and just picks
//out the arguments that are known.  Can be called several times in a row to
//collect all options in between other arguments.
//Return a pointer to the next argument (which isn't an option).
//Return NULL for any error;
private CS
get_syn_options(
   CS start,      // next argument to be checked
   SynOptArg* opt,      // various things
   int skip      // TRUE if skipping over command
) {
   CS arg = start;
   CS gname_start;
   CS gname;
   int      len;
   int      i;
   static struct flag {
      CS name;
      int argtype;
      int flags;
   } flagtab[] = {
         {S"cCoOnNtTaAiInNeEdD",   0,   HL_CONTAINED},
         {S"oOnNeElLiInNeE",      0,   HL_ONELINE},
         {S"kKeEeEpPeEnNdD",      0,   HL_KEEPEND},
         {S"eExXtTeEnNdD",      0,   HL_EXTEND},
         {S"eExXcClLuUdDeEnNlL",   0,   HL_EXCLUDENL},
         {S"tTrRaAnNsSpPaArReEnNtT",   0,   HL_TRANSP},
         {S"sSkKiIpPnNlL",      0,   HL_SKIPNL},
         {S"sSkKiIpPwWhHiItTeE",   0,   HL_SKIPWHITE},
         {S"sSkKiIpPeEmMpPtTyY",   0,   HL_SKIPEMPTY},
         {S"gGrRoOuUpPhHeErReE",   0,   HL_SYNC_HERE},
         {S"gGrRoOuUpPtThHeErReE",   0,   HL_SYNC_THERE},
         {S"dDiIsSpPlLaAyY",      0,   HL_DISPLAY},
         {S"fFoOlLdD",      0,   HL_FOLD},
         {S"cCoOnNcCeEaAlL",      0,   HL_CONCEAL},
         {S"cCoOnNcCeEaAlLeEnNdDsS",   0,   HL_CONCEALENDS},
         {S"cCcChHaArR",      11,   0},
         {S"cCoOnNtTaAiInNsS",   1,   0},
         {S"cCoOnNtTaAiInNeEdDiInN",   2,   0},
         {S"nNeExXtTgGrRoOuUpP",   3,   0},
   };
   static CS first_letters = S"cCoOkKeEtTsSgGdDfFnN";

   if (!arg)      // already detected error
      return NULL;

   for (;;) {
      //This is used very often when a large number of keywords is defined.
      //Need to skip quickly when no option name is found. Also avoid tolower(), it's slow.
      if (STRCHR(first_letters, *arg) == NULL)
         break;

      int fidx;
      for (fidx = ARRAY_LENGTH(flagtab); --fidx >= 0; ) {
         CS p = flagtab[fidx].name;
         for (i = 0, len = 0; p[i] != ZERO; i += 2, ++len) {
            if (arg[len] != p[i] && arg[len] != p[i + 1])
               break;
         } 
         if (p[i] == ZERO 
                && (SPACE_OR_TAB(arg[len])
                   || (flagtab[fidx].argtype > 0 ? arg[len] == '=' : endsComm(arg + len)))
         ) {
            if (opt->keyword
                     && (flagtab[fidx].flags == HL_DISPLAY
                         || flagtab[fidx].flags == HL_FOLD
                         || flagtab[fidx].flags == HL_EXTEND))
               // treat "display", "fold" and "extend" as a keyword
               fidx = -1;
            break;
         }
      }
      if (fidx < 0)       // no match found
          break;

      if (flagtab[fidx].argtype == 1) {
         if (!opt->has_containsHiId) {
            emsg(_(e_contains_argument_not_accepted_here));
            return NULL;
         }
         if (get_id_list(&arg, 8, &opt->containsHiId, skip) == FAIL)
            return NULL;
      } ei (flagtab[fidx].argtype == 2) {
         if (get_id_list(&arg, 11, &opt->containedInHiId, skip) == FAIL)
            return NULL;
      } ei (flagtab[fidx].argtype == 3) {
          if (get_id_list(&arg, 9, &opt->next_list, skip) == FAIL)
         return NULL;
      } ei (flagtab[fidx].argtype == 11 && arg[5] == '=') {
          // cchar=?
         arg += utfCharLen(arg + 6) - 1;
          arg = skipwhite(arg + 7);
      } else {
          opt->flags |= flagtab[fidx].flags;
          arg = skipwhite(arg + len);

         if (flagtab[fidx].flags == HL_SYNC_HERE || flagtab[fidx].flags == HL_SYNC_THERE) {
            if (opt->sync_idx == NULL) {
               emsg(_(e_groupthere_not_accepted_here));
               return NULL;
            }
            gname_start = arg;
            arg = skiptowhite(arg);
            if (gname_start == arg)
               return null;
            gname = copySubstr(gname_start, arg - gname_start);
            if (!gname)
               return null;
            if (STRCMP(gname, "NONE") == 0)
               *opt->sync_idx = NONE_IDX;
            else {
               Short hiId = hiliteGroupByName(mbText(gname));
               for (i = curPor->ownSyntax->syntaxPatterns.len; --i >= 0; )
               if (SYN_ITEMS(curPor->ownSyntax)[i].syntax.hiId == hiId
                     && SYN_ITEMS(curPor->ownSyntax)[i].sp_type == SPTYPE_START
               ){
                   *opt->sync_idx = i;
                   break;
               }
               if (i < 0) {
                  showErrFmtMsg(_(e_didnt_find_region_item_for_str), gname);
                  eeglFree(gname);
                  return NULL;
               }
            }

            eeglFree(gname);
            arg = skipwhite(arg);
         }
      }
   }

   return arg;
}

//Adjustments to syntax item when declared in a ":syn include"'d file.
//Set the contained flag, and if the item is not already contained, add it
//to the specified top-level group, if any.
private void
syn_incl_toplevel(int id, int *flagsp) {
   if ((*flagsp & HL_CONTAINED) || curPor->ownSyntax->b_syn_topgrp == 0)
      return;
   *flagsp |= HL_CONTAINED | HL_INCLUDED_TOPLEVEL;
   if (curPor->ownSyntax->b_syn_topgrp >= SYNID_CLUSTER) {
      // We have to alloc this, because syn_combine_list() will free it.
      Short* grp_list = ALLOC_MULT(Short, 2);
      int tlg_id = curPor->ownSyntax->b_syn_topgrp - SYNID_CLUSTER;

      if (grp_list) {
         grp_list[0] = id;
         grp_list[1] = 0;
         syn_combine_list(&SYN_CLSTR(curPor->ownSyntax)[tlg_id].hiIds, &grp_list, CLUSTER_ADD);
      }
   }
}

// Handle ":syntax include [@{group-name}] filename" command.
private void
syn_cmd_include(Invocation* invo, int syncing UNUSED) {
   CS arg = invo->arg;
   int      sgl_id = 1;
   CS group_name_end;
   CS rest;
   CS errorMsg = NULL;
   int      prev_toplvl_grp;
   int      prev_syn_inc_tag;
   int      source = FALSE;

   invo->nextComm = find_nextcmd(arg);
   if (invo->skip)
      return;

   if (arg[0] == '@') {
      ++arg;
      rest = get_group_name(arg, OUT &group_name_end);
      if (rest == NULL) {
         emsg(_(e_filename_required));
         return;
      }
      sgl_id = syn_check_cluster(arg, (int)(group_name_end - arg));
      if (sgl_id == 0)
         return;
      // separateNextCommand() and expand_filename() depend on this
      invo->arg = rest;
   }

   // Everything that's left, up to the next command, should be the filename to include.
   invo->argFlags |= (commandFlagExpandWildcards() | commandFlagNoSpacesInExtra());
   separateNextCommand(invo, FALSE);
   if (*invo->arg == '<' || *invo->arg == '$' || !fiIsRelative(invo->arg)) {
      //For an absolute path, "$EEGL/..." or "<sfile>.." we ":source" the
      //file.  Need to expand the file name first.  In other cases ":runtime!" is used.
      source = TRUE;
      if (expand_filename(invo, synCommline, OUT &errorMsg) == FAIL) {
         if (errorMsg)
            emsg(errorMsg);
         return;
      }
   }

   //Save and restore the existing top-level grouplist id and ":syn
   //include" tag around the actual inclusion.
   if (running_syn_inc_tag >= MAX_SYN_INC_TAG) {
      emsg(_(e_too_many_syntax_includes));
      return;
   }
   prev_syn_inc_tag = current_syn_inc_tag;
   current_syn_inc_tag = ++running_syn_inc_tag;
   prev_toplvl_grp = curPor->ownSyntax->b_syn_topgrp;
   curPor->ownSyntax->b_syn_topgrp = sgl_id;
   if (source ? scriptRunFile(invo->arg, NULL) == FAIL
            : source_runtime(invo->arg, DIP_ALL) == FAIL
   ) {
      _bp(true);
      showErrFmtMsg(_(e_cant_open_file_str), invo->arg);
   } 
   curPor->ownSyntax->b_syn_topgrp = prev_toplvl_grp;
   current_syn_inc_tag = prev_syn_inc_tag;
}

// Handle ":syntax keyword {group-name} [{option}] keyword .." command.
private void
syn_cmd_keyword(Invocation* invo, int syncing UNUSED) {
   CS arg = invo->arg;
   CS group_name_end;
   Short hiId;
   CS keyword_copy = NULL;
   CS p;
   CS kw;
   SynOptArg syn_opt_arg;
   int      cnt;

   CS rest = get_group_name(arg, OUT &group_name_end);

   if (rest) {
      if (invo->skip)
         hiId = SHORT;
      else
         hiId = hiliteGroupByName(mbText(arg));
      if (hiId != SHORT)
         // allocate a buffer, for removing backslashes in the keyword
         keyword_copy = alloc(STRLEN(rest) + 1);
      if (keyword_copy) {
         syn_opt_arg.flags = 0;
         syn_opt_arg.keyword = TRUE;
         syn_opt_arg.sync_idx = NULL;
         syn_opt_arg.has_containsHiId = false;
         syn_opt_arg.containedInHiId = NULL;
         syn_opt_arg.next_list = NULL;

         //The options given apply to ALL keywords, so all options must be
         //found before keywords can be created.
         //1: collect the options and copy the keywords to keyword_copy.
         cnt = 0;
         p = keyword_copy;
         for ( ; rest != NULL && !endsComm(rest); rest = skipwhite(rest)) {
            rest = get_syn_options(rest, &syn_opt_arg, invo->skip);
            if (rest == NULL || endsComm(rest))
               break;
            // Copy the keyword, removing backslashes, and add a ZERO.
            while (*rest != ZERO && !SPACE_OR_TAB(*rest)) {
               if (*rest == '\\' && rest[1] != ZERO)
                  ++rest;
               *p++ = *rest++;
            }
            *p++ = ZERO;
            ++cnt;
         }

         if (!invo->skip) {
            Unt   kwlen = 0;

            //Adjust flags for use of ":syn include".
            syn_incl_toplevel(hiId, &syn_opt_arg.flags);

            //2: Add an entry for each keyword.
            for (kw = keyword_copy; --cnt >= 0; kw += kwlen + 1) {
               for (p = firstOccurrence(kw, '['); ; ) {
                  if (p == NULL)
                      kwlen = STRLEN(kw);
                  else {
                      *p = ZERO;
                      kwlen = (Unt)(p - kw);
                  }
                  add_keyword(kw, kwlen, hiId, syn_opt_arg.flags, syn_opt_arg.containedInHiId,
                         syn_opt_arg.next_list);
                  if (!p)
                      break;
                  if (p[1] == ZERO) {
                      showErrFmtMsg(_(e_error_missing_rsb_str), kw);
                      goto error;
                  }
                  if (p[1] == ']') {
                     if (p[2] != ZERO) {
                        showErrFmtMsg(_(e_trailing_char_after_rsb_str_str), kw, &p[2]);
                        goto error;
                     }
                     kw = p + 1;      // skip over the "]"
                     kwlen = 1;
                     break;
                  }
                  int l = utfCharLen(p + 1);

                  mch_memmove(p, p + 1, l);
                  p += l;
               }
            }
         }
   error:
         eeglFree(keyword_copy);
         eeglFree(syn_opt_arg.containedInHiId);
         eeglFree(syn_opt_arg.next_list);
      }
   }

   if (rest)
     set_nextcmd(invo, rest);
   else
     showErrFmtMsg(_(e_invalid_argument_str), arg);

   drawCurBookLater(UPD_SOME_VALID);
   synFreeBlock(curPor->ownSyntax);      // Need to recompute all syntax.
}

//Handle ":syntax match {name} [{options}] {pattern} [{options}]".
//
//Also ":syntax sync match {name} [[grouphere | groupthere] {group-name}] .."
private void
syn_cmd_match( Invocation   *invo, int      syncing) {      // TRUE for ":syntax sync match .. "
   CS arg = invo->arg;
   SyntaxPattern   item;      // the item found in the line
   Unt      hiId;
   int      idx;
   SynOptArg syn_opt_arg;
   int      sync_idx = 0;
   int      orig_called_emsg = called_emsg;

   // Isolate the group name, check for validity
   CS group_name_end;
   CS rest = get_group_name(arg, OUT &group_name_end);

   // Get options before the pattern
   syn_opt_arg.flags = 0;
   syn_opt_arg.keyword = FALSE;
   syn_opt_arg.sync_idx = syncing ? &sync_idx : NULL;
   syn_opt_arg.has_containsHiId = true;
   syn_opt_arg.containsHiId = NULL;
   syn_opt_arg.containedInHiId = NULL;
   syn_opt_arg.next_list = NULL;
   rest = get_syn_options(rest, &syn_opt_arg, invo->skip);

   // get the pattern.
   init_syn_patterns();
   CLEAR_FIELD(item);
   Boole hadEol = false;
   rest = getSyntPattern(rest, &item, OUT &hadEol);
   if (hadEol && !(syn_opt_arg.flags & HL_EXCLUDENL))
      syn_opt_arg.flags |= HL_HAS_EOL;

   // Get options after the pattern
   rest = get_syn_options(rest, &syn_opt_arg, invo->skip);

   if (rest) {     // all arguments are valid
      // Check for trailing command and illegal trailing arguments.
      set_nextcmd(invo, rest);
      if (!endsComm(rest) || invo->skip)
         rest = NULL;
      ei (ga_grow(&curPor->ownSyntax->syntaxPatterns, 1) == OK 
            && (hiId = hiliteGroupByName(mbText(arg))) != SHORT
      ) {
         syn_incl_toplevel(hiId, &syn_opt_arg.flags);
         // Store the pattern in the syn_items list
         idx = curPor->ownSyntax->syntaxPatterns.len;
         SYN_ITEMS(curPor->ownSyntax)[idx] = item;
         SYN_ITEMS(curPor->ownSyntax)[idx].syncing = syncing;
         SYN_ITEMS(curPor->ownSyntax)[idx].sp_type = SPTYPE_MATCH;
         SYN_ITEMS(curPor->ownSyntax)[idx].syntax.hiId = hiId;
         SYN_ITEMS(curPor->ownSyntax)[idx].syntax.inc_tag = current_syn_inc_tag;
         SYN_ITEMS(curPor->ownSyntax)[idx].sp_flags = syn_opt_arg.flags;
         SYN_ITEMS(curPor->ownSyntax)[idx].sp_sync_idx = sync_idx;
         SYN_ITEMS(curPor->ownSyntax)[idx].sp_containsHiId = syn_opt_arg.containsHiId;
         SYN_ITEMS(curPor->ownSyntax)[idx].syntax.containedInHiId = syn_opt_arg.containedInHiId;
         if (syn_opt_arg.containedInHiId != NULL)
            curPor->ownSyntax->b_syn_containedin = TRUE;
         SYN_ITEMS(curPor->ownSyntax)[idx].sp_next_list = syn_opt_arg.next_list;
         ++curPor->ownSyntax->syntaxPatterns.len;

         // remember that we found a match for syncing on
         if (syn_opt_arg.flags & (HL_SYNC_HERE|HL_SYNC_THERE))
            curPor->ownSyntax->syncFlags |= SF_MATCH;
         if (syn_opt_arg.flags & HL_FOLD)
            ++curPor->ownSyntax->b_syn_folditems;

         drawCurBookLater(UPD_SOME_VALID);
         synFreeBlock(curPor->ownSyntax);   // Need to recompute all syntax.
         return;   // don't free the progs and patterns now
      }
   }

   //Something failed, free the allocated memory.
   eeRegFree(item.prog);
   eeglFree(item.pattern);
   eeglFree(syn_opt_arg.containsHiId);
   eeglFree(syn_opt_arg.containedInHiId);
   eeglFree(syn_opt_arg.next_list);

   if (!rest && called_emsg == orig_called_emsg)
      showErrFmtMsg(_(e_invalid_argument_str), arg);
}

// Handle ":syntax region {group-name} [matchgroup={group-name}]
//      start {start} .. [skip {skip}] end {end} .. [{options}]".
private void
syn_cmd_region(
   Invocation   *invo,
   int      syncing       // TRUE for ":syntax sync region .."
){
   CS arg = invo->arg;
   CS group_name_end;
   CS rest;         // next arg, NULL on error
   CS key_end;
   CS key = NULL;
   CS p;
   int         item;
#define ITEM_START      0
#define ITEM_SKIP       1
#define ITEM_END        2
#define ITEM_MATCHGROUP 3
   struct pat_ptr {
      SyntaxPattern* pattern;      // pointer to syn_pattern
      Short      pp_matchgroup_id;   // matchgroup ID
      struct pat_ptr   *pp_next;      // pointer to next pat_ptr
   }         *(pat_ptrs[3]);
               // patterns found in the line
   struct pat_ptr   *ppp;
   struct pat_ptr   *ppp_next;
   int         pat_count = 0;      // nr of syn_patterns found
   Short hiId;
   Short matchgroup_id = SHORT;
   int         not_enough = FALSE;   // not enough arguments
   int         illegal = FALSE;   // illegal arguments
   int         success = FALSE;
   int         idx;

   // Isolate the group name, check for validity
   rest = get_group_name(arg, OUT &group_name_end);

   pat_ptrs[0] = NULL;
   pat_ptrs[1] = NULL;
   pat_ptrs[2] = NULL;

   init_syn_patterns();

   SynOptArg   syn_opt_arg;
   syn_opt_arg.flags = 0;
   syn_opt_arg.keyword = FALSE;
   syn_opt_arg.sync_idx = NULL;
   syn_opt_arg.has_containsHiId = true;
   syn_opt_arg.containsHiId = NULL;
   syn_opt_arg.containedInHiId = NULL;
   syn_opt_arg.next_list = NULL;

   // get the options, patterns and matchgroup.
   while (rest && !endsComm(rest)) {
      // Check for option arguments
      rest = get_syn_options(rest, &syn_opt_arg, invo->skip);
      if (!rest || endsComm(rest))
         break;

      // must be a pattern or matchgroup then
      key_end = rest;
      while (*key_end && !SPACE_OR_TAB(*key_end) && *key_end != '=')
         ++key_end;
      eeglFree(key);
      key = copySubstr_up(rest, key_end - rest);
      if (!key) {        // out of memory
         rest = NULL;
         break;
      }
      if (STRCMP(key, "MATCHGROUP") == 0)
          item = ITEM_MATCHGROUP;
      ei (STRCMP(key, "START") == 0)
          item = ITEM_START;
      ei (STRCMP(key, "END") == 0)
          item = ITEM_END;
      ei (STRCMP(key, "SKIP") == 0) {
         if (pat_ptrs[ITEM_SKIP] != NULL) {  // one skip pattern allowed
            illegal = TRUE;
            break;
         }
         item = ITEM_SKIP;
      } else
        break;
      rest = skipwhite(key_end);
      if (*rest != '=') {
         rest = NULL;
         showErrFmtMsg(_(e_missing_equal_str), arg);
         break;
      }
      rest = skipwhite(rest + 1);
      if (*rest == ZERO) {
         not_enough = TRUE;
         break;
      }

      if (item == ITEM_MATCHGROUP) {
         p = skiptowhite(rest);
         if ((p - rest == 4 && STRNCMP(rest, "NONE", 4) == 0) || invo->skip)
            matchgroup_id = SHORT;
         else {
            matchgroup_id = hiliteGroupByName(mbText(rest));
            if (matchgroup_id == SHORT) {
               illegal = TRUE;
               break;
            }
         }
         rest = skipwhite(p);
      } else {
         //Allocate room for a syn_pattern, and link it in the list of syn_patterns for this item,
         //at the start (because the list is used from end to start).
         ppp = ALLOC_ONE(struct pat_ptr);
         if (!ppp) {
            rest = NULL;
            break;
         }
         ppp->pp_next = pat_ptrs[item];
         pat_ptrs[item] = ppp;
         ppp->pattern = ALLOC_CLEAR_ONE(SyntaxPattern);
         if (!ppp->pattern) {
            rest = NULL;
            break;
         }

         //Get the syntax pattern and the following offset(s).
         //Enable the appropriate \z specials.
         if (item == ITEM_START)
            reg_do_extmatch = REX_SET;
         ei (item == ITEM_SKIP || item == ITEM_END)
            reg_do_extmatch = REX_USE;
         Boole hadEol = false; 
         rest = getSyntPattern(rest, ppp->pattern, OUT &hadEol);
         reg_do_extmatch = 0;
         if (item == ITEM_END && hadEol && !(syn_opt_arg.flags & HL_EXCLUDENL))
            ppp->pattern->sp_flags |= HL_HAS_EOL;
         ppp->pp_matchgroup_id = matchgroup_id;
         ++pat_count;
      }
   }
   eeglFree(key);
   if (illegal || not_enough)
      rest = NULL;

   // Must have a "start" and "end" pattern.
   if (rest && (pat_ptrs[ITEM_START] == NULL || pat_ptrs[ITEM_END] == NULL)) {
      not_enough = TRUE;
      rest = NULL;
   }

   if (rest) {
      // Check for trailing garbage or command. If OK, add the item.
      set_nextcmd(invo, rest);
      if (!endsComm(rest) || invo->skip)
         rest = NULL;
      ei (ga_grow(&(curPor->ownSyntax->syntaxPatterns), pat_count) == OK 
            && (hiId = hiliteGroupByName(mbText(arg))) != SHORT
      ) {
          syn_incl_toplevel(hiId, &syn_opt_arg.flags);
         // Store the start/skip/end in the syn_items list
         idx = curPor->ownSyntax->syntaxPatterns.len;
         for (item = ITEM_START; item <= ITEM_END; ++item) {
            for (ppp = pat_ptrs[item]; ppp != NULL; ppp = ppp->pp_next) {
                SYN_ITEMS(curPor->ownSyntax)[idx] = *(ppp->pattern);
                SYN_ITEMS(curPor->ownSyntax)[idx].syncing = syncing;
                SYN_ITEMS(curPor->ownSyntax)[idx].sp_type =
                   (item == ITEM_START) ? SPTYPE_START :
                   (item == ITEM_SKIP) ? SPTYPE_SKIP : SPTYPE_END;
               SYN_ITEMS(curPor->ownSyntax)[idx].sp_flags |= syn_opt_arg.flags;
               SYN_ITEMS(curPor->ownSyntax)[idx].syntax.hiId = hiId;
               SYN_ITEMS(curPor->ownSyntax)[idx].syntax.inc_tag = current_syn_inc_tag;
               SYN_ITEMS(curPor->ownSyntax)[idx].patternHiId = ppp->pp_matchgroup_id;
               if (item == ITEM_START) {
                  SYN_ITEMS(curPor->ownSyntax)[idx].sp_containsHiId = syn_opt_arg.containsHiId;
                  SYN_ITEMS(curPor->ownSyntax)[idx].syntax.containedInHiId = syn_opt_arg.containedInHiId;
                  if (syn_opt_arg.containedInHiId != NULL)
                      curPor->ownSyntax->b_syn_containedin = TRUE;
                  SYN_ITEMS(curPor->ownSyntax)[idx].sp_next_list = syn_opt_arg.next_list;
               }
               ++curPor->ownSyntax->syntaxPatterns.len;
               ++idx;
               if (syn_opt_arg.flags & HL_FOLD)
                  ++curPor->ownSyntax->b_syn_folditems;
            }
         }

         drawCurBookLater(UPD_SOME_VALID);
         synFreeBlock(curPor->ownSyntax);   // Need to recompute all syntax.
         success = TRUE;       // don't free the progs and patterns now
      }
   }

   //Free the allocated memory.
   for (item = ITEM_START; item <= ITEM_END; ++item) {
      for (ppp = pat_ptrs[item]; ppp; ppp = ppp_next) {
         if (!success && ppp->pattern != NULL) {
            eeRegFree(ppp->pattern->prog);
            eeglFree(ppp->pattern->pattern);
         }
         eeglFree(ppp->pattern);
         ppp_next = ppp->pp_next;
         eeglFree(ppp);
      }
   } 

   if (!success) {
      eeglFree(syn_opt_arg.containsHiId);
      eeglFree(syn_opt_arg.containedInHiId);
      eeglFree(syn_opt_arg.next_list);
      if (not_enough)
         showErrFmtMsg(_(e_not_enough_arguments_syntax_region_str), arg);
      ei (illegal || rest == NULL)
         showErrFmtMsg(_(e_invalid_argument_str), arg);
   }
}

//A simple syntax group ID comparison function suitable for use in qsort()
private int
syn_compare_stub(const void *v1, const void *v2) {
   const Short   *s1 = v1;
   const Short   *s2 = v2;
   return (*s1 > *s2 ? 1 : *s1 < *s2 ? -1 : 0);
}

//Combine lists of syntax clusters.
//*clstr1 and *clstr2 must both be allocated memory; they will be consumed.
private void
syn_combine_list(Short **clstr1, Short **clstr2, int list_op) {
   int      count1 = 0;
   int      count2 = 0;
   Short   *g1;
   Short   *g2;
   Short   *clstr = NULL;
   int      count;

   // Handle degenerate cases.
   if (*clstr2 == NULL)
      return;
   if (*clstr1 == NULL || list_op == CLUSTER_REPLACE) {
      if (list_op == CLUSTER_REPLACE)
         eeglFree(*clstr1);
      if (list_op == CLUSTER_REPLACE || list_op == CLUSTER_ADD)
         *clstr1 = *clstr2;
      else
         eeglFree(*clstr2);
      return;
   }

   for (g1 = *clstr1; *g1; g1++)
      ++count1;
   for (g2 = *clstr2; *g2; g2++)
      ++count2;

   // For speed purposes, sort both lists.
   qsort(*clstr1, (Unt)count1, sizeof(Short), syn_compare_stub);
   qsort(*clstr2, (Unt)count2, sizeof(Short), syn_compare_stub);

   // We proceed in two passes; in round 1, we count the elements to place in the new list, and in 
   // round 2, we allocate and populate the new list.  For speed, we use a mergesort-like method, 
   // adding the smaller of the current elements in each list to the new list.
   for (int round = 1; round <= 2; round++) {
      g1 = *clstr1;
      g2 = *clstr2;
      count = 0;

      // First, loop through the lists until one of them is empty.
      while (*g1 && *g2) {
         // We always want to add from the first list.
         if (*g1 < *g2) {
            if (round == 2)
               clstr[count] = *g1;
            count++;
            g1++;
            continue;
         }
         //We only want to add from the second list if we're adding the lists.
         if (list_op == CLUSTER_ADD) {
            if (round == 2)
               clstr[count] = *g2;
            count++;
         }
         if (*g1 == *g2)
            g1++;
         g2++;
      }

      //Now add the leftovers from whichever list didn't get finished
      //first.  As before, we only want to add from the second list if we're adding the lists.
      for (; *g1; g1++, count++) {
         if (round == 2)
            clstr[count] = *g1;
      } 
      if (list_op == CLUSTER_ADD)
         for (; *g2; g2++, count++) {
            if (round == 2)
                clstr[count] = *g2;
         }

      if (round == 1) {
         //If the group ended up empty, we don't need to allocate any space for it.
         if (count == 0) {
            clstr = NULL;
            break;
         }
         clstr = ALLOC_MULT(Short, count + 1);
         clstr[count] = 0;
      }
   }

   // Finally, put the new list in place.
   eeglFree(*clstr1);
   eeglFree(*clstr2);
   *clstr1 = clstr;
}

// Lookup a syntax cluster name and return its ID. If it is not found, SHORT is returned.
private Short
clusterByName(CS name) {
   // Avoid using stricmp() too much, it's slow on some systems
   CS name_u = copyStr_up(name);
   if (!name_u)
      return 0;
   Short      i;
   for (i = curPor->ownSyntax->syntaxClusters.len; --i != SHORT; ) {
      if (SYN_CLSTR(curPor->ownSyntax)[i].nameUpper != NULL
            && STRCMP(name_u, SYN_CLSTR(curPor->ownSyntax)[i].nameUpper) == 0
      )
         break;
   } 
   eeglFree(name_u);
   return (i == SHORT ? SHORT : i + SYNID_CLUSTER);
}

// Lookup a syntax cluster name and return its ID. If it is not found, SHORT is returned.
Short
syntaxClusterByName(Text line) {
   CS name = copySubstr(line.c, line.len);
   if (!name)
      return 0;

   Short id = clusterByName(name);
   eeglFree(name);
   return id;
}

// Find syntax cluster name in the table and return its ID. The argument is a pointer to the name 
// and the length of the name. If it doesn't exist yet, a new entry is created. Return 0 for 
// failure.
private int
syn_check_cluster(CS pp, int len) {
   CS name = copySubstr(pp, len);
   if (!name)
      return 0;

   int id = clusterByName(name);
   if (id == SHORT)         // doesn't exist yet
      id = addCluster(name);
   else
      eeglFree(name);
   return id;
}

//Add new syntax cluster and return its ID.
//"name" must be an allocated string, it will be consumed. Return 0 for failure.
private int
addCluster(CS name) {
   // First call for this growarray: init growing array.
   if (curPor->ownSyntax->syntaxClusters.c == NULL) {
      curPor->ownSyntax->syntaxClusters.ga_itemsize = sizeof(SynCluster);
      curPor->ownSyntax->syntaxClusters.ga_growsize = 10;
   }

   int len = curPor->ownSyntax->syntaxClusters.len;
   if (len >= MAX_CLUSTER_ID) {
      emsg(_(e_too_many_syntax_clusters));
      eeglFree(name);
      return 0;
   }

   //  Make room for at least one other cluster entry.
   if (ga_grow(&curPor->ownSyntax->syntaxClusters, 1) == FAIL) {
      eeglFree(name);
      return 0;
   }

   CLEAR_POINTER(&(SYN_CLSTR(curPor->ownSyntax)[len]));
   SYN_CLSTR(curPor->ownSyntax)[len].name = name;
   SYN_CLSTR(curPor->ownSyntax)[len].nameUpper = copyStr_up(name);
   SYN_CLSTR(curPor->ownSyntax)[len].hiIds = NULL;
   ++curPor->ownSyntax->syntaxClusters.len;

   if (caseInsensitiveCompare(name, "Spell") == 0)
      curPor->ownSyntax->spellClusterId = len + SYNID_CLUSTER;
   if (caseInsensitiveCompare(name, "NoSpell") == 0)
      curPor->ownSyntax->noSpellClusterId = len + SYNID_CLUSTER;

   return len + SYNID_CLUSTER;
}

//Handle ":syntax cluster {cluster-name} [contains={groupname},..]
//     [add={groupname},..] [remove={groupname},..]".
private void
syn_cmd_cluster(Invocation* invo, int syncing UNUSED) {
   CS arg = invo->arg;
   CS group_name_end;
   CS rest;
   int      scl_id;
   int      got_clstr = FALSE;
   int      opt_len;
   int      list_op;

   invo->nextComm = find_nextcmd(arg);
   if (invo->skip)
      return;

   rest = get_group_name(arg, OUT &group_name_end);

   if (rest) {
      scl_id = syn_check_cluster(arg, (int)(group_name_end - arg));
      if (scl_id == 0)
         return;
      scl_id -= SYNID_CLUSTER;

      for (;;) {
         if (STRNICMP(rest, "add", 3) == 0
             && (SPACE_OR_TAB(rest[3]) || rest[3] == '='))
          {
            opt_len = 3;
            list_op = CLUSTER_ADD;
         } ei (STRNICMP(rest, "remove", 6) == 0 && (SPACE_OR_TAB(rest[6]) || rest[6] == '=')) {
            opt_len = 6;
            list_op = CLUSTER_SUBTRACT;
         } ei (STRNICMP(rest, "contains", 8) == 0
            && (SPACE_OR_TAB(rest[8]) || rest[8] == '='))
          {
            opt_len = 8;
            list_op = CLUSTER_REPLACE;
         } else
            break;

         Short* cluster = NULL;
         if (get_id_list(&rest, opt_len, OUT &cluster, invo->skip) == FAIL) {
            showErrFmtMsg(_(e_invalid_argument_str), rest);
            break;
         }
         if (scl_id >= 0)
            syn_combine_list(&SYN_CLSTR(curPor->ownSyntax)[scl_id].hiIds, &cluster, list_op);
         else
            eeglFree(cluster);
         got_clstr = TRUE;
      }

      if (got_clstr) {
          drawCurBookLater(UPD_SOME_VALID);
          synFreeBlock(curPor->ownSyntax);   // Need to recompute all.
      }
   }

   if (!got_clstr)
      emsg(_(e_no_cluster_specified));
   if (rest == NULL || !endsComm(rest))
      showErrFmtMsg(_(e_invalid_argument_str), arg);
}

// On first call for current buffer: Init growing array.
private void
init_syn_patterns(void) {
   curPor->ownSyntax->syntaxPatterns.ga_itemsize = sizeof(SyntaxPattern);
   curPor->ownSyntax->syntaxPatterns.ga_growsize = 10;
}

//Get one pattern for a ":syntax match" or ":syntax region" command.
//Store the pattern and program in a SyntaxPattern.
//Returns a pointer to the next argument, or NULL in case of an error.
private CS
getSyntPattern(CS arg, SyntaxPattern *ci, OUT Boole* hadEol) {
   int      *p;
   int      idx;

   // need at least three chars
   if (arg == NULL || arg[0] == ZERO || arg[1] == ZERO || arg[2] == ZERO)
      return NULL;

   CS end = skip_regexp(arg + 1, *arg, TRUE);
   if (*end != *arg) {            // end delimiter not found
      showErrFmtMsg(_(e_pattern_delimiter_not_found_str), arg);
      return NULL;
   }
   // store the pattern and compiled regexp program
   if ((ci->pattern = copySubstr(arg + 1, end - arg - 1)) == NULL)
      return NULL;

   ci->prog = compileRegexp(ci->pattern, RE_MAGIC);

   if (ci->prog == NULL)
      return NULL;
   *hadEol = regexContainsEol(ci->prog);
   ci->sp_ic = curPor->ownSyntax->b_syn_ic;

   // Check for a match, highlight or region offset.
   ++end;
   do {
      for (idx = SPO_COUNT; --idx >= 0; ) {
         if (STRNCMP(end, spo_name_tab[idx], 3) == 0)
            break;
      } 
      if (idx >= 0) {
         p = &(ci->sp_offsets[idx]);
         if (idx != SPO_LC_OFF) {
            switch (end[3]) {
            case 's':   break;
            case 'b':   break;
            case 'e':   idx += SPO_COUNT; break;
            default:    idx = -1; break;
            }
         } 
         
         if (idx >= 0) {
            ci->sp_off_flags |= (1 << idx);
            if (idx == SPO_LC_OFF) {      // lc=99
               end += 3;
               *p = parseLong(&end);

               // "lc=" offset automatically sets "ms=" offset
               if (!(ci->sp_off_flags & (1 << SPO_MS_OFF))) {
                  ci->sp_off_flags |= (1 << SPO_MS_OFF);
                  ci->sp_offsets[SPO_MS_OFF] = *p;
                }
            } else {            // yy=x+99
               end += 4;
               if (*end == '+') {
                  ++end;
                  *p = parseLong(&end);      // positive offset
               } ei (*end == '-') {
                  ++end;
                  *p = -parseLong(&end);      // negative offset
               }
            }
            if (*end != ',')
               break;
            ++end;
          }
      }
   } while (idx >= 0);

   if (!endsComm(end) && !SPACE_OR_TAB(*end)) {
      showErrFmtMsg(_(e_garbage_after_pattern_str), arg);
      return NULL;
   }
   return skipwhite(end);
}

// Handle ":syntax sync .." command.
private void
syn_cmd_sync(Invocation* invo, int syncing UNUSED) {
    CS arg_start = invo->arg;
    CS arg_end;
    CS key = NULL;
    CS next_arg;
    int      illegal = FALSE;
    int      finished = FALSE;
    long   n;

   if (endsComm(arg_start)) {
      syn_cmd_list(invo, TRUE);
      return;
   }

   while (!endsComm(arg_start)) {
      arg_end = skiptowhite(arg_start);
      next_arg = skipwhite(arg_end);
      eeglFree(key);
      key = copySubstr_up(arg_start, arg_end - arg_start);
      if (key == NULL)
          break;
      if (STRCMP(key, "CCOMMENT") == 0) {
         if (!invo->skip)
            curPor->ownSyntax->syncFlags |= SF_CCOMMENT;
         if (!endsComm(next_arg)) {
            arg_end = skiptowhite(next_arg);
            if (!invo->skip)
                curPor->ownSyntax->syncHiId = hiliteGroupByName(mbText(next_arg));
            next_arg = skipwhite(arg_end);
         } ei (!invo->skip)
            curPor->ownSyntax->syncHiId = hiliteGroupByName(tConst("Comment"));
      }
      ei (  STRNCMP(key, "LINES", 5) == 0
         || STRNCMP(key, "MINLINES", 8) == 0
         || STRNCMP(key, "MAXLINES", 8) == 0
         || STRNCMP(key, "LINEBREAKS", 10) == 0
      ){
         if (key[4] == 'S')
            arg_end = key + 6;
         ei (key[0] == 'L')
            arg_end = key + 11;
         else
            arg_end = key + 9;
         if (arg_end[-1] != '=' || !EE_ISDIGIT(*arg_end)) {
            illegal = TRUE;
            break;
         }
         n = parseLong(&arg_end);
         if (!invo->skip) {
            if (key[4] == 'B')
               curPor->ownSyntax->syncLinebreaks = n;
            ei (key[1] == 'A')
               curPor->ownSyntax->b_syn_sync_maxlines = n;
            else
               curPor->ownSyntax->b_syn_sync_minlines = n;
         }
      } ei (STRCMP(key, "FROMSTART") == 0) {
          if (!invo->skip) {
         curPor->ownSyntax->b_syn_sync_minlines = MAXLNUM;
         curPor->ownSyntax->b_syn_sync_maxlines = 0;
          }
      } ei (STRCMP(key, "LINECONT") == 0) {
          if (*next_arg == ZERO) {     // missing pattern
         illegal = TRUE;
         break;
         }
         if (curPor->ownSyntax->lineContinuationPattern != NULL) {
            emsg(_(e_syntax_sync_line_continuations_pattern_specified_twice));
            finished = TRUE;
            break;
         }
         arg_end = skip_regexp(next_arg + 1, *next_arg, TRUE);
         if (*arg_end != *next_arg)  {     // end delimiter not found
            illegal = TRUE;
            break;
         }

         if (!invo->skip) {
            // store the pattern and compiled regexp program
            if ((curPor->ownSyntax->lineContinuationPattern =
                   copySubstr(next_arg + 1, arg_end - next_arg - 1)) == NULL
            ){
                finished = TRUE;
                break;
            }
            curPor->ownSyntax->lineContinIgnoreCase = curPor->ownSyntax->b_syn_ic;

            curPor->ownSyntax->lineContinProg = 
               compileRegexp(curPor->ownSyntax->lineContinuationPattern, RE_MAGIC);

            if (curPor->ownSyntax->lineContinProg == NULL) {
                EE_CLEAR(curPor->ownSyntax->lineContinuationPattern);
                finished = TRUE;
                break;
            }
         }
          next_arg = skipwhite(arg_end + 1);
      } else {
         invo->arg = next_arg;
         if (STRCMP(key, "MATCH") == 0)
            syn_cmd_match(invo, TRUE);
         ei (STRCMP(key, "REGION") == 0)
            syn_cmd_region(invo, TRUE);
         ei (STRCMP(key, "CLEAR") == 0)
            clearSubcommand(invo, TRUE);
         else
            illegal = TRUE;
         finished = TRUE;
         break;
      }
      arg_start = next_arg;
   }
   eeglFree(key);
   if (illegal)
      showErrFmtMsg(_(e_illegal_arguments_str), arg_start);
   ei (!finished) {
      set_nextcmd(invo, arg_start);
      drawCurBookLater(UPD_SOME_VALID);
      synFreeBlock(curPor->ownSyntax);   // Need to recompute all syntax.
   }
}

//Convert a line of hilite group names into a list of group ID numbers.
//"arg" should point to the "contains" or "nextgroup" keyword.
//"arg" is advanced to after the last group name.
//Careful: the argument is modified (NULs added). return FAIL for some error, OK for success.
private int
get_id_list(
   Byte   **arg,
   int      keylen,      // length of keyword
   OUT Arr(Short)* list, // where to store the resulting list. (if not NULL, has no effect)
   int      skip
) {
   CS p = NULL;
   CS end;
   int      round;
   int      count;
   int      total_count = 0;
   Arr(Short) retval = NULL;
   CS name;
   RegMatch   regmatch;
   Short      id;
   int      i;
   int      failed = FALSE;

   //We parse the list twice:
   //round == 1: count the number of items, allocate the array.
   //round == 2: fill the array with the items.
   //In round 1 new groups may be added, causing the number of items to
   //grow when a regexp is used.  In that case round 1 is done once again.
   for (round = 1; round <= 2; ++round) {
      //skip "contains"
      p = skipwhite(*arg + keylen);
      if (*p != '=') {
         showErrFmtMsg(_(e_missing_equal_sign_str), *arg);
         break;
      }
      p = skipwhite(p + 1);
      if (endsComm(*arg)) {
         showErrFmtMsg(_(e_empty_argument_str), *arg);
         break;
      }

      // parse the arguments after "contains"
      count = 0;
      while (!endsComm(p)) {
         for (end = p; *end && !SPACE_OR_TAB(*end) && *end != ','; ++end)
            {}
         name = alloc(end - p + 3);       // leave room for "^$"
         copySubstrToAllocation(name + 1, (Text){p, end - p});
         if (   STRCMP(name + 1, "ALLBUT") == 0
             || STRCMP(name + 1, "ALL") == 0
             || STRCMP(name + 1, "TOP") == 0
             || STRCMP(name + 1, "CONTAINED") == 0
         ){
            if (TOUPPER_ASC(**arg) != 'C') {
               showErrFmtMsg(_(e_str_not_allowed_here), name + 1);
               failed = TRUE;
               eeglFree(name);
               break;
            }
            if (count != 0) {
                showErrFmtMsg(_(e_str_must_be_first_in_contains_list), name + 1);
                failed = TRUE;
                eeglFree(name);
                break;
            }
            if (name[1] == 'A')
                id = SYNID_ALLBUT;
            ei (name[1] == 'T')
                id = SYNID_TOP;
            else
                id = SYNID_CONTAINED;
            id += current_syn_inc_tag;
         } ei (name[1] == '@') {
            if (skip)
               id = SHORT;
            else
               id = syn_check_cluster(name + 2, (int)(end - p - 1));
         } else {
            // Handle full group name.
            if (eeStrpbrk(name + 1, (CS)"\\.*^$~[") == NULL)
               id = hiliteGroupByName(text(name + 1));
            else {
               // Handle match of regexp with group names.
               *name = '^';
               STRCAT(name, "$");
               regmatch.regprog = compileRegexp(name, RE_MAGIC);
               if (regmatch.regprog == NULL) {
                  failed = TRUE;
                  eeglFree(name);
                  break;
               }

               regmatch.rm_ic = TRUE;
               id = SHORT;
               for (i = countDecosG; --i >= 0; ) {
                  if (eeRegexec(&regmatch, hiliteGroupName(i).c, (ColNr)0)) {
                     if (round == 2) {
                        // Got more items than expected; can happen
                        // when adding items that match: "contains=a.*b,axb".
                        // Go back to first round
                        if (count >= total_count) {
                            eeglFree(retval);
                            round = 1;
                        } else
                            retval[count] = i + 1;
                     }
                     ++count;
                     id = SHORT;       // remember that we found one
                  }
               }
               eeRegFree(regmatch.regprog);
            }
         }
         eeglFree(name);
         if (id == SHORT) {
            showErrFmtMsg(_(e_unknown_group_name_str), p);
            failed = TRUE;
            break;
         }
         if (id > 0) {
            if (round == 2) {
               // Got more items than expected, go back to first round
               if (count >= total_count) {
                  eeglFree(retval);
                  round = 1;
               } else
                  retval[count] = id;
            }
            ++count;
         }
         p = skipwhite(end);
         if (*p != ',')
            break;
         p = skipwhite(p + 1);   // skip comma in between arguments
      }
      if (failed)
         break;
      if (round == 1) {
         retval = ALLOC_MULT(Short, count + 1);
         retval[count] = 0;       // zero means end of the list
         total_count = count;
      }
    }

   *arg = p;
   if (failed || retval == NULL) {
      eeglFree(retval);
      return FAIL;
   }

   if (*list == NULL)
      *list = retval;
   else
      eeglFree(retval);   // list already found, don't overwrite it

   return OK;
}

// Make a copy of an ID list.
private Short *
copy_id_list(Short *list) {
   int       len;
   int       count;
   Short   *retval;

   if (!list)
      return NULL;

   for (count = 0; list[count]; ++count)
   {}
   len = (count + 1) * sizeof(Short);
   retval = alloc(len);

   return retval;
}

//Check if syntax group "ssp" is in the ID list "list" of "currStateItem".
//"currStateItem" can be NULL if not checking the "containedin" list.
//Used to check if a syntax item is in the "contains" or "nextgroup" list of the current item.
//This function is called very often, keep it fast!!
private int
in_id_list(
   StateItem   *currStateItem,   // current item or NULL
   Arr(Short) list,      // id list
   SyntaxInfo* ssp,      // group id and ":syn include" tag of group
   int      flags)      // group flags
{
   int      retval;
   Arr(Short) hiIds;
   Short   item;
   Short   hiId = ssp->hiId;
   static int   depth = 0;
   int      r;
   int      toplevel;

   // If ssp has a "containedin" list and "currStateItem" is in it, return TRUE.
   if (currStateItem != NULL 
         && ssp->containedInHiId != NULL 
         && !(currStateItem->si_flags & HL_MATCH)
   ) {
      // Ignore transparent items without a contains argument.  Double check that we don't go back 
      // past the first one.
      while ((currStateItem->si_flags & HL_TRANS_CONT)
         && currStateItem > (StateItem *)(current_state.c))
          --currStateItem;
      // currStateItem->si_idx is -1 for keywords, these never contain anything.
      if (currStateItem->si_idx >= 0 && in_id_list(NULL, ssp->containedInHiId,
         &(SYN_ITEMS(synBlockS)[currStateItem->si_idx].syntax),
           SYN_ITEMS(synBlockS)[currStateItem->si_idx].sp_flags))
          return TRUE;
   }

   if (!list)
      return FALSE;

   // If list is ID_LIST_ALL, we are in a transparent item that isn't inside anything. Only allow 
   // not-contained groups.
   if (list == ID_LIST_ALL)
      return !(flags & HL_CONTAINED);

    //Is this top-level (i.e. not 'contained') in the file it was declared in?
    //For included files, this is different from HL_CONTAINED, which is set unconditionally.
    toplevel = !(flags & HL_CONTAINED) || (flags & HL_INCLUDED_TOPLEVEL);

   //If the first item is "ALLBUT", return TRUE if "id" is NOT in the
   //contains list.  We also require that "id" is at the same ":syn include"
   //level as the list.
   item = *list;
   if (item >= SYNID_ALLBUT && item < SYNID_CLUSTER) {
      if (item < SYNID_TOP) {
          // ALL or ALLBUT: accept all groups in the same file
          if (item - SYNID_ALLBUT != ssp->inc_tag)
         return FALSE;
      } ei (item < SYNID_CONTAINED) {
          // TOP: accept all not-contained groups in the same file
          if (item - SYNID_TOP != ssp->inc_tag || !toplevel)
         return FALSE;
      } else {
          // CONTAINED: accept all contained groups in the same file
          if (item - SYNID_CONTAINED != ssp->inc_tag || toplevel)
         return FALSE;
      }
      item = *++list;
      retval = FALSE;
   } else
      retval = TRUE;

   // Return "retval" if id is in the contains list.
   while (item != 0) {
      if (item == hiId)
          return retval;
      if (item >= SYNID_CLUSTER) {
         hiIds = SYN_CLSTR(synBlockS)[item - SYNID_CLUSTER].hiIds;
         // restrict recursiveness to 30 to avoid an endless loop for a
         // cluster that includes itself (indirectly)
         if (hiIds && depth < 30) {
            ++depth;
            r = in_id_list(NULL, hiIds, ssp, flags);
            --depth;
            if (r)
               return retval;
         }
      }
      item = *++list;
   }
   return !retval;
}

typedef struct subcommand {
   CS name;         // subcommand name
   void (*fn)(Invocation *, int);   // function to call
} Subcommand;

private Subcommand subcommands[] = { SMAP1((CS),
   "case",      caseSubcommand,
   "clear",      clearSubcommand,
   "cluster",      syn_cmd_cluster,
   "enable",      syn_cmd_enable,
   "foldlevel",   syn_cmd_foldlevel,
   "include",      syn_cmd_include,
   "iskeyword",   syn_cmd_iskeyword,
   "keyword",      syn_cmd_keyword,
   "list",      syn_cmd_list,
   "manual",      syn_cmd_manual,
   "match",      syn_cmd_match,
   "on",      theOnSubcommand,
   "off",      offSubcommand,
   "region",      syn_cmd_region,
   "reset",      syn_cmd_reset,
   "spell",      syn_cmd_spell,
   "sync",      syn_cmd_sync,
   "",      syn_cmd_list
   ) 
};

//":syntax". Search the subcommands[] table for the subcommand name, and call a
//syntax_subcommand() function to do the rest.
void
c_syntax(Invocation* invo) {
   CS arg = invo->arg;
   CS subcmd_end;
   CS subcmd_name;
   int      i;

   synCommline = invo->commline;

   // isolate subcommand name
   for (subcmd_end = arg; ASCII_ISALPHA(*subcmd_end); ++subcmd_end)
      {}
   subcmd_name = copySubstr(arg, subcmd_end - arg);
   if (subcmd_name == NULL)
      return;

   if (invo->skip)      // skip error messages for all subcommands
      ++emsg_skip;
   for (i = 0; i < (int)ARRAY_LENGTH(subcommands); ++i) {
      if (STRCMP(subcmd_name, (CS)subcommands[i].name) == 0) {
          invo->arg = skipwhite(subcmd_end);
          (subcommands[i].fn)(invo, FALSE);
          break;
      }
   }

   if (i == (int)ARRAY_LENGTH(subcommands))
      showErrFmtMsg(_(e_invalid_syntax_subcommand_str), subcmd_name);

   eeglFree(subcmd_name);
   if (invo->skip)
      --emsg_skip;
}

void
c_ownsyntax(Invocation* invo) {
   CS old_value;
   CS new_value;

   if (curPor->ownSyntax == &curPor->book->syntax) {
      curPor->ownSyntax = ALLOC_ONE(SyntaxBlock);
      CLEAR_POINTER(curPor->ownSyntax);
      hash_init(&curPor->ownSyntax->keywords);
      hash_init(&curPor->ownSyntax->keywordsIgnoreCase);
   }

   // save value of b:currentSyntax
   old_value = get_var_value(BUFF_SYN_VAR);
   if (old_value)
      old_value = copyStr(old_value);

   //Apply the "syntax" autocommand event, this finds and loads the syntax file.
   applyAutocomms(EVENT_SYNTAX, invo->arg, curBook->currFileName, TRUE, curBook);

   //move value of b:currentSyntax to w:currentSyntax
   new_value = get_var_value(BUFF_SYN_VAR);
   if (new_value != NULL)
      set_internal_string_var(PORT_SYN_VAR, new_value);

   // restore value of b:currentSyntax
   if (!old_value)
      unletImpl(BUFF_SYN_VAR, true);
   else {
      set_internal_string_var(BUFF_SYN_VAR, old_value);
      eeglFree(old_value);
   }
}

int
syntax_present(Portal* po) {
   return (po->ownSyntax->syntaxPatterns.len != 0
       || po->ownSyntax->syntaxClusters.len != 0
       || po->ownSyntax->keywords.count > 0
       || po->ownSyntax->keywordsIgnoreCase.count > 0);
}

private enum {
   EXP_SUBCMD,       // expand ":syn" sub-commands
   EXP_CASE,       // expand ":syn case" arguments
   EXP_SPELL,       // expand ":syn spell" arguments
   EXP_SYNC,       // expand ":syn sync" arguments
   EXP_CLUSTER       // expand ":syn list @cluster" arguments
} expand_what;

// Called when we are done expandin'
void
reset_expand_highlight(void) {
   hiComplIncludeNoneG = 0;
   hiComplIncludeDefaultG = 0;
   hiComplIncludeLinkG = 0;
}

// Handle command line completion for :match and :echohl command: Add "NONE" as hilite group.
void
set_context_in_echohl_cmd(Expand *xp, CS arg) {
   xp->context = EXPAND_HILITE_GROUP;
   xp->input = mbText(arg);
   hiComplIncludeNoneG = 1;
}

// Handle command line completion for :syntax command.
void
set_context_in_syntax_cmd(Expand *xp, CS arg) {
   // Default: expand subcommands
   xp->context = EXPAND_SYNTAX;
   expand_what = EXP_SUBCMD;
   xp->input = mbText(arg);
   hiComplIncludeLinkG = 0;
   hiComplIncludeDefaultG = 0;

   if (*arg == ZERO)
      return;

   // (part of) subcommand already typed
   CS p = skiptowhite(arg);
   if (*p == ZERO)
      return;

   // past first word
   p = skipwhite(p);
   xp->input.len -= (p - xp->input.c);
   xp->input.c = p;
   if (*skiptowhite(xp->input.c) != ZERO)
      xp->context = EXPAND_NOTHING;
   ei (STRNICMP(arg, "case", p - arg) == 0)
      expand_what = EXP_CASE;
   ei (STRNICMP(arg, "spell", p - arg) == 0)
      expand_what = EXP_SPELL;
   ei (STRNICMP(arg, "sync", p - arg) == 0)
      expand_what = EXP_SYNC;
   ei (STRNICMP(arg, "list", p - arg) == 0) {
      p = skipwhite(p);
      if (*p == '@')
         expand_what = EXP_CLUSTER;
      else
         xp->context = EXPAND_HILITE_GROUP;
   } ei (   STRNICMP(arg, "keyword", p - arg) == 0
          || STRNICMP(arg, "region", p - arg) == 0
          || STRNICMP(arg, "match", p - arg) == 0
   )
      xp->context = EXPAND_HILITE_GROUP;
   else
      xp->context = EXPAND_NOTHING;
}

// Function given to expandGeneric() to obtain the list syntax names for expansion.
CS
get_syntax_name(Expand* xp, int idx) {
   switch (expand_what) {
   case EXP_SUBCMD:
      if (idx < 0 || idx >= (int)ARRAY_LENGTH(subcommands))
         return NULL;
      return (CS)subcommands[idx].name;
   case EXP_CASE: {
      static char *case_args[] = {"match", "ignore", NULL};
      return (CS)case_args[idx];
   }
   case EXP_SPELL: {
      static char *spell_args[] = { "toplevel", "notoplevel", "default", NULL};
      return (CS)spell_args[idx];
   }
   case EXP_SYNC: {
      static char *sync_args[] = {
         "ccomment", "clear", "fromstart", "linebreaks=", "linecont", "lines=", "match",
         "maxlines=", "minlines=", "region", NULL
      };
      return (CS)sync_args[idx];
   }
   case EXP_CLUSTER: {
      if (idx < curPor->ownSyntax->syntaxClusters.len) {
         eeSnprintf(xp->matchBuilder, EXPAND_BUF_LEN, "@%s", 
               SYN_CLSTR(curPor->ownSyntax)[idx].name
         );
         return xp->matchBuilder;
      } else
         return NULL;
   }
   }
   return NULL;
}


// Function called for expression evaluation: get syntax ID at file position.
int
syn_get_id(
   Portal   *wp,
   long   lnum,
   ColNr   col,
   int      trans,      // remove transparency
   int      keep_state  // keep state of char at "col"
){
   // When the position is not after the current position and in the same
   // line of the same window with the same buffer, need to restart parsing.
   if (wp != syntPortS || wp->book != synBookS || lnum != currLnumS || col < currColS)
      syntaxStartLine(wp, lnum);
   ei (wp->book == synBookS && lnum == currLnumS && col > currColS)
      // next_match may not be correct when moving around, e.g. with the
      // "skip" expression in searchpair()
      nextMatchIdx = -1;

   (void)syntGetDeco(col, keep_state);

   return (trans ? current_trans_id : current_id);
}

#if defined(PROTO)
// Get extra information about the syntax item.  Must be called right after syntGetDeco().
// Stores the current item sequence nr in "*seqnrp". Returns the current flags.
int
get_syntax_info(int *seqnrp) {
   *seqnrp = current_seqnr;
   return current_flags;
}

#endif

// Return the syntax ID at position "i" in the current stack. The caller must have called 
// syn_get_id() before to fill the stack. Returns -1 when "i" is out of range.
int
syn_get_stack_item(int i) {
   if (i >= current_state.len) {
      // Need to invalidate the state, because we didn't properly finish it
      // for the last character, "keep_state" was TRUE.
      invalidate_current_state();
      currColS = MAXCOL;
      return -1;
   }
   return CUR_STATE(i).hiId;
}

private int
syn_cur_foldlevel(void) {
   int      level = 0;
   for (int i = 0; i < current_state.len; ++i) {
      if (CUR_STATE(i).si_flags & HL_FOLD)
         ++level;
   } 
   return level;
}

// Function called to get folding level for line "lnum" in portal "po".
int
syn_get_foldlevel(Portal *po, long lnum) {
   int level = 0;
   int low_level;
   int cur_level;

   // Return quickly when there are no fold items at all.
   if (po->ownSyntax->b_syn_folditems != 0
       && !po->ownSyntax->b_syn_error
# ifdef SYN_TIME_LIMIT
       && !po->ownSyntax->redrawTime
# endif
   ){
      syntaxStartLine(po, lnum);

      // Start with the fold level at the start of the line.
      level = syn_cur_foldlevel();

      if (po->ownSyntax->foldLevel == SYNFLD_MINIMUM) {
         // Find the lowest fold level that is followed by a higher one.
         cur_level = level;
         low_level = cur_level;
         while (!currentFinishedS) {
            (void)getCurrentDeco(false, false, false);
            cur_level = syn_cur_foldlevel();
            if (cur_level < low_level)
               low_level = cur_level;
            ei (cur_level > low_level)
               level = low_level;
            ++currColS;
         }
      }
   }
   if (level > FOLD_NEST_MAX) {
      level = FOLD_NEST_MAX;
      if (level < 0)
         level = 0;
   }
   return level;
}

// "synIDattr(id, what [, mode])" function
void
f_synIDattr(Arr(Var) argvars, Var* returnVar) {
   int id = (int)tv_get_number(&argvars[0]);
   if (id >= SHORT || id < 0)
      return;
   Short hiId = (id < SHORT && id >= 0) ? (Short)id : SHORT;
   HiliteGroup* g = hilites + hiId;
   
   CS what = tv_get_string(&argvars[1]);
   CS p = NULL;
   Byte buf[4];
   
   switch (what[0]) {
   case 'b':
      if (what[1] == 'g')   // bg
         p = printColor(OUT buf,  g->bg);
      else               // bold
         p = hiliteHasFlag(g, DECO_BOLD);
      break;

   case 'f':               // fg
      if (what[1] == 'g')
         p = printColor(OUT buf, g->fg);
      break;

   case 'i':
      if (TOLOWER_ASC(what[1]) == 'n')
         p = (g->flags & DECO_INVERSE) != 0 ? S"inverse" : null;
      else           
         p = (g->flags & DECO_ITALIC) != 0 ? S"italic" : null;
      break;

   case 'n':
      if (TOLOWER_ASC(what[1]) == 'o')
         p = (g->flags & DECO_NOCOMBINE) != 0 ? S"nocombine" : null;
      else           
         p = g->name.c;
      break;
   case 'u':
      if (STRLEN(what) >= 9) {
         if (TOLOWER_ASC(what[5]) == 'l') // underline
            p = (g->flags & DECO_UNDERLINE) != 0 ? S"underline" : null;
         ei (TOLOWER_ASC(what[5]) != 'd') // undercurl
            p = (g->flags & DECO_UNDERCURL) != 0 ? S"undercurl" : null;
      } ei (what[1] == 'n') // under
         p = printColor(OUT buf, g->under);
      break;
   }

   returnVar->tag = VAR_STRING;
   returnVar->string = p ? copyStr(p) : null;
}

// "synIDtrans(id)" function
void
f_synIDtrans(Arr(Var) argvars UNUSED, Var* returnVar) {
   int id = (int)tv_get_number(&argvars[0]);

   if (id > 0)
      id = hiResolveLinks(id);
   else
      id = 0;

   returnVar->number = id;
}

//}}}
