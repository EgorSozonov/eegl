void optSetStringDefault(CS name, CS val);
void optFreeAllOptions(void);
void optInit1(void);
CS optSetByName(CS name, OptionValue newVal, SetScope setScope);
Unt stringToChar(CS arg, Boole multi_byte);
ScriptPos* optGetScriptPos(CS name);
OptionValue optGetValue(OUT Unt* flagsp, CS name, int scope);
Boole optIsFnOption(Unt flags);
void optSetFromVar(CS varname, Var *varp);
int optExpandForSet(Expand* xp, RegMatch* regmatch, OUT ExpandMatch* matches);
Boole optWasSet(CS name);
int makefoldset(FILE *fd);
CS optSetBinary(OptionChange* cha);
CS setModifiable(OptionChange* cha);
void optChangeAndReportError(
   CS name,
   OptionValue newValue,
   SetScope setScope
);
void afterCopyPortOpt(Portal* po);
void copyPortOpt(PortalOptions* t, PortalOptions* s);
void optClearPortOptions(PortalOptions* t);
void optInitExpandContextForSet(
   OUT Expand* xp,
   CS arg,
   SetScope setScope
);
int optExpandOldOption(OUT ExpandMatch* matches);
int reset_optWasSet(CS name);
long get_sidescrolloff_value(void);
int optSetCallback(OUT Callback* cb, CS new);
CS setShowTabpanel(OptionChange* cha);
void initPortalOptions(PortalOptions* o);
void updateStringRef(OptionChange* cha);
void optInit0();
int writeOptionsAsSet(FILE *fd UNUSED);
void optSetLocalOptionsToDefault(Portal *wp, Boole doBook);
void c_get(Invocation* invo);
void c_set(Invocation* invo);
Bag* getBookOrPortOptions(Boole bufopt);
void optCopyBetweenPortals(OUT Portal* to, Portal* from);
void optCopyGlobalToPortal(OUT Portal* to);
void optFreeBookCallbacks(Book* book);
void optsCopyToBook(OUT Book* book, Unt flags);
int optExpandOption(
   Expand* xp UNUSED,
   RegMatch* regmatch,
   CS fuzzystr,
   Boole canFuzzy,
   OUT ExpandMatch* matches
);
Boole optImmutableMode();
void optChangeStringOptionDirect(
   CS name,
   CS val,
   SetScope scope,
   ScriptId set_sid
);
void optSetStringOptionDirectInPort(
   OUT Portal* po,
   CS name,
   CS val,
   Unt optFlags,
   int set_sid
);
void optSetStringOptionDirectInBook(
   Book* book,
   CS name,
   CS val,
   Unt optFlags,
   int set_sid
);
CS get_mess_lang(void);
void set_lang_var(void);
void init_locale(void);
void c_language(Invocation* invo);
void free_locales(void);
CS get_lang_arg(Expand* xp UNUSED, int idx);
CS get_locales(Expand* xp UNUSED, int idx);
