
/* Define unless no X support found */
#define HAVE_X11 1

/* Define unless no Wayland support found */
#define HAVE_WAYLAND 1

/* Define when termcap.h contains ospeed */
#define HAVE_OSPEED 1

/* Define when termcap.h contains UP, BC and PC */
#define HAVE_UP_BC_PC 1

/* Define when del_curterm() is available */
#define HAVE_DEL_CURTERM 1

/* Define when __DATE__ " " __TIME__ can be used */
#define HAVE_DATE_TIME 1

/* Define when __attribute__((unused)) can be used */
#define HAVE_ATTRIBUTE_UNUSED 1

/* defined always when using configure */
#define UNIX 1

/* Defined to the size of an int */
#define VIM_SIZEOF_INT 4

/* Defined to the size of a long */
#define VIM_SIZEOF_LONG 8

/* Defined to the size of off_t */
#define SIZEOF_OFF_T 8

/* Defined to the size of time_t */
#define SIZEOF_TIME_T 8

#define USEMEMMOVE 1

/* Define if you can safely include both <sys/time.h> and <sys/select.h>.  */
#define SYS_SELECT_WITH_SYS_TIME 1

/* Define to a typecast for select() arguments 2, 3 and 4. */
#define SELECT_TYPE_ARG234 (fd_set *)

/* Define if you have Sys4 ptys */
#define HAVE_SVR4_PTYS 1

/* Define if struct sigcontext is present */
#define HAVE_SIGCONTEXT 1

/* Define to nanoseconds field of struct stat */
#define ST_MTIM_NSEC st_mtim.tv_nsec

/* Define if tgetstr() has a second argument that is (char *) */
/* #undef TGETSTR_CHAR_P */

/* Define if tgetent() returns zero for an error */
#define TGETENT_ZERO_ERR 0

/* Define if you the function: */
#define HAVE_FCHOWN 1
#define HAVE_FCHMOD 1
#define HAVE_FSEEKO 1
#define HAVE_FSYNC 1
#define HAVE_FTRUNCATE 1
#define HAVE_GETCWD 1
#define HAVE_GETPGID 1
/* #undef HAVE_GETPSEUDOTTY */
#define HAVE_GETPWENT 1
#define HAVE_GETPWNAM 1
#define HAVE_GETPWUID 1
#define HAVE_GETRLIMIT 1
#define HAVE_GETTIMEOFDAY 1
/* #undef HAVE_GETWD */
#define HAVE_ICONV 1
#define HAVE_INET_NTOP 1
#define HAVE_LOCALTIME_R 1
#define HAVE_MEMSET 1
#define HAVE_MKDTEMP 1
#define HAVE_NANOSLEEP 1
#define HAVE_NL_LANGINFO_CODESET 1
#define HAVE_OPENDIR 1
#define HAVE_POSIX_OPENPT 1
#define HAVE_PUTENV 1
#define HAVE_QSORT 1
#define HAVE_RENAME 1
#define HAVE_SELECT 1
/* #undef HAVE_SELINUX */
#define HAVE_SETENV 1
#define HAVE_SETPGID 1
#define HAVE_SETSID 1
#define HAVE_SIGACTION 1
#define HAVE_SIGALTSTACK 1
#define HAVE_SIGSET 1
/* #undef HAVE_SIGSETJMP */
#define HAVE_SIGSTACK 1
#define HAVE_SIGPROCMASK 1
/* #undef HAVE_SIGVEC */
/* #undef HAVE_SMACK */
#define HAVE_STRCASECMP 1
#define HAVE_STRCOLL 1
#define HAVE_STRERROR 1
#define HAVE_STRFTIME 1
/* #undef HAVE_STRICMP */
#define HAVE_STRNCASECMP 1
/* #undef HAVE_STRNICMP */
#define HAVE_STRPBRK 1
#define HAVE_STRPTIME 1
#define HAVE_STRTOL 1
/* #undef HAVE_CANBERRA */
#define HAVE_SODIUM 1
#define HAVE_ST_BLKSIZE 1
#define HAVE_SYNC 1
#define HAVE_SYSCONF 1
/* #undef HAVE_SYSCTL */
#define HAVE_SYSINFO 1
#define HAVE_SYSINFO_MEM_UNIT 1
#define HAVE_SYSINFO_UPTIME 1
#define HAVE_TOWLOWER 1
#define HAVE_TOWUPPER 1
#define HAVE_ISWUPPER 1
#define HAVE_TZSET 1
#define HAVE_UNSETENV 1
#define HAVE_USLEEP 1
#define HAVE_UTIME 1
#define HAVE_MBLEN 1
#define HAVE_TIMER_CREATE 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_XATTR 1


/* Define if you do not have utime(), but do have the utimes() function. */
#define HAVE_UTIMES 1

/* Define if you have the header file: */
#define HAVE_DIRENT_H 1
/* #undef HAVE_DISPATCH_DISPATCH_H */
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
/* #undef HAVE_FRAME_H */
#define HAVE_ICONV_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LANGINFO_H 1
/* #undef HAVE_LIBC_H */
#define HAVE_LIBGEN_H 1
#define HAVE_LIBINTL_H 1
#define HAVE_LOCALE_H 1
/* #undef HAVE_NDIR_H */
#define HAVE_POLL_H 1
/* #undef HAVE_PTHREAD_NP_H */
#define HAVE_PWD_H 1
#define HAVE_SETJMP_H 1
#define HAVE_SGTTY_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRINGS_H 1
/* #undef HAVE_STROPTS_H */
/* #undef HAVE_SYS_ACCESS_H */
/* #undef HAVE_SYS_DIR_H */
#define HAVE_SYS_IOCTL_H 1
/* #undef HAVE_SYS_NDIR_H */
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_POLL_H 1
/* #undef HAVE_SYS_PTEM_H */
/* #undef HAVE_SYS_PTMS_H */
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_STATFS_H 1
/* #undef HAVE_SYS_STREAM_H */
/* #undef HAVE_SYS_SYSCTL_H */
#define HAVE_SYS_SYSINFO_H 1
/* #undef HAVE_SYS_SYSTEMINFO_H */
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_TERMCAP_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_TERMIO_H 1
#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1
#define HAVE_UNISTD_H 1
/* #undef HAVE_UTIL_DEBUG_H */
/* #undef HAVE_UTIL_MSGI18N_H */
#define HAVE_UTIME_H 1
#define HAVE_X11_SM_SMLIB_H 1

/* Define if you have <sys/wait.h> that is POSIX.1 compatible.  */
#define HAVE_SYS_WAIT_H 1

/* instead, we check a few STDC things ourselves */
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1

#define FEAT_HUGE 1

/* Define if you want to add support of GPM (Linux console mouse daemon) */
#define HAVE_GPM 1

/* Define if you want to include the Cscope interface. */
#define ENABLE_CSCOPE 1

/* Define if you don't want to include right-left support. */
#define DISABLE_RIGHTLEFT 1
/* Define if we have dlfcn.h. */
#define HAVE_DLFCN_H 1

/* Define if there is a working gettext(). */
#define HAVE_GETTEXT 1

/* Define if there is a working bind_textdomain_codeset(). */
#define HAVE_BIND_TEXTDOMAIN_CODESET 1

/* Define if there is a working dgettext(). */
#define HAVE_DGETTEXT 1

/* Define if there is a working dngettext(). */
#define HAVE_DNGETTEXT 1

/* Define if _nl_msg_cat_cntr is present. */
#define HAVE_NL_MSG_CAT_CNTR 1

/* Define if we have dlsym() */
#define HAVE_DLSYM 0

/* Define if we can use IPv6 networking. */
#define FEAT_IPV6 1

/* Define if you want XSMP interaction as well as vanilla swapfile safety */
#define USE_XSMP_INTERACT 1

/* Define if fcntl()'s F_SETFD command knows about FD_CLOEXEC */
#define HAVE_FD_CLOEXEC 1

/* Define if /proc/self/exe or similar can be read */
#define PROC_EXE_LINK "/proc/self/exe"

/* Define if Xutf8SetWMProperties() is in an X library. */
#define HAVE_XUTF8SETWMPROPERTIES 1
/* Define if we have isinf() */
#define HAVE_ISINF 1

/* Define if we have isnan() */
#define HAVE_ISNAN 1

/* Define if we have dirfd() */
#define HAVE_DIRFD 1

/* Define if we have flock() */
#define HAVE_FLOCK 1

/* Define if we have shm_open() */
#define HAVE_SHM_OPEN 1

/* Define to inline symbol or empty */
/* #undef inline */

/* Define if _SC_SIGSTKSZ is available via sysconf() */
#define HAVE_SYSCONF_SIGSTKSZ 1

