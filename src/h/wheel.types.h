#define FIND_IDENT   1 //find identifier (keyword)
#define FIND_STRING  2 //find any non-whitespace text
#define FIND_EVAL    4 //include "->", "[]" and "." (useful for C program debugging)
#define FIND_NOERROR 8 //no error when no word found
#define INSCHAR_FORMAT    1   //force formatting
#define INSCHAR_DO_COM    2   //format comments
#define INSCHAR_CTRLV     4   //char typed just after CTRL-V
#define INSCHAR_NO_FEX    8   //don't use 'formatexpr'
#define INSCHAR_COM_LIST 16   //format comments with list/2nd line indent
