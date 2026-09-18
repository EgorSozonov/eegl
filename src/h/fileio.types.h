ssize_t listxattr(const char*, char*, size_t);
int setxattr(const char*, const char*, const void*, size_t, int);
#define BAD_REPLACE   '?'   // replace it with '?' (default)
#define BAD_KEEP    1000   // leave it
#define BAD_DROP    1002   // erase it
declStruct(VisitedList);
declStruct (DirSearchStack);
#define FINDFILE_FILE  0  // only files
#define FINDFILE_DIR   1  // only directories
#define FINDFILE_BOTH  2  // files and directories
