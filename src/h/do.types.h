int stat(const char* restrict path, struct stat* restrict buf);
#define VGR_GLOBAL  1
#define VGR_NOJUMP  2
#define VGR_FUZZY   4
#define CCGD_AW       1   //do autowrite if book was changed
#define CCGD_MULTWIN  2   //check also when several wins for the buf
#define CCGD_FORCEIT  4   //! used
#define CCGD_ALLBOOKS 8   //may write all books
#define CCGD_EXCMD   16   //may suggest using !
#define OPENLINE_DELSPACES    0x01 //delete spaces after cursor
#define OPENLINE_DO_COM       0x02 //format comments
#define OPENLINE_KEEPTRAIL    0x04 //keep trailing spaces
#define OPENLINE_MARKFIX      0x08 //fix mark positions
#define OPENLINE_COM_LIST     0x10 //format comments with list/2nd line indent
#define OPENLINE_FORMAT       0x20 //formatting long comment
#define OPENLINE_FORCE_INDENT 0x40 //use second_line_indent without indent logic
