
#ifdef OPTIONS_FIELDS
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) oType oFieldName;
#endif

#ifdef OPTIONS_NAMES
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) (CS)oName,
#endif

#ifdef GLOBAL_OPTION_DEFS
#define TYPEBASED_Boole Boole
#define TYPEBASED_long Num
#define TYPEBASED_CS Str
#define TYPEBASED_Byte Enum
#define TYPEBASED_Unt Flag
#define TYPEBASED_CallbackPtr Callback
#define OPTION(oName, oVar, oType, oDefaultValue, flagsArg, setterArg, expanderArg) \
    {.fullName = (CS)oName, .defaultValue = GLUE(opt, TYPEBASED_##oType)(oDefaultValue),\
       .c = {.reference = GLUE(ref, TYPEBASED_##oType)(&oVar)},\
       .setter = setterArg, .expander = expanderArg,\
       .flags = flagsArg | P_GLOBAL, .scriptPos = {0, 0, 0} },
#endif

#ifdef LOCAL_OPTION_DEFS
#define TYPEBASED_Boole Boole
#define TYPEBASED_long Num
#define TYPEBASED_CS Str
#define TYPEBASED_Byte Enum
#define TYPEBASED_Unt Flag
#define TYPEBASED_CallbackPtr Callback
#define OPTION(oName, oVar, oType, oDefaultValue, flagsArg, setterArg, expanderArg) \
    {.fullName = (CS)oName, .defaultValue = GLUE(opt, TYPEBASED_##oType)(oDefaultValue),\
       .setter = setterArg, .expander = expanderArg, \
       .flags = flagsArg, .scriptPos = {0, 0, 0} },
#endif


#ifdef OPTIONS_ENUM
#ifdef OPTIONS_DEF_BOOK
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) \
    BOOK_##oFieldName,
#else
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) \
    PORT_##oFieldName,
#endif
#endif

#ifdef OPTIONS_INIT_PORTAL

#define TYPEBASED_Boole boole
#define TYPEBASED_long num
#define TYPEBASED_CS string
#define TYPEBASED_Byte enume
#define TYPEBASED_Unt flags
#define TYPEBASED_CallbackPtr callback
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, setter, expander) \
    o->oFieldName = OPTIONS_PORTAL[PORT_##oFieldName].defaultValue.TYPEBASED_##oType;
#endif

#ifdef OPTIONS_COPY
#define TYPEBASED_CS(x) t->x = copyOptionVal(&t->stringOptions, s->x);
#define TYPEBASED_Boole(x) t->x = s->x;
#define TYPEBASED_long(x) t->x = s->x;
#define TYPEBASED_Byte(x) t->x = s->x;
#define TYPEBASED_Unt(x) t->x = s->x;
#define TYPEBASED_CallbackPtr(x) evCopyCallback(t->x, s->x);
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) \
   TYPEBASED_##oType(oFieldName)
#endif

#ifdef COPY_GLOBAL_TO_BOOK

#define TYPEBASED_CS(f, localInd) \
   f = copyOptionVal(&t->stringOptions, OPTIONS_BOOK[localInd].c.local.val.string);
#define TYPEBASED_Boole(f, localInd) f = OPTIONS_BOOK[localInd].c.local.val.boole;
#define TYPEBASED_long(f, localInd) f = OPTIONS_BOOK[localInd].c.local.val.num;
#define TYPEBASED_Byte(f, localInd) f = OPTIONS_BOOK[localInd].c.local.val.enume;
#define TYPEBASED_Unt(f, localInd) f = OPTIONS_BOOK[localInd].c.local.val.flags;
#define TYPEBASED_CallbackPtr(f, localInd) \
   evCopyCallback(f, OPTIONS_BOOK[localInd].c.local.val.callback);
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) \
   TYPEBASED_##oType(t->oFieldName, BOOK_##oFieldName)
   
#endif

#ifdef COPY_STRINGS_TO_BOOK

#define TYPEBASED_CS(old) copyStringOptToBook(wr, old, cha);
#define TYPEBASED_Boole(x)
#define TYPEBASED_long(x)
#define TYPEBASED_Byte(x)
#define TYPEBASED_Unt(x)
#define TYPEBASED_CallbackPtr(x)
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) \
   TYPEBASED_##oType(&curBook->o.oFieldName)
   
#endif

#ifdef COPY_STRINGS_TO_PORTAL

#define TYPEBASED_CS(old)  copyStringOptToBook(wr, old, cha);
#define TYPEBASED_Boole(x)
#define TYPEBASED_long(x)
#define TYPEBASED_Byte(x)
#define TYPEBASED_Unt(x)
#define TYPEBASED_CallbackPtr(x)
#define OPTION(oName, oFieldName, oType, oDefaultValue, flags, postCb, completeCb) \
   TYPEBASED_##oType(&curPor->o.oFieldName)
   
#endif


#ifdef OPTIONS_DEF_GLOBAL 

OPTION("autocomplete", p_ac, Boole, false, 0, null, null)
OPTION("autocompletedelay", p_acl, long, 0, 0, null, null)
OPTION("autoshelldir",  p_asd, Boole, false, 0, NULL, NULL)
OPTION("autowrite", p_aw, Boole, false, 0, null, null)
OPTION("autowriteall", p_awa, Boole, false, 0, null, null)
OPTION("backup", p_bk, Boole, false,  0, null, null)
OPTION("backupdir", p_bdir, CS, DFLT_BDIR, P_EXPAND_DIR|P_ONECOMMA|P_NODUP, null, null)
OPTION("backupext", p_bex, CS, ".bak", P_NFNAME, null, null)
OPTION("backupskip",  p_bsk, CS,  "/tmp/*,$TMPDIR/*,$TMP/*,$TEMP/*", P_ONECOMMA|P_NODUP, 
   null, null)
OPTION("balloondelay", p_bdlay, long, 600, 0, null, null)
OPTION("balloonevalterm", p_bevalterm, Boole, true, P_NO_MKRC, &did_set_balloonevalterm, null) 
OPTION("cdpath", p_cdpath, CS, ",,", P_EXPAND_DIR|P_EXPAND_3_BS|P_COMMA|P_NODUP, 
      null, null)
OPTION("commheight", commlineHeightG, long, 1, P_RALL, &setCommHeight, null)
OPTION("commportheight", p_cwh, long, 7, 0, &setStrictlyPositive, null)
OPTION("columns", visibleColsG, long, 100, P_NODEFAULT|P_NO_MKRC|P_RCLR, &setVisibleCols, null)
OPTION("completefuzzycollect", p_cfc, CS, null, P_ONECOMMA|P_NODUP, 
   &setCompletefuzzycollect, &expandCompletefuzzycollect)
OPTION("completeitemalign", p_cia, Unt, CPT_ABBR|CPT_KIND|CPT_MENU, P_ONECOMMA|P_NODUP,
   &did_set_completeitemalign, NULL)
OPTION("completepopup", p_cpp, CS, null, P_COMMA|P_NODUP|P_COLON, 
   &did_set_completepopup, &expand_set_popupoption)
OPTION("confirm", p_confirm, Boole, false, 0, null, null)
OPTION("cscopepathcomp", p_cspc, long, 0, 0, null, null)
OPTION("cscopeprog", p_csprg, CS, "cscope", P_EXPAND, null, null)
OPTION("cscoperelative", p_csre, Boole, false, 0, null, null)
OPTION("cscopetag", p_cst, Boole, false, 0, null, null)
OPTION("cscopetagorder", p_csto, long, 0, 0, null, null)
OPTION("cscopeverbose", p_csverbose, Boole, false, 0, null, null)
OPTION("cursorInsert", cursorInsertG, Byte, CURSOR_BEAM, P_ONECOMMA|P_NODUP, &setCursorInsert, NULL)
OPTION("cursorNormal", cursorNormalG, Byte, CURSOR_BLOCK, P_ONECOMMA|P_NODUP, 
   &setCursorNormal, null)
OPTION("debug", p_debug, CS, null, 0, &did_set_debug, &expand_set_debug)
OPTION("delcombine", p_delcomb, Boole, false, 0, null, null)
OPTION("diffexpr", p_dex, CS, null, P_CURSWANT, &setOptexpr, null)
OPTION("diffopt",  p_dip, CS, "internal,filler,closeoff,inline:simple", 
   P_REDRAW_PORT|P_ONECOMMA|P_COLON|P_NODUP, &setDiffopt, &expandDiffopt)
OPTION("eadirection", p_ead, Byte, EAD_BOTH, 0, &setEadirection, &expandEadirection)
OPTION("eeglinfo", p_eeglinfo, CS, "'100,<50,s10,h", P_ONECOMMA|P_NODUP, 
      &setEeglinfo, NULL)
OPTION("eeglinfofile", p_eeglinfofile, CS, null, P_EXPAND|P_ONECOMMA|P_NODUP, 
      null, null)
OPTION("equalalways", p_ea, Boole, false, P_RALL, &did_set_equalalways, NULL)
OPTION("errorfile", p_ef, CS, DFLT_ERRORFILE, P_EXPAND, null, null)
OPTION("eventignore", p_ei, CS, null, P_ONECOMMA|P_NODUP, 
      &did_set_eventignore, &expand_set_eventignore)
OPTION("foldlevelstart", foldLevelStart, long, 0, P_CURSWANT, null, null)
OPTION("foldopen", p_fdo, Unt, 
      FDO_BLOCK|FDO_HOR|FDO_MARK|FDO_PERCENT|FDO_LOCATION|FDO_SEARCH|FDO_TAG|FDO_UNDO, 
      P_ONECOMMA|P_NODUP|P_CURSWANT, &setFoldopen, &expandFoldopen)
OPTION("fsync", p_fs, Boole, false, 0, null, null)
OPTION("helpheight", p_hh, long, 20, 0, &setHelpHeight, NULL)
OPTION("helplang", p_hlg, CS, null, P_ONECOMMA, &did_set_helplang, NULL)
OPTION("history", p_hi, long, 200, 0, &setHistory, null)
OPTION("hlsearch", p_hls, Boole, true, P_RALL|P_HLONLY, &did_set_hlsearch, NULL)
OPTION("ignorecase", p_ic, Boole, false, 0, &did_set_ignorecase, null)
OPTION("incsearch", p_is, Boole, false, 0, null, null)
OPTION("isfname", p_isf, CS, "@,48-57,/,.,-,_,+,,,#,$,%,~,=", P_COMMA|P_NODUP, 
   &setIsopt, NULL)
OPTION("isident", p_isi, CS, "@,48-57,_,192-255", P_COMMA|P_NODUP, &setIsopt, NULL)
OPTION("langmap", p_langmap, CS, null, P_ONECOMMA|P_NODUP, &setLangmap, NULL)
OPTION("langremap", p_lrm, Boole, false, 0, null, NULL)
OPTION("lazyredraw", p_lz, Boole, false, 0, null, null)
OPTION("lines", visibleRowsG, long, 24, P_NODEFAULT|P_NO_MKRC|P_RCLR, &setVisibleLines, NULL)
OPTION("liteTheme", liteThemeG, Boole, false, P_RCLR|P_HLONLY, &setLiteTheme, null)
OPTION("makeef", p_mef, CS, "make.err", P_EXPAND, NULL, NULL)
// open the location list when "make" is done
OPTION("makeOpenWhenDone", makeOpenWhenDoneG, Boole, true, 0, null, null)
OPTION("maxfuncdepth", p_mfd, long, 128, 0, null, null)
OPTION("maxmem", p_mm, long, DFLT_MAXMEM, 0, null, null)
OPTION("maxmempattern", p_mmp, long, 1000, 0, null, null)
OPTION("maxsearchcount", p_msc, long, 1000, 0, &did_set_maxsearchcount, NULL)
OPTION("messagesopt", p_mopt, CS, "history", P_ONECOMMA|P_COLON|P_NODUP, 
   &did_set_messagesopt, &expand_set_messagesopt)
OPTION("more", p_more, Boole, true, 0, NULL, NULL)
OPTION("mousetime", p_mouset, long, 100, 0, null, null)
OPTION("operatorfunc", p_opfunc, CS, null, P_FUNC, &did_set_operatorfunc, null)
OPTION("patchexpr", p_pex, CS, null, 0, &setOptexpr, null)
OPTION("previewheight", p_pvh, long, 12, 0, null, null)
OPTION("pumheight", p_ph, long, 0, 0, null, null)
OPTION("pummaxwidth", p_pmw, long, 0, 0, null, null) 
OPTION("pumwidth", p_pw, long, 15, 0, null, null)
OPTION("quickfixtextfunc", p_qftf, CS, null, P_FUNC,
      &setQuickfixtextfunc, NULL)
OPTION("redrawtime", p_rdt, long, 2000, 0, null, null) 
OPTION("rulerformat", p_ruf, CS, null, P_RSTAT, &setRulerFormat, NULL)
OPTION("scrolljump", p_sj, long, 1, 0, &setScrollJump, null) 
OPTION("scrollopt", p_sbo, Unt, SCR_VER|SCR_JUMP, P_ONECOMMA|P_NODUP, 
   &setScrollopt, &expand_set_scrollopt)
OPTION("shellcmdflag", p_shcf, CS, "-c", 0, null, null)
OPTION("shellpipe", p_sp, CS, " 2>&1 | tee", 0, null, null) 
OPTION("shellredir", p_srr, CS, ">%s 2>&1", 0, null, null)
OPTION("shelltemp", p_stmp, Boole, true, 0, null, null)
OPTION("shortmess", p_shm, CS, "filnxtToO", P_FLAGLIST, 
      &did_set_shortmess, &expand_set_shortmess)
OPTION("showcmdloc", p_sloc, Byte, SHOW_COMM_LAST, P_RSTAT, &did_set_showcmdloc, &expand_set_showcmdloc)
OPTION("showfulltag", p_sft, Boole, false, 0, null, null)
OPTION("showmode", p_smd, Boole, true, 0, null, null)
OPTION("showtabpanel", p_stpl, Boole, true, P_RALL, &setShowTabpanel, NULL)
OPTION("sidescroll", p_ss, long, 0, 0, &setNonNegative, null)
OPTION("smartcase", p_scs, Boole, false, 0, null, null)
OPTION("splitbelow", p_sb, Boole, false, 0, null, null)
OPTION("splitright", p_spr, Boole, false, 0, null, null) 
OPTION("startofline", p_sol, Boole, false, 0, null, null) 
OPTION("suffixes", p_su, CS, ".bak,~,.o,.h,.info,.swp,.obj", P_ONECOMMA|P_NODUP, null, null) 
OPTION("swapsync", p_sws, Boole, false, 0, null, null)
OPTION("switchbook", p_swb, Unt, 0, P_ONECOMMA|P_NODUP, &setSwitchbook, &expand_set_switchbook)
OPTION("tabclose", p_tcl, Byte, 0, P_ONECOMMA|P_NODUP, &setTabClose, &expand_set_tabclose)
OPTION("tabpanel",  p_tpl, CS, null, P_RALL, null, null) 
OPTION("tabpanelopt",p_tplo, CS, null, P_ONECOMMA|P_COLON|P_NODUP, 
   &did_set_tabpanelopt, &expand_set_tabpanelopt)
OPTION("tagbsearch", p_tbs, Boole, true, 0, null, null) 
OPTION("tagstack", p_tgst, Boole, false, 0, null, null)
OPTION("term", termCodeS[KS_NAME], CS, null, P_EXPAND|P_NODEFAULT|P_NO_MKRC|P_RALL, 
   &setTerm, null)
OPTION("timeout", p_timeout, Boole, false, 0, null, null)
OPTION("timeoutlen", p_tm, long, 0,  0, &setTimeoutLen, null)
OPTION("ttimeout", p_ttimeout, Boole, false, 0, null, null)
OPTION("ttimeoutlen", p_ttm, long, -1, 0, null, null)
OPTION("ttyscroll", p_ttyscroll, long, 0, 0, null, null)
OPTION("undodir", p_udir, CS, null, P_EXPAND|P_ONECOMMA|P_NODUP, null, null)
OPTION("undoreload", p_ur, long, 10000, 0, null, null)
OPTION("updatetime", p_ut, long, 2000, 0, &setNonNegative, null)
OPTION("verbose", p_verbose, long, 0, 0, null, null)
OPTION("verbosefile", p_vfile, CS, null, P_EXPAND, &did_set_verbosefile, NULL)
OPTION("whichwrap", p_ww, CS, "b,s", P_ONECOMMA|P_FLAGLIST, 
   &did_set_whichwrap, &expand_set_whichwrap)
OPTION("wildchar", p_wc, long, (long)TAB, 0, &did_set_wildchar, NULL)
OPTION("wildcharm", p_wcm, long, 0, 0, &did_set_wildchar, NULL)
OPTION("wildignore", p_wig, CS, null, P_ONECOMMA|P_NODUP, null, null)
OPTION("wildignorecase", p_wic, Boole, false, 0, null, null)
OPTION("wildmenu", p_wmnu, Boole, false, 0, null, null)
OPTION("wildmode", p_wim, CS, "full", P_ONECOMMA|P_NODUP|P_COLON, 
   &did_set_wildmode, &expand_set_wildmode)
OPTION("wildoptions", p_wop, Unt, WILDOPT_PUM, P_ONECOMMA|P_NODUP, 
   &setWildoptions, &expandWildoptions)
OPTION("winheight", p_wh, long, 0, 0, &setWinHeight, NULL)
OPTION("winwidth", p_wiw, long, 20, 0, &did_set_winwidth, NULL)
OPTION("writedelay", p_wd, long, 0, 0, null, null)
OPTION("listchars", p_lcs, CS, "eol:$", P_RALL|P_ONECOMMA|P_NODUP, 
   &setListChars, &expand_set_chars_option)
OPTION("fillchars", p_fcs, CS, "vert:|,fold: ,eob: ,lastline: ", 
   P_RALL|P_ONECOMMA|P_NODUP, &setFillChars, &expand_set_chars_option)
OPTION("showbreak", p_sbr, CS, null, P_RALL, &did_set_showbreak, NULL)
OPTION("undolevels", p_ul, long, 128, 0, &did_set_undolevels, NULL)
OPTION("termwinscroll", p_twsl, long, 10000, P_RBUF, &did_set_termwinscroll, null)

#ifdef FEAT_WAYLAND
OPTION("wlseat", p_wse, CS, null, 0, &setWlseat, NULL)
OPTION("wlsteal", p_wst, Boole, false, 0, &did_set_wlsteal, NULL)
OPTION("wltimeoutlen", p_wtm, long, 500, 0, &did_set_wltimeoutlen, NULL)
#endif

#undef TYPEBASED_CS
#undef TYPEBASED_Boole
#undef TYPEBASED_long
#undef TYPEBASED_Byte
#undef TYPEBASED_Unt
#undef TYPEBASED_CallbackPtr
#undef OPTION
   
#endif 

#ifdef OPTIONS_DEF_PORTAL

OPTION("breakindent", breakIndent, Boole, false, 0, null, null)
OPTION("breakindentopt", breakIndentOpt, CS, null, 0, setBreakindentOpt, expandBreakindentOpt)
OPTION("portcolor", hiliteGroupName, CS, null, P_REDRAW_PORT, 
   &didSetPortcolor, &expandSetPortcolor)
OPTION("diff", diff, Boole, false, P_REDRAW_PORT, &did_set_diff, null)
OPTION("eventignoreport", eventIgnorePort, CS, null, P_ONECOMMA|P_NODUP, 
   &did_set_eventignore, &expand_set_eventignore)
OPTION("foldenable", foldEnable, Boole, false, P_REDRAW_PORT, null, null)
OPTION("foldignore", foldIgnore, CS, "#", P_REDRAW_PORT, &did_set_foldignore, NULL)
OPTION("foldlevel", foldLevel, long, 0, P_REDRAW_PORT, &did_set_foldlevel, NULL)
OPTION("foldmethod", foldMethod, Byte, FOLD_MARKER, P_REDRAW_PORT, 
      &setFoldMethod, &expand_set_foldmethod)
OPTION("foldexpr", foldExpr, CS, null, P_REDRAW_PORT, &did_set_foldexpr, NULL)
OPTION("foldtext", foldText, CS, "foldtext()", P_REDRAW_PORT, 
   &setOptexpr, null)
OPTION("foldmarker", foldMarker, CS, "{{{,}}}",  P_REDRAW_PORT|P_ONECOMMA|P_NODUP,
    &did_set_foldmarker, NULL)
OPTION("linebreak", lineBreak, Boole, false, P_REDRAW_PORT, null, null)
OPTION("list", list, Boole, false, P_REDRAW_PORT, null, null)
OPTION("relativenumber", relativeNumber, Boole, true, P_REDRAW_PORT, null, null)
OPTION("numberwidth", numberWidth, long, 4, P_REDRAW_PORT, &did_set_numberwidth, NULL)
OPTION("portfixbuf", portFixBuf, Boole, false, 0, null, null)
OPTION("portfixheight", portFixHeight, Boole, false, P_RSTAT, null, null)
OPTION("portfixwidth", portFixWidth, Boole, false, P_RSTAT, null, null)
OPTION("smoothscroll", smoothScroll, Boole, false, P_REDRAW_PORT, &did_set_smoothscroll, NULL)
OPTION("cursorcolumn", cursorColumn, Boole, false, P_REDRAW_PORT|P_HLONLY, null, null)
OPTION("cursorline", cursorLine, Boole, false, P_REDRAW_PORT|P_HLONLY, null, null)
OPTION("statusline", statusLine, CS, 
      "%n\\:%f%r%m\\ \\|%l\\:%c\\/%L\\L\\|\\ %{strftime('%H:%M')}",
      P_RSTAT, &did_set_statusline, NULL)
OPTION("scrollbind", scrollBind, Boole, false, 0, &did_set_scrollbind, NULL)
OPTION("wrap", wrap, Boole, true, P_REDRAW_PORT, &did_set_wrap, NULL)
OPTION("cursorbind", cursorBind, Boole, false, 0, null, null)
OPTION("signcolumn", signColumn, Boole, true, P_RCLR, null, null)
OPTION("sidescrolloff", sideScrollOff, long, 0, P_RBUF, &setSideScrollOff, null)
OPTION("scrolloff", scrollOff, long, 3, P_RALL, &setScrollOff, null)
OPTION("termwinkey", termWinKey, CS, null, P_REDRAW_PORT, &did_set_termwinkey, null)
OPTION("termwinsize", termWinSize, CS, null, P_REDRAW_PORT, &did_set_termwinsize, null)

#undef TYPEBASED_CS
#undef TYPEBASED_Boole
#undef TYPEBASED_long
#undef TYPEBASED_Byte
#undef TYPEBASED_Unt
#undef TYPEBASED_CallbackPtr
#undef OPTION
   
#endif 

#ifdef OPTIONS_DEF_BOOK 

// used for @cinkeys and @indentkeys
#define INDENTKEYS_DEFAULT "0{,0},0),0],:,0#,!^F,o,O,e"

OPTION("autoindent", autoIndent, Boole, false, 0, null, null)
OPTION("backupcopy", backupCopy, Unt, BKC_AUTO, P_ONECOMMA|P_NODUP, 
    &setBackupCopy, &expand_set_backupcopy)
OPTION("balloonexpr", balloonExpr, CS, null, 0, &setOptexpr, null)
OPTION("binary", binary, Boole, false, P_RSTAT, &optSetBinary, NULL)
OPTION("booklisted", bookListed, Boole, false, 0, &setBookListed, NULL)
OPTION("booktype", kind, Byte, BOOK_NORMAL, 0, &setBufType, &expand_set_buftype)
OPTION("cinwords", indentKeywords, CS, "if,else,while,do,for,switch", 
      P_ONECOMMA|P_NODUP, null, null)
OPTION("comments", comments, CS, "s1:/" "*,mb:*,ex:*" "/,://,b:#,:%,:XCOMM,n:>,fb:-", 
      P_ONECOMMA|P_NODUP, &did_set_comments, NULL)
OPTION("commentstring", commentString, CS, "//%s", 0, &did_set_commentstring, null)
OPTION("complete", complete, CS, ".,w,b,u,t,i", P_ONECOMMA|P_NODUP, 
      &setComplete, &expandComplete)
OPTION("completeopt", completeOpt, Unt, COT_MENU|COT_PREVIEW, P_ONECOMMA|P_NODUP, 
      &setCompleteopt, &expandCompleteopt)
OPTION("completefunc", completeFn, CallbackPtr, null, P_FUNC, &setCompletefunc, NULL)
OPTION("omnifunc", omniFn, CallbackPtr, null, P_FUNC, &setOmnifunc, NULL)
OPTION("tagfunc", tagFn, CallbackPtr, null, P_FUNC, &did_set_tagfunc, NULL)
OPTION("findfunc", findFn, CallbackPtr, null, P_FUNC, &setFindFn, NULL)
OPTION("expandtab", expandTab, Boole, true, 0, null, null)
OPTION("formatoptions", formatOptions, CS, "tcq", P_FLAGLIST, 
      &did_set_formatoptions, &expand_set_formatoptions)
OPTION("formatlistpat", formatListPattern, CS, "^\\s*\\d\\+[\\]:.)}\\t ]\\s*", 
      0, &setFormatListPat, null)
OPTION("infercase", inferCase, Boole, false, 0, null, null)
OPTION("isexpand", expandTriggers, CS, ".|->|/*", P_ONECOMMA|P_NODUP, &setExpandTriggers, NULL)
OPTION("iskeyword", isKeyword, CS, "@,48-57,_,192-255", P_COMMA|P_NODUP, 
      &did_set_iskeyword, NULL)
OPTION("define", definer, CS, "^\\s*#\\s*define", 0, null, null)
OPTION("include", includer, CS, "^\\s*#\\s*include", 0, null, null)
OPTION("includeexpr", includeExpr, CS, null,  0, &setOptexpr, NULL)
OPTION("indentexpr", indentExpr, CS, null, 0, &setOptexpr, null)
OPTION("formatprog", formatProg, CS, null, P_EXPAND, null, null)
OPTION("formatexpr", formatExpr, CS, null, 0, &setOptexpr, null)
// Can use ":help" for @keywordprog
OPTION("keywordprog", keywordProg, CS, "man -S", P_EXPAND, null, null)
OPTION("matchpairs", matchPairs, CS, "(:),{S:},[:]", P_ONECOMMA|P_NODUP, 
      &did_set_matchpairs, null)
OPTION("modifiable",  modifiable, Boole, true, 0, &setModifiable, NULL)
OPTION("shiftwidth", shiftWidth, long, 4, 0, &setShiftWidth, null)
OPTION("swapfile", swapFile, Boole, true, P_RSTAT, &did_set_swapfile, null)
OPTION("smartindent", smartIndent, Boole, true, 0, null, null)
OPTION("suffixesadd", suffixesAdd, CS, ".java,.rust", P_ONECOMMA|P_NODUP, 
      NULL, NULL)
OPTION("textwidth", textWidth, long, 0, P_RBUF|P_HLONLY, &did_set_textwidth, NULL)
OPTION("wrapmargin", wrapMargin, long, 0, 0, null, null)
OPTION("grepformat", grepFormat, CS, "%f:%l:%m,%f:%l%m,%f  %l%m", P_ONECOMMA|P_NODUP, 
      null, null)
//Add an extra file name so that grep will always insert a file name in the match line
OPTION("grepprog", grepProg, CS, "grep -n $* /dev/null", P_EXPAND, null, null)
OPTION("path", path, CS, ".,/usr/include,,", P_EXPAND_DIR|P_EXPAND_3_BS|P_COMMA|P_NODUP, 
      null, null)
OPTION("makeprog", makeProg, CS, "make", P_EXPAND, null, null)
OPTION("errorformat", errorFormat, CS, DFLT_EFM, P_ONECOMMA|P_NODUP, null, null)
OPTION("autoread", autoRead, Boole, true, 0, null, null)
OPTION("tags", tags, CS, "./tags,tags", P_EXPAND|P_EXPAND_3_BS|P_ONECOMMA|P_NODUP, null, null)
OPTION("tagcase", tagCase, Byte, TC_FOLLOWIC, 0, &setTagcase, &expand_set_tagcase)
OPTION("dictionary", dictionary, CS, null, P_EXPAND|P_ONECOMMA|P_NODUP|P_NDNAME, 
      NULL, NULL)
OPTION("diffanchors", diffAnchors, CS, null, P_ONECOMMA, &did_set_diffanchors, NULL)
OPTION("thesaurus", thesaurus, CS, null, P_EXPAND|P_ONECOMMA|P_NODUP|P_NDNAME, NULL, NULL)
OPTION("thesaurusfunc", thesaurusFn, CallbackPtr, null, P_FUNC, 
      &did_set_thesaurusfunc, null)
OPTION("undofile", undoFile, Boole, false, 0, &did_set_undofile, null)

#undef TYPEBASED_CS
#undef TYPEBASED_Boole
#undef TYPEBASED_long
#undef TYPEBASED_Byte
#undef TYPEBASED_Unt
#undef TYPEBASED_CallbackPtr
#undef INDENTKEYS_DEFAULT
#undef OPTION
   
#endif 

