//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## channel.c: implements communication through a socket or any file handle, plus logging

#include "eegl.h"

#ifndef PROTO
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/resource.h>
#endif


# define EXEC_FAILED 122 //Exit code when shell didn't execute. Don't use
                         // 127, some shells use that already
# define OPEN_NULL_FAILED 123 // Exit code if /dev/null can't be opened

# define SIGSET_DECL(set)   sigset_t set;
# define BLOCK_SIGNALS(set)   block_signals(set)
# define UNBLOCK_SIGNALS(set)   unblock_signals(set)

private int dontCheckJobEndedS = 0;

typedef int waitstatus;

// volatile because it is used in signal handler deathtrap().
private volatile SigAtomic inMchDelayS = false; // sleeping in mch_delay()


// volatile because it is used in signal handler deathtrap().
private volatile SigAtomic deadlySignalS = 0;      // The signal we caught

#define SOCK_ERRNO
#define sock_write(sd, buf, len) write(sd, buf, len)
#define sock_read(sd, buf, len) read(sd, buf, len)
#define sock_close(sd) close(sd)
#define fd_read(fd, buf, len) read(fd, buf, len)
#define fd_write(sd, buf, len) write(sd, buf, len)
#define fd_close(sd) close(sd)

// Structure to hold info about an async shell Job
struct Job {
   Unt refCount; //reference count
   Job* next;
   Job* prev;
   ProId pid;
   JobStatus status;
   Arr(Byte) ttyIn;    //controlling tty input, allocated
   Arr(Byte) ttyOut;   //controlling tty output, allocated
   Arr(Byte) jv_stoponexit;//allocated
   Arr(Byte) jv_termsig;   //allocated
   int exitVal;
   void (*nativeCb)(void); //native C function to call when the job finishes
   Callback exitCb;

   Book* inBook;   //book from "in-name"

   int copyId;

   Channel* channel; //channel for I/O, reference-counted
   Arr(CS) argv;   //command line used to start the job
};


#define FOR_ALL_CHANNELS(ch) \
    for ((ch) = first_channel; (ch) != NULL; (ch) = (ch)->next)
    
#define FOR_ALL_JOBS(job) \
    for ((job) = firstJobS; (job) != NULL; (job) = (job)->next)

// Whether we are inside channel_parse_messages() or another situation where it
// is safe to invoke callbacks.
private int safe_to_invoke_callback = 0;

// The list of all allocated channels.
private Channel *first_channel = NULL;
private int next_ch_id = 0;
private int ignore_sigtstp = false;

#define LOG_ALWAYS 9// must be different from true and false

//{{{forward declarations

private void channel_read(Channel *channel, ChannelFdKind part, char *func);
private ChannelMode channel_get_mode(Channel *channel, ChannelFdKind part);
private int channel_get_timeout(Channel *channel, ChannelFdKind part);
private ChannelFdKind channel_part_send(Channel *channel);
private ChannelFdKind channel_part_read(Channel *channel);
private void channel_close(Channel *channel, int invoke_close_cb);

private void set_default_child_environment(int is_terminal);
private void init_signal_stack(void);
private void catch_int_signal(void);
private void catch_signals(void (*func_deadly)(int), void (*func_other)(int));
private void open_pty(int *pty_master_fd, int *pty_slave_fd, Byte **name1, Byte **name2);
private void ch_log_literal(CS lead, Channel* ch, ChannelFdKind part, OUT Text buf);

//}}}
//{{{auxiliary

// Allocate a new channel. The refcount is set to 1.
// The channel isn't actually used until it is opened.
Channel*
add_channel(void) {
   ChannelFdKind   part;
   Channel* channel = ALLOC_CLEAR_ONE(Channel);

   channel->id = next_ch_id++;
   ch_log(channel, "Created channel");

   for (part = PART_SOCK; part < PART_COUNT; ++part) {
      channel->fds[part].ch_fd = INVALID_FD;
      channel->fds[part].ch_timeout = 2000;
   }

   if (first_channel != NULL) {
      first_channel->prev = channel;
      channel->next = first_channel;
   }
   first_channel = channel;

   channel->refCount = 1;
   return channel;
}

int
has_any_channel(void){
   return first_channel != NULL;
}

// Called when the refcount of a channel is zero.
// Return true if "channel" has a callback and the associated job wasn't killed.
int
channel_still_useful(Channel *channel) {
   int has_sock_msg;
   int   has_out_msg;
   int   has_err_msg;

   // If the job was killed the channel is not expected to work anymore.
   if (channel->isBeingKilled && channel->job == NULL)
      return false;

   // If there is a close callback it may still need to be invoked.
   if (channel->ch_close_cb.name != NULL)
      return true;

   // If reading from or a book it's still useful.
   if (channel->fds[PART_IN].bookref.c != NULL)
      return true;

   // If there is no callback then nobody can get readahead.  If the fd is
   // closed and there is no readahead then the callback won't be called.
   has_sock_msg = channel->fds[PART_SOCK].ch_fd != INVALID_FD
      || channel->fds[PART_SOCK].head.next != NULL
      || channel->fds[PART_SOCK].ch_json_head.jq_next != NULL;
   has_out_msg = channel->fds[PART_OUT].ch_fd != INVALID_FD
        || channel->fds[PART_OUT].head.next != NULL
        || channel->fds[PART_OUT].ch_json_head.jq_next != NULL;
   has_err_msg = channel->fds[PART_ERR].ch_fd != INVALID_FD
        || channel->fds[PART_ERR].head.next != NULL
        || channel->fds[PART_ERR].ch_json_head.jq_next != NULL;
   return (channel->ch_callback.name && (has_sock_msg || has_out_msg || has_err_msg))
       || ((channel->fds[PART_OUT].ch_callback.name != NULL
             || channel->fds[PART_OUT].bookref.c != NULL)
          && has_out_msg)
       || ((channel->fds[PART_ERR].ch_callback.name != NULL
             || channel->fds[PART_ERR].bookref.c != NULL)
          && has_err_msg);
}

// Return true if "channel" is closeable (i.e. all readable fds are closed).
int
channel_can_close(Channel* channel) {
   return channel->ch_to_be_closed == 0;
}

// Close a channel and free all its resources. The "channel" pointer remains valid.
private void
channel_free_contents(Channel* channel) {
   channel_close(channel, true);
   channel_clear(channel);
   ch_log(channel, "Freeing channel");
}

// Unlink "channel" from the list of channels and free it.
private void
channel_free_channel(Channel* channel) {
   if (channel->next)
      channel->next->prev = channel->prev;
   if (channel->prev == NULL)
      first_channel = channel->next;
   else
      channel->prev->next = channel->next;
   eeglFree(channel);
}

private void
channel_free(Channel* channel) {
   if (in_free_unref_items)
      return;

   if (safe_to_invoke_callback == 0)
      channel->ch_to_be_freed = true;
   else {
      channel_free_contents(channel);
      channel_free_channel(channel);
   }
}

// Close a channel and free all its resources if there is no further action
// possible, there is no callback to be invoked or the associated job was
// killed. Return true if the channel was freed.
private int
channel_may_free(Channel *channel) {
   if (!channel_still_useful(channel)) {
      channel_free(channel);
      return true;
   }
   return false;
}

// Decrement the reference count on "channel" and maybe free it when it goes
// down to zero.  Don't free it if there is a pending action.
// Return true when the channel is no longer referenced.
int
channel_unref(Channel* channel) {
   if (channel && --channel->refCount <= 0)
      return channel_may_free(channel);
   return false;
}

int
free_unused_channels_contents(int copyID, int mask) {
   int      did_free = false;

   // This is invoked from the garbage collector, which only runs at a safe point.
   ++safe_to_invoke_callback;

   Channel* ch;
   FOR_ALL_CHANNELS(ch) {
      if (!channel_still_useful(ch) && (ch->copyId & mask) != (copyID & mask)) {
          // Free the channel and ordinary items it contains, but don't
          // recurse into Lists, Dictionaries etc.
          channel_free_contents(ch);
          did_free = true;
      }
   } 

   --safe_to_invoke_callback;
   return did_free;
}

void
free_unused_channels(int copyID, int mask) {
   Channel* next;
   for (Channel* ch = first_channel; ch != NULL; ch = next) {
      next = ch->next;
      if (!channel_still_useful(ch) && (ch->copyId & mask) != (copyID & mask))
         // Free the channel struct itself.
         channel_free_channel(ch);
   }
}

//"flags": MCH_DELAY_IGNOREINPUT - don't read input
//      MCH_DELAY_SETTMODE - use termSetMode() even for short delays
void
mch_delay(long msec, int flags) {
   TermInputMode old_tmode;
   int call_termSetMode;

   if (flags & MCH_DELAY_IGNOREINPUT) {
      //Go to cooked mode without echo, to allow SIGINT interrupting us
      //here. But we don't want QUIT to kill us (CTRL-\ used in a
      //shell may produce SIGQUIT). Only do this if sleeping for more than half a second.
      inMchDelayS = true;
      call_termSetMode = mch_cur_tmode == TMODE_RAW
                   && (msec > 500 || (flags & MCH_DELAY_SETTMODE));
      if (call_termSetMode) {
          old_tmode = mch_cur_tmode;
          termSetMode(TMODE_SLEEP);
      }

      //Everybody sleeps in a different way...
      //Prefer nanosleep(), some versions of usleep() can only sleep up to one second.
      struct timespec ts;

      ts.tv_sec = msec / 1000;
      ts.tv_nsec = (msec % 1000) * 1000000;
      (void)nanosleep(&ts, NULL);

      if (call_termSetMode)
         termSetMode(old_tmode);
      inMchDelayS = false;
   } else
      waitForChar(msec, NULL, false);
}


// We need to call connect() again after connect() failed.
private int
channel_connect(
   Channel* channel, 
   const struct sockaddr* server_addr, 
   int server_addrlen, 
   int *waittime
) {
   int      sd = -1;

   while (true) {
      long   elapsed_msec = 0;
      int   waitnow;
      int   ret;

      if (sd >= 0)
          sock_close(sd);
      sd = socket(server_addr->sa_family, SOCK_STREAM, 0);
      if (sd == -1) {
          ch_error(channel, "in socket() in channel_connect().");
          PERROR(_(e_socket_in_channel_connect));
          return -1;
      }

      if (*waittime >= 0) {
         // Make connect() non-blocking.
         if (fcntl(sd, F_SETFL, O_NONBLOCK) < 0) {
            SOCK_ERRNO;
            ch_error(channel, "channel_connect: Connect failed with errno %d", errno);
            sock_close(sd);
            return -1;
         }
      }

      // Try connecting to the server.
      ch_log(channel, "Connecting...");

      ret = connect(sd, server_addr, server_addrlen);
      if (ret == 0)
         // The connection could be established.
         break;

      SOCK_ERRNO;
      if (*waittime < 0 || (errno != EWOULDBLOCK
         && errno != ECONNREFUSED
#ifdef EINPROGRESS
         && errno != EINPROGRESS
#endif
         ))
      {
         ch_error(channel, "channel_connect: Connect failed with errno %d", errno);
         PERROR(_(e_cannot_connect_to_port));
         sock_close(sd);
         return -1;
      } ei (errno == ECONNREFUSED) {
         ch_error(channel, "channel_connect: Connection refused");
         sock_close(sd);
         return -1;
      }

      // Limit the waittime to 50 msec.  If it doesn't work within this
      // time we close the socket and try creating it again.
      waitnow = *waittime > 50 ? 50 : *waittime;

      // If connect() didn't finish then try using select() to wait for the connection to be made.
      {
          TimeVal   tv;
          fd_set      rfds;
          fd_set      wfds;
          int         so_error = 0;
          socklen_t      so_error_len = sizeof(so_error);
          TimeVal   start_tv;
          TimeVal   end_tv;
          FD_ZERO(&rfds);
          FD_SET(sd, &rfds);
          FD_ZERO(&wfds);
          FD_SET(sd, &wfds);

          tv.tv_sec = waitnow / 1000;
          tv.tv_usec = (waitnow % 1000) * 1000;
          gettimeofday(&start_tv, NULL);
          ch_log(channel,
               "Waiting for connection (waiting %d msec)...", waitnow);

         ret = select(sd + 1, &rfds, &wfds, NULL, &tv);
         if (ret < 0) {
            SOCK_ERRNO;
            ch_error(channel, "channel_connect: Connect failed with errno %d", errno);
            PERROR(_(e_cannot_connect_to_port));
            sock_close(sd);
            return -1;
         }

         // See socket(7) for the behavior
         // After putting the socket in non-blocking mode, connect() will return EINPROGRESS, 
         // select() will not wait (as if writing is possible), need to use getsockopt() to check 
         // if the socket is actually able to connect. We detect a failure to connect when either 
         // read and write fds are set.  Use getsockopt() to find out what kind of failure.
         if (FD_ISSET(sd, &rfds) || FD_ISSET(sd, &wfds)) {
            ret = getsockopt(sd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len);
            if (ret < 0 || (so_error != 0
               && so_error != EWOULDBLOCK
               && so_error != ECONNREFUSED
# ifdef EINPROGRESS
               && so_error != EINPROGRESS
# endif
               ))
            {
                ch_error(channel, "channel_connect: Connect failed with errno %d", so_error);
                PERROR(_(e_cannot_connect_to_port));
                sock_close(sd);
                return -1;
            } ei (errno == ECONNREFUSED) {
                ch_error(channel, "channel_connect: Connection refused");
                sock_close(sd);
                return -1;
            }
         }

         if (FD_ISSET(sd, &wfds) && so_error == 0)
            // Did not detect an error, connection is established.
            break;

         gettimeofday(&end_tv, NULL);
         elapsed_msec = (end_tv.tv_sec - start_tv.tv_sec) * 1000
                + (end_tv.tv_usec - start_tv.tv_usec) / 1000;
      }

      if (*waittime > 1 && elapsed_msec < *waittime) {
         // The port isn't ready but we also didn't get an error.
         // This happens when the server didn't open the socket
         // yet.  Select() may return early, wait until the remaining
         // "waitnow"  and try again.
         waitnow -= elapsed_msec;
         *waittime -= elapsed_msec;
         if (waitnow > 0) {
            mch_delay((long)waitnow, MCH_DELAY_IGNOREINPUT);
            ui_breakcheck();
            *waittime -= waitnow;
         }
         if (!gotInterruptG) {
            if (*waittime <= 0)
               // give it one more try
               *waittime = 1;
            continue;
         }
         // we were interrupted, behave as if timed out
      }

      // We timed out.
      ch_error(channel, "Connection timed out");
      sock_close(sd);
      return -1;
   }

   if (*waittime >= 0) {
      (void)fcntl(sd, F_SETFL, 0);
   }

   return sd;
}

// Open a socket channel to the Unix socket at "path".
// Return the channel for success. NULL for failure.
private Channel*
channel_open_unix(CS path) {
   Unt path_len = STRLEN(path);
   struct sockaddr_un   server;

   if (*path == ZERO || path_len >= sizeof(server.sun_path)) {
      showErrFmtMsg(_(e_invalid_argument_str), path);
      return NULL;
   }

   Channel* channel = add_channel();
   if (!channel) {
      ch_error(NULL, "Cannot allocate channel.");
      return NULL;
   }

   CLEAR_FIELD(server);
   server.sun_family = AF_UNIX;
   STRNCPY(server.sun_path, path, sizeof(server.sun_path) - 1);

   ch_log(channel, "Trying to connect to %s", path);

   Unt server_len = offsetof(struct sockaddr_un, sun_path) + path_len + 1;
   int waittime = -1;
   int sd = channel_connect(channel, (struct sockaddr *)&server, (int)server_len, &waittime);
   if (sd < 0) {
      channel_free(channel);
      return NULL;
   }

   ch_log(channel, "Connection made");

   channel->fds[PART_SOCK].ch_fd = (Socket)sd;
   channel->socketName = copyStr((CS)path);
   channel->ch_to_be_closed |= (1U << PART_SOCK);

   return channel;
}

private void
setCallback(Callback* cbp, Callback* callback) {
   evFreeCallback(cbp);

   if (callback->name != NULL && *callback->name != ZERO)
      evCopyCallback(cbp, callback);
   else
      cbp->name = NULL;
}

// Prepare book "book" for writing channel output to.
private void
prepare_buffer(Book* book) {
   Book* curBookSaved = curBook;

   optsCopyToBook(book, BCO_ENTER);
   curBook = book;
   optChangeAndReportError(S"booktype", optStr("nofile"), SET_LOCAL);
   optChangeAndReportError(S"bufhidden", optStr("hide"), SET_LOCAL);
   if (curBook->mem.mfile == NULL)
      ml_open(curBook);
   curBook = curBookSaved;
}

// Find a buffer matching "name" or create a new one.
// Return NULL if there is something very wrong (error already reported).
private Book*
chaFindBook(CS name, int err, int msg) {
   Book* book = NULL;
   Book* curBookSaved = curBook;

   if (name && *name != ZERO) {
      book = booklistFindName(name);
      if (!book)
         book = booklistFindByNameExpandingLinks(name);
   }

   if (book)
      return book;

   book = bookNew(name == NULL || *name == ZERO ? NULL : name,
       NULL, (LineNr)0, BLN_LISTED | BLN_NEW);
   if (!book)
      return NULL;
   prepare_buffer(book);

   curBook = book;
   if (msg) {
      ml_replace(1, (CS)(err ? "Reading from channel error..."
          : "Reading from channel output..."), true);
   } 
   changed_bytes(1, 0);
   curBook = curBookSaved;

   return book;
}

// Set various properties from an "opt" argument.
private void
channel_set_options(Channel* channel, JobOptions* opt) {
   ChannelFdKind   part;
   if (opt->set & JO_MODE) {
      for (part = PART_SOCK; part < PART_COUNT; ++part)
          channel->fds[part].ch_mode = opt->mode;
   } 
   if (opt->set & JO_IN_MODE)
      channel->fds[PART_IN].ch_mode = opt->jo_in_mode;
   if (opt->set & JO_OUT_MODE)
      channel->fds[PART_OUT].ch_mode = opt->jo_out_mode;
   if (opt->set & JO_ERR_MODE)
      channel->fds[PART_ERR].ch_mode = opt->jo_err_mode;
   channel->ch_nonblock = opt->jo_noblock;

   if (opt->set & JO_TIMEOUT) {
      for (part = PART_SOCK; part < PART_COUNT; ++part)
          channel->fds[part].ch_timeout = opt->jo_timeout;
   } 
   if (opt->set & JO_OUT_TIMEOUT)
      channel->fds[PART_OUT].ch_timeout = opt->jo_out_timeout;
   if (opt->set & JO_ERR_TIMEOUT)
      channel->fds[PART_ERR].ch_timeout = opt->jo_err_timeout;
   if (opt->set & JO_BLOCK_WRITE)
      channel->fds[PART_IN].ch_block_write = 1;

   if (opt->set & JO_CALLBACK)
      setCallback(&channel->ch_callback, &opt->jo_callback);
   if (opt->outNativeCb) {
      channel->fds[PART_OUT].nativeCb = opt->outNativeCb;
   } ei(opt->set & JO_OUT_CALLBACK) {
      setCallback(&channel->fds[PART_OUT].ch_callback, &opt->jo_out_cb);
   }
   
   if (opt->errNativeCb) {
      channel->fds[PART_ERR].nativeCb = opt->errNativeCb;
   } ei(opt->set & JO_ERR_CALLBACK) {
      setCallback(&channel->fds[PART_ERR].ch_callback, &opt->jo_err_cb);
   }
   
   if (opt->set & JO_CLOSE_CALLBACK)
      setCallback(&channel->ch_close_cb, &opt->closeCb);
   channel->ch_drop_never = opt->dropNever;

   if ((opt->set & JO_OUT_IO) && opt->ioMode[PART_OUT] == JIO_BUFFER) {
      Book *book;

      // writing output to a buffer. Default mode is NL.
      if (!(opt->set & JO_OUT_MODE))
         channel->fds[PART_OUT].ch_mode = CH_MODE_NL;
      if (opt->set & JO_OUT_BUF) {
         book = bookFindFileByBookNr(opt->ioText[PART_OUT]);
         if (book == NULL)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt->ioText[PART_OUT]);
      } else {
         int msg = true;

         if (opt->set1 & JO2_OUT_MSG)
            msg = opt->jo_message[PART_OUT];
         book = chaFindBook(opt->name[PART_OUT], false, msg);
      }
      if (book) {
         if (opt->set & JO_OUT_MODIFIABLE)
            channel->fds[PART_OUT].ch_nomodifiable = !opt->jo_modifiable[PART_OUT];

         if ((IMMUTABLE) && !channel->fds[PART_OUT].ch_nomodifiable) {
            emsg(_(e_cannot_make_changes_modifiable_is_off));
         } else {
            ch_log(channel, "writing out to book '%s'", book->fullFileName);
            bookStoreInRef(OUT &channel->fds[PART_OUT].bookref, book);
            // if the buffer was deleted or unloaded resurrect it
            if (book->mem.mfile == NULL)
                prepare_buffer(book);
         }
      }
    }

   if ((opt->set & JO_ERR_IO) 
         && (opt->ioMode[PART_ERR] == JIO_BUFFER
          || (opt->ioMode[PART_ERR] == JIO_OUT && (opt->set & JO_OUT_IO)
                      && opt->ioMode[PART_OUT] == JIO_BUFFER))
   ) {
      Book* book;

      // writing err to a buffer. Default mode is NL.
      if (!(opt->set & JO_ERR_MODE))
         channel->fds[PART_ERR].ch_mode = CH_MODE_NL;
      if (opt->ioMode[PART_ERR] == JIO_OUT)
         book = channel->fds[PART_OUT].bookref.c;
      ei (opt->set & JO_ERR_BUF) {
         book = bookFindFileByBookNr(opt->ioText[PART_ERR]);
         if (!book)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt->ioText[PART_ERR]);
      } else {
         int msg = true;

         if (opt->set1 & JO2_ERR_MSG)
            msg = opt->jo_message[PART_ERR];
         book = chaFindBook(opt->name[PART_ERR], true, msg);
      }
      if (book) {
         if (opt->set & JO_ERR_MODIFIABLE)
            channel->fds[PART_ERR].ch_nomodifiable = !opt->jo_modifiable[PART_ERR];
         if ((IMMUTABLE) && !channel->fds[PART_ERR].ch_nomodifiable) {
            emsg(_(e_cannot_make_changes_modifiable_is_off));
         } else {
            ch_log(channel, "writing err to buffer '%s'", book->fullFileName);
            bookStoreInRef(OUT &channel->fds[PART_ERR].bookref, book);
            // if the buffer was deleted or unloaded resurrect it
            if (book->mem.mfile == NULL)
                prepare_buffer(book);
         }
      }
   }

   channel->fds[PART_OUT].ch_io = opt->ioMode[PART_OUT];
   channel->fds[PART_ERR].ch_io = opt->ioMode[PART_ERR];
   channel->fds[PART_IN].ch_io = opt->ioMode[PART_IN];
}

// Implement ch_open().
private Channel *
channel_open_func(Arr(Var) argvars) {
   JobOptions opt;

   CS address = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN && check_for_nonnull_dict_arg(argvars, 1) == FAIL)
      return NULL;

   if (*address == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), address);
      return NULL;
   }

   if (STRNCMP(address, "unix:", 5) == 0) {
      address += 5;
   } else {
      showErrFmtMsg(_(e_invalid_argument_str), address);
      return null;
   } 

   // parse options
   CLEAR_POINTER(&opt);
   opt.mode = CH_MODE_JSON;
   opt.jo_timeout = 2000;
   if (get_job_options(&argvars[1], OUT &opt, JO_MODE_ALL + JO_CB_ALL + JO_TIMEOUT_ALL, 0) == FAIL)
      goto theend;
   if (opt.jo_timeout < 0) {
      emsg(_(e_invalid_argument));
      goto theend;
   }

   Channel* channel = channel_open_unix(address);
   if (channel) {
      opt.set = JO_ALL;
      channel_set_options(channel, &opt);
   }
theend:
   free_job_options(&opt);
   return channel;
}

void
ch_close_part(Channel *channel, ChannelFdKind part) {
   Socket *fd = &channel->fds[part].ch_fd;

   if (*fd == INVALID_FD)
      return;

   if (part == PART_SOCK)
      sock_close(*fd);
   else {
      // When using a pty the same FD is set on multiple parts, only
      // close it when the last reference is closed.
      if ((part == PART_IN || channel->fds[PART_IN].ch_fd != *fd)
         && (part == PART_OUT || channel->fds[PART_OUT].ch_fd != *fd)
         && (part == PART_ERR || channel->fds[PART_ERR].ch_fd != *fd)
      ){
          fd_close(*fd);
      }
   }
   *fd = INVALID_FD;

   // channel is closed, may want to end the job if it was the last
   channel->ch_to_be_closed &= ~(1U << part);
}

void
channel_set_pipes(Channel *channel, Socket in, Socket out, Socket err) {
   if (in != INVALID_FD) {
      ch_close_part(channel, PART_IN);
      channel->fds[PART_IN].ch_fd = in;
      // Do not end the job when all output channels are closed, wait until the job ended.
      if (mch_isatty(in))
          channel->ch_to_be_closed |= (1U << PART_IN);
   }
   if (out != INVALID_FD) {
      ch_close_part(channel, PART_OUT);
      channel->fds[PART_OUT].ch_fd = out;
      channel->ch_to_be_closed |= (1U << PART_OUT);
   }
   if (err != INVALID_FD) {
      ch_close_part(channel, PART_ERR);
      channel->fds[PART_ERR].ch_fd = err;
      channel->ch_to_be_closed |= (1U << PART_ERR);
   }
}

// Set the job the channel is associated with and associated options.
// This does not keep a refcount, when the job is freed job is cleared.
void
channel_set_job(Channel* channel, Job* job, JobOptions* options) {
   channel->job = job;
   channel_set_options(channel, options);

   if (!job->inBook)
      return;

   ChannelFd *in_part = &channel->fds[PART_IN];

   bookStoreInRef(OUT &in_part->bookref, job->inBook);
   ch_log(channel, "reading from buffer '%s'", (char *)in_part->bookref.c->fullFileName);
   if (options->set & JO_IN_TOP) {
      if (options->jo_in_top == 0 && !(options->set & JO_IN_BOT)) {
          // Special mode: send last-but-one line when appending a line
          // to the buffer.
          in_part->bookref.c->writeToChannel = true;
          in_part->ch_buf_append = true;
          in_part->ch_buf_top =
         in_part->bookref.c->mem.lineCount + 1;
      } else
          in_part->ch_buf_top = options->jo_in_top;
   } else
      in_part->ch_buf_top = 1;
   if (options->set & JO_IN_BOT)
      in_part->ch_buf_bot = options->jo_in_bot;
   else
      in_part->ch_buf_bot = in_part->bookref.c->mem.lineCount;
}

// Set the callback for "channel"/"part" for the response with "id".
private void
channel_set_req_callback(Channel* channel, ChannelFdKind part, Callback* callback, int id) {
   CbNode *head = &channel->fds[part].ch_cb_head;
   CbNode *item = ALLOC_ONE(CbNode);

   if (!item)
      return;

   evCopyCallback(&item->cq_callback, callback);
   item->cq_seq_nr = id;
   item->cq_prev = head->cq_prev;
   head->cq_prev = item;
   item->cq_next = NULL;
   if (item->cq_prev == NULL)
      head->cq_next = item;
   else
      item->cq_prev->cq_next = item;
}

private void
write_buf_line(Book* book, LineNr lnum, Channel* channel) {
   CS line = memGetLine(book, lnum, false);
   int len = memGetBookLen(book, lnum);
   int       i;

   // Need to make a copy to be able to append a NL.
   CS p = alloc(len + 2);
   memcpy((char *)p, (char *)line, len);

   if (channel->writeTextMode)
      p[len] = ENTER;
   else {
      for (i = 0; i < len; ++i) {
         if (p[i] == NL)
            p[i] = ZERO;
      } 

      p[len] = NL;
   }
   p[len + 1] = ZERO;
   channel_send(channel, PART_IN, p, len + 1, "write_buf_line");
   eeglFree(p);
}

// true if "channel" can be written to. * false if the input is closed or the write would block.
private int
can_write_buf_line(Channel* channel) {
   ChannelFd *in_part = &channel->fds[PART_IN];

   if (in_part->ch_fd == INVALID_FD)
   return false;  // pipe was closed

   // for testing: block every other attempt to write
   if (in_part->ch_block_write == 1)
   in_part->ch_block_write = -1;
    ei (in_part->ch_block_write == -1)
   in_part->ch_block_write = 1;

   TimeVal   tval;
   fd_set      wfds;
   int      ret;

   FD_ZERO(&wfds);
   FD_SET((int)in_part->ch_fd, &wfds);
   tval.tv_sec = 0;
   tval.tv_usec = 0;
   for (;;) {
      ret = select((int)in_part->ch_fd + 1, NULL, &wfds, NULL, &tval);
      SOCK_ERRNO;
      if (ret == -1 && errno == EINTR)
         continue;
      if (ret <= 0 || in_part->ch_block_write == 1) {
         if (ret > 0)
            ch_log(channel, "FAKED Input not ready for writing");
         else
            ch_log(channel, "Input not ready for writing");
         return false;
      }
      break;
   }
   return true;
}

// Write any buffer lines to the input channel.
void
channel_write_in(Channel* channel) {
   ChannelFd *in_part = &channel->fds[PART_IN];
   LineNr    lnum;
   Book* book = in_part->bookref.c;
   int      written = 0;

   if (!book || in_part->ch_buf_append)
      return;  // no buffer or using appending
   if (!bookRefValid(&in_part->bookref) || book->mem.mfile == NULL) {
      // buffer was wiped out or unloaded
      ch_log(channel, "input buffer has been wiped out");
      in_part->bookref.c = NULL;
      return;
   }

   for (lnum = in_part->ch_buf_top; lnum <= in_part->ch_buf_bot
               && lnum <= book->mem.lineCount; ++lnum
   ) {
      if (!can_write_buf_line(channel))
         break;
      write_buf_line(book, lnum, channel);
      ++written;
   }

   if (written == 1)
      ch_log(channel, "written line %d to channel", (int)lnum - 1);
   ei (written > 1)
      ch_log(channel, "written %d lines to channel", written);

   in_part->ch_buf_top = lnum;
   if (lnum > book->mem.lineCount || lnum > in_part->ch_buf_bot) {
      // Send CTRL-D to close stdin
      if (channel->job != NULL)
          term_send_eof(channel);

      // Writing is done, no longer need the buffer.
      in_part->bookref.c = NULL;
      ch_log(channel, "Finished writing all lines to channel");

      // Close the pipe/socket, so that the other side gets EOF.
      ch_close_part(channel, PART_IN);
   } else
      ch_log(channel, "Still %ld more lines to write", (long)(book->mem.lineCount - lnum + 1));
}

// Handle book "book" being freed, remove it from any channels.
void
chaFreeBook(Book* book) {
   Channel* channel;

   FOR_ALL_CHANNELS(channel) {
      for (ChannelFdKind part = PART_SOCK; part < PART_COUNT; ++part) {
         ChannelFd* fds = &channel->fds[part];

         if (fds->bookref.c == book) {
            ch_log(channel, "%s buffer has been wiped out", chanFdNames[part]);
            fds->bookref.c = NULL;
         }
      }
   } 
}

// Write any lines waiting to be written to "channel".
private void
channel_write_input(Channel* channel) {
   ChannelFd* in_part = &channel->fds[PART_IN];

   if (in_part->ch_writeque.next)
      channel_send(channel, PART_IN, (CS)"", 0, "channel_write_input");
   ei (in_part->bookref.c != NULL) {
      if (in_part->ch_buf_append)
         channel_write_new_lines(in_part->bookref.c);
      else
         channel_write_in(channel);
    }
}

// Write any lines waiting to be written to a channel.
void
channel_write_any_lines(void) {
   Channel* channel;
   FOR_ALL_CHANNELS(channel) {
      channel_write_input(channel);
   } 
}

// Write appended lines above the last one in "book" to the channel.
void
channel_write_new_lines(Book* book) {
   Channel* channel;
   int found_one = false;

   // There could be more than one channel for the buffer, loop over all of them.
   FOR_ALL_CHANNELS(channel) {
      ChannelFd  *in_part = &channel->fds[PART_IN];
      LineNr    lnum;
      int       written = 0;

      if (in_part->bookref.c == book && in_part->ch_buf_append) {
         if (in_part->ch_fd == INVALID_FD)
            continue;  // pipe was closed
         found_one = true;
         for (lnum = in_part->ch_buf_bot; lnum < book->mem.lineCount; ++lnum) {
            if (!can_write_buf_line(channel))
               break;
            write_buf_line(book, lnum, channel);
            ++written;
         }

         if (written == 1)
            ch_log(channel, "written line %d to channel", (int)lnum - 1);
         ei (written > 1)
            ch_log(channel, "written %d lines to channel", written);
         if (lnum < book->mem.lineCount)
            ch_log(channel, "Still %ld more lines to write", (long)(book->mem.lineCount - lnum));

         in_part->ch_buf_bot = lnum;
      }
   }
   if (!found_one)
      book->writeToChannel = false;
}

// Invoke the "callback" on channel "channel". This does not redraw but sets channel_need_redraw;
private void
invoke_callback(Channel *channel, Callback *callback, Var *argv) {
   if (safe_to_invoke_callback == 0)
      internalErrMsg(S"Invoking callback when it is not safe");

   argv[0].tag = VAR_CHANNEL;
   argv[0].channel = channel;

   Var   returnVar;
   call_callback(callback, -1, OUT &returnVar, 2, argv);
   clearVar(&returnVar);
   channel_need_redraw = true;
}

// Return the first node from "channel"/"part" without removing it. Return NULL if there is nothing.
ReadChunk *
channel_peek(Channel *channel, ChannelFdKind part) {
    ReadChunk *head = &channel->fds[part].head;

    return head->next;
}

// Return a pointer to the first NL in "node".
// Skip over ZERO characters. Return NULL if there is no NL.
CS
channel_first_nl(ReadChunk* node) {
   CS buffer = node->c;
   for (Unt i = 0; i < node->len; ++i) {
      if (buffer[i] == NL)
         return buffer + i;
   } 
   return NULL;
}

// Return the first buffer from channel "channel"/"part" and remove it.
// The caller owns it. Return NULL if there is nothing.
private CS
channel_get(Channel* channel, ChannelFdKind part, int *outlen) {
   ReadChunk* head = &channel->fds[part].head;
   ReadChunk* node = head->next;

   if (!node)
      return NULL;
   if (outlen)
      *outlen += node->len;
   // dispose of the node but keep the buffer
   CS p = node->c;
   head->next = node->next;
   if (!node->next)
      head->prev = NULL;
   else
      node->next->prev = NULL;
   eeglFree(node);
   return p;
}

// Return the whole buffer contents concatenated for "channel"/"part". Replace ZERO bytes with NL.
private CS
channel_get_all(Channel *channel, ChannelFdKind part, int *outlen) {
   ReadChunk* head = &channel->fds[part].head;
   ReadChunk* node;
   Ulong  len = 0;

   // Concatenate everything into one buffer.
   for (node = head->next; node != NULL; node = node->next)
      len += node->len;
   CS res = alloc(len + 1);
   CS p = res;
   for (node = head->next; node != NULL; node = node->next) {
      MEMMOVE(p, node->c, node->len);
      p += node->len;
   }
   *p = ZERO;

   // Free all buffers
   do {
      p = channel_get(channel, part, NULL);
      eeglFree(p);
   } while (p);

   if (outlen) {
      // Returning the length, keep ZERO characters.
      *outlen += len;
      return res;
   }

   // Turn all ZERO into newlines, so that the result can be used as a string.
   p = res;
   while (p < res + len) {
      if (*p == ZERO)
         *p = NL;
      ei (*p == 0x1b) {
         // crush the escape sequence OSC 0/1/2: ESC ]0;
         if (p + 3 < res + len
            && p[1] == ']'
            && (p[2] == '0' || p[2] == '1' || p[2] == '2')
            && p[3] == ';'
         ) {
            // '\a' becomes a NL
            while (p < res + (len - 1) && *p != '\a')
               ++p;
            // BEL is zero width characters, suppress display mistake
            // ConPTY (after 10.0.18317) requires advance checking
            if (p[-1] == ZERO)
               p[-1] = 0x07;
         }
      }
      ++p;
   }

    return res;
}

//Consume "len" bytes from the head of "node". Caller must check these bytes are available.
void
channel_consume(Channel *channel, ChannelFdKind part, int len) {
   ReadChunk *head = &channel->fds[part].head;
   ReadChunk *node = head->next;
   CS buf = node->c;

   MEMMOVE(buf, buf + len, node->len - len);
   node->len -= len;
   node->c[node->len] = ZERO;
}

//Collapses the first and second buffer for "channel"/"part". Return FAIL if nothing was done.
//When "want_nl" is true collapse more buffers until a NL is found. When the channel part mode 
//is "lsp", collapse all the buffers as the http header and the JSON content can be present in 
//multiple buffers.
int
channel_collapse(Channel *channel, ChannelFdKind part, int want_nl) {
   ChannelMode   mode = channel->fds[part].ch_mode;
   ReadChunk* head = &channel->fds[part].head;
   ReadChunk* node = head->next;
   ReadChunk* n;

   if (!node || !node->next)
      return FAIL;

   ReadChunk* last_node = node->next;
   Ulong len = node->len + last_node->len;
   if (want_nl || mode == CH_MODE_LSP) {
      while (last_node->next && (mode == CH_MODE_LSP || channel_first_nl(last_node) == NULL)) {
          last_node = last_node->next;
          len += last_node->len;
      }
   } 
   CS newbuf = alloc(len + 1);
   CS p = newbuf;
   MEMMOVE(p, node->c, node->len);
   p += node->len;
   eeglFree(node->c);
   node->c = newbuf;
   for (n = node; n != last_node; ) {
      n = n->next;
      MEMMOVE(p, n->c, n->len);
      p += n->len;
      eeglFree(n->c);
   }
   *p = ZERO;
   node->len = (Ulong)(p - newbuf);

   // dispose of the collapsed nodes and their buffers
   for (n = node->next; n != last_node; ) {
      n = n->next;
      eeglFree(n->prev);
   }
   node->next = last_node->next;
   if (!last_node->next)
      head->prev = node;
   else
      last_node->next->prev = node;
   eeglFree(last_node);
   return OK;
}

// Store "buf[len]" on "channel"/"part".
// When "prepend" is true put in front, otherwise append at the end. Return OK or FAIL.
private int
saveMsg(Channel* channel, ChannelFdKind part, CS msg, int len, int prepend, CS logLead) {
   ReadChunk *head = &channel->fds[part].head;
   Byte  *p;
   int       i;
   ReadChunk* node = ALLOC_ONE(ReadChunk);
   // A ZERO is added at the end, because netbeans code expects that.
   // Otherwise a ZERO may appear inside the text.
   node->c = alloc(len + 1);

   if (channel->fds[part].ch_mode == CH_MODE_NL) {
      // Drop any CR before a newline.
      p = node->c;
      for (i = 0; i < len; ++i) {
         if (msg[i] != ENTER || i + 1 >= len || msg[i + 1] != NL)
            *p++ = msg[i];
      } 
      *p = ZERO;
      node->len = (Ulong)(p - node->c);
   } else {
      MEMMOVE(node->c, msg, len);
      node->c[len] = ZERO;
      node->len = (Ulong)len;
   }

   if (prepend) {
      // prepend node to the head of the queue
      node->next = head->next;
      node->prev = NULL;
      if (head->next == NULL)
         head->prev = node;
      else
         head->next->prev = node;
      head->next = node;
   } else {
      // append node to the tail of the queue
      node->next = NULL;
      node->prev = head->prev;
      if (head->prev == NULL)
         head->next = node;
      else
         head->prev->next = node;
      head->prev = node;
   }

   if (ch_log_active() && logLead)
      ch_log_literal(logLead, channel, part, OUT (Text){msg, len});

   return OK;
}

// Try to fill the buffer of "reader". Returns false when nothing was added.
private int
channel_fill(JsReader* reader) {
   Channel* channel = (Channel *)reader->js_cookie;
   ChannelFdKind   part = reader->js_cookie_arg;
   CS next = channel_get(channel, part, NULL);
   int      addlen;
   CS p;

   if (next == NULL)
      return false;

   int keeplen = reader->js_end - reader->js_buf;
   if (keeplen > 0) {
      // Prepend unused text.
      addlen = (int)STRLEN(next);
      p = alloc(keeplen + addlen + 1);
      MEMMOVE(p, reader->js_buf, keeplen);
      MEMMOVE(p + keeplen, next, addlen + 1);
      eeglFree(next);
      next = p;
   }

    eeglFree(reader->js_buf);
    reader->js_buf = next;
    return true;
}

// Process the HTTP header in a Language Server Protocol (LSP) message.
//
// The message format is described in the LSP specification:
// https://microsoft.github.io/language-server-protocol/specification
//
// It has the following two fields:
//
//   Content-Length: ...
//   Content-Type: application/vscode-jsonrpc; charset=utf-8
//
// Each field ends with "\r\n". The header ends with an additional "\r\n".
//
// Return OK if a valid header is received and FAIL if some fields in the
// header are not correct. Return MAYBE if a partial header is received and
// need to wait for more data to arrive.
private int
channel_process_lsp_http_hdr(JsReader* reader) {
   Byte   *line_start;
   Byte   *p;
   Unt   hdr_len;
   int      payload_len = -1;
   Unt   jsbuf_len;

   // We find the end once, to avoid calling strlen() many times.
   jsbuf_len = (Unt)STRLEN(reader->js_buf);
   reader->js_end = reader->js_buf + jsbuf_len;

   p = reader->js_buf;

   // Process each line in the header till an empty line is read (header separator).
   while (true) {
      line_start = p;
      while (*p != ZERO && *p != '\n')
         p++;
      if (*p == ZERO)         // partial header
         return MAYBE;
      p++;

      // process the content length field (if present)
      if ((p - line_start > 16) && STRNICMP(line_start, "Content-Length: ", 16) == 0) {
         errno = 0;
         payload_len = strtol((char *)line_start + 16, NULL, 10);
         if (errno == ERANGE || payload_len < 0)
            // invalid length, discard the payload
            return FAIL;
      }

      if ((p - line_start) == 2 && line_start[0] == '\r' &&
         line_start[1] == '\n')
         // reached the empty line
         break;
   }

   if (payload_len == -1)
      // Content-Length field is not present in the header
      return FAIL;

   hdr_len = p - reader->js_buf;

    // if the entire payload is not received, wait for more data to arrive
   if (jsbuf_len < hdr_len + payload_len)
      return MAYBE;

   reader->js_used += hdr_len;
   // recalculate the end based on the length read from the header.
   reader->js_end = reader->js_buf + hdr_len + payload_len;

   return OK;
}

// Use the read buffer of "channel"/"part" and parse a JSON message that is
// complete.  The messages are added to the queue. Return true if there is more to read.
private int
channel_parse_json(Channel* channel, ChannelFdKind part) {
   Var   listtv;
   ChannelFd   *chanpart = &channel->fds[part];
   JsonQ   *head = &chanpart->ch_json_head;
   int      status = OK;
   int      ret;

   if (channel_peek(channel, part) == NULL)
      return false;

   JsReader reader;
   reader.js_buf = channel_get(channel, part, NULL);
   reader.js_used = 0;
   reader.js_fill = channel_fill;
   reader.js_cookie = channel;
   reader.js_cookie_arg = part;

   if (chanpart->ch_mode == CH_MODE_LSP)
      status = channel_process_lsp_http_hdr(&reader);

   // When a message is incomplete we wait for a short while for more to
   // arrive.  After the delay drop the input, otherwise a truncated string
   // or list will make us hang.
   // Do not generate error messages, they will be written in a channel log.
   if (status == OK) {
      ++emsg_silent;
      status = json_decode(OUT &listtv, &reader);
      --emsg_silent;
   }
   if (status == OK) {
      // Only accept the response when it is a list with at least two items.
      if (chanpart->ch_mode == CH_MODE_LSP && listtv.tag != VAR_BAG) {
         ch_error(channel, "Did not receive a LSP dict, discarding");
         clearVar(&listtv);
      }
      ei (chanpart->ch_mode != CH_MODE_LSP && (listtv.tag != VAR_LIST || listtv.list->len < 2)) {
         if (listtv.tag != VAR_LIST)
            ch_error(channel, "Did not receive a list, discarding");
         else
            ch_error(channel, "Expected list with two items, got %d", listtv.list->len);
         clearVar(&listtv);
      } else {
         JsonQ* item = ALLOC_ONE(JsonQ);
         if (item == NULL)
            clearVar(&listtv);
         else {
            item->jq_no_callback = false;
            item->jq_value = allocVar();
            if (item->jq_value == NULL) {
               eeglFree(item);
               clearVar(&listtv);
            } else {
               *item->jq_value = listtv;
               item->jq_prev = head->jq_prev;
               head->jq_prev = item;
               item->jq_next = NULL;
               if (item->jq_prev == NULL)
                  head->jq_next = item;
               else
                  item->jq_prev->jq_next = item;
            }
          }
      }
   }

   if (status == OK)
      chanpart->ch_wait_len = 0;
   ei (status == MAYBE) {
      Unt buflen = STRLEN(reader.js_buf);

      if (chanpart->ch_wait_len < buflen) {
         // First time encountering incomplete message or after receiving
         // more (but still incomplete): set a deadline of 100 msec.
         ch_log(channel,
            "Incomplete message (%d bytes) - wait 100 msec for more",
            (int)buflen);
         reader.js_used = 0;
         chanpart->ch_wait_len = buflen;
         gettimeofday(&chanpart->deadline, NULL);
         chanpart->deadline.tv_usec += 100 * 1000;
         if (chanpart->deadline.tv_usec > 1000 * 1000) {
           chanpart->deadline.tv_usec -= 1000 * 1000;
           ++chanpart->deadline.tv_sec;
         }
      } else {
         int timeout;
         {
         TimeVal now_tv;

         gettimeofday(&now_tv, NULL);
         timeout = now_tv.tv_sec > chanpart->deadline.tv_sec
               || (now_tv.tv_sec == chanpart->deadline.tv_sec
               && now_tv.tv_usec > chanpart->deadline.tv_usec);
         }
         if (timeout) {
            status = FAIL;
            chanpart->ch_wait_len = 0;
            ch_log(channel, "timed out");
         } else {
            reader.js_used = 0;
            ch_log(channel, "still waiting on incomplete message");
         }
      }
   }

   if (status == FAIL) {
      ch_error(channel, "Decoding failed - discarding input");
      ret = false;
      chanpart->ch_wait_len = 0;
   } ei (reader.js_buf[reader.js_used] != ZERO) {
      // Put the unread part back into the channel.
      saveMsg(channel, part, reader.js_buf + reader.js_used,
            (int)(reader.js_end - reader.js_buf) - reader.js_used, true, NULL);
      ret = status == MAYBE ? false: true;
   } else
      ret = false;

   eeglFree(reader.js_buf);
   return ret;
}

// Remove "node" from the queue that it is in.  Does not free it.
private void
remove_cb_node(CbNode* head, CbNode* node) {
   if (node->cq_prev == NULL)
      head->cq_next = node->cq_next;
   else
      node->cq_prev->cq_next = node->cq_next;
   if (node->cq_next == NULL)
      head->cq_prev = node->cq_prev;
   else
      node->cq_next->cq_prev = node->cq_prev;
}

// Remove "node" from the queue that it is in and free it.
// Caller should have freed or used node->jq_value.
private void
remove_json_node(JsonQ* head, JsonQ* node) {
   if (!node->jq_prev)
      head->jq_next = node->jq_next;
   else
      node->jq_prev->jq_next = node->jq_next;
   if (!node->jq_next)
      head->jq_prev = node->jq_prev;
   else
      node->jq_next->jq_prev = node->jq_prev;
   eeglFree(node);
}

// Add "id" to the list of JSON message IDs we are waiting on.
private void
channel_add_block_id(ChannelFd* chanpart, int id) {
   ArrayList* gap = &chanpart->ch_block_ids;

   if (gap->ga_growsize == 0)
      ga_init2(gap, sizeof(int), 10);
   if (ga_grow(gap, 1) == OK) {
      ((int *)gap->c)[gap->len] = id;
      ++gap->len;
   }
}

// Remove "id" from the list of JSON message IDs we are waiting on.
private void
channel_remove_block_id(ChannelFd* chanpart, int id) {
   ArrayList* gap = &chanpart->ch_block_ids;

   for (int i = 0; i < gap->len; ++i) {
      if (((int *)gap->c)[i] == id) {
         --gap->len;
         if (i < gap->len) {
            int *p = ((int *)gap->c) + i;
            MEMMOVE(p, p + 1, (gap->len - i) * sizeof(int));
         }
         return;
      }
   } 
   internalErrFmtMsg("channel_remove_block_id(): cannot find id %d", id);
}

// Return true if "id" is in the list of JSON message IDs we are waiting on.
private int
channel_has_block_id(ChannelFd* chanpart, int id) {
   ArrayList   *gap = &chanpart->ch_block_ids;
   for (int i = 0; i < gap->len; ++i) {
      if (((int *)gap->c)[i] == id)
          return true;
   } 
   return false;
}

// Get a message from the JSON queue for channel "channel". When "id" is positive it must match 
// the first number in the list. When "id" is zero or negative jut get the first message. But not 
// one in the ch_block_ids list. When "without_callback" is true also get messages that were 
// pushed back. Return OK when found and return the value in "returnVar". FAIL otherwise.
private int
channel_get_json(
   Channel   *channel,
   ChannelFdKind   part,
   int       id,
   int       without_callback,
   Var    **returnVar
) {
   JsonQ* head = &channel->fds[part].ch_json_head;
   JsonQ* item = head->jq_next;

   while (item) {
      List* l;
      Var* tv;

      if (channel->fds[part].ch_mode != CH_MODE_LSP) {
         l = item->jq_value->list;
         CHECK_LIST_MATERIALIZE(l);
         tv = &l->first->c;
      } else {
         // LSP message payload is a JSON-RPC dict. For RPC requests and responses, the 'id' 
         // item will be present. For notifications, it will not be present.
         if (id > 0) {
            if (item->jq_value->tag != VAR_BAG)
               goto nextitem;
            Bag* d = item->jq_value->bag;
            if (!d)
               goto nextitem;
            // When looking for a response message from the LSP server,
            // ignore new LSP request and notification messages.  LSP
            // request and notification messages have the "method" field in
            // the header and the response messages do not have this field.
            if (bagHasKey(d, tConst("method")))
                goto nextitem;
            DictItem* di = bagFind(d, tConst("id"));
            if (!di)
               goto nextitem;
            tv = &di->c;
         } else
            tv = item->jq_value;
      }

      if ((without_callback || !item->jq_no_callback)
          && ((id > 0 && tv->tag == VAR_NUMBER && tv->number == id)
            || (id <= 0 && (tv->tag != VAR_NUMBER
             || tv->number == 0
             || !channel_has_block_id( &channel->fds[part], tv->number))))
      ) {
         *returnVar = item->jq_value;
         if (tv->tag == VAR_NUMBER)
            ch_log(channel, "Getting JSON message %ld", (long)tv->number);
         remove_json_node(head, item);
         return OK;
      }
   nextitem:
      item = item->jq_next;
   }
   return FAIL;
}

// Put back "returnVar" into the JSON queue, there was no callback for it.
// Take over the values in "returnVar".
private void
channel_push_json(Channel* channel, ChannelFdKind part, Var* returnVar) {
   JsonQ* head = &channel->fds[part].ch_json_head;
   JsonQ* item = head->jq_next;
   JsonQ* newitem;

   if (head->jq_prev != NULL && head->jq_prev->jq_no_callback)
      // last item was pushed back, append to the end
      item = NULL;
   else while (item != NULL && item->jq_no_callback)
      // append after the last item that was pushed back
      item = item->jq_next;

   newitem = ALLOC_ONE(JsonQ);
   newitem->jq_value = allocVar();

   newitem->jq_no_callback = false;
   *newitem->jq_value = *returnVar;
   if (!item) {
      // append to the end
      newitem->jq_prev = head->jq_prev;
      head->jq_prev = newitem;
      newitem->jq_next = NULL;
      if (newitem->jq_prev == NULL)
          head->jq_next = newitem;
      else
          newitem->jq_prev->jq_next = newitem;
   } else {
      // append after "item"
      newitem->jq_prev = item;
      newitem->jq_next = item->jq_next;
      item->jq_next = newitem;
      if (newitem->jq_next == NULL)
         head->jq_prev = newitem;
      else
         newitem->jq_next->jq_prev = newitem;
   }
}

#define CH_JSON_MAX_ARGS 4

// Execute a command received over "channel"/"part"
// "argv[0]" is the command string.
// "argv[1]" etc. have further arguments, type is VAR_UNKNOWN if missing.
private void
channel_exe_cmd(Channel *channel, ChannelFdKind part, Var *argv) {
   CS cmd = argv[0].string;
   if (argv[1].tag != VAR_STRING) {
      ch_error(channel, "received command with non-string argument");
      if (p_verbose > 2)
         emsg(_(e_received_command_with_non_string_argument));
      return;
   }
   CS arg = argv[1].string;
   if (arg == NULL)
      arg = (CS)"";

   if (STRCMP(cmd, "ex") == 0) {
      int   called_emsg_before = called_emsg;
      Byte   *p = arg;
      int   do_emsg_silent;

      ch_log(channel, "Executing command '%s'", (char *)arg);
      do_emsg_silent = !checkforcmd(&p, S"echoerr", 5);
      if (do_emsg_silent)
          ++emsg_silent;
      executeCommLine(arg);
      if (do_emsg_silent)
          --emsg_silent;
      if (called_emsg > called_emsg_before)
          ch_log(channel, "Command error: '%s'", (char *)get_EeglVar_str(VV_ERRMSG));
   } ei (STRCMP(cmd, "normal") == 0) {
      ch_log(channel, "Executing normal command '%s'", (char *)arg);
      Invocation ea;
      CLEAR_FIELD(ea);
      ea.arg = arg;
      ea.addr_count = 0;
      ea.forceit = true; // no mapping
      c_normal(&ea);
   } ei (STRCMP(cmd, "redraw") == 0) {
      ch_log(channel, "redraw");
      redraw_cmd(*arg != ZERO);
      showruler(false);
      setcursor();
      out_flush();
   } ei (STRCMP(cmd, "expr") == 0 || STRCMP(cmd, "call") == 0) {
      int is_call = cmd[0] == 'c';
      int id_idx = is_call ? 3 : 2;

      if (argv[id_idx].tag != VAR_UNKNOWN && argv[id_idx].tag != VAR_NUMBER) {
         ch_error(channel, "last argument for expr/call must be a number");
         if (p_verbose > 2)
            emsg(_(e_last_argument_for_expr_call_must_be_number));
      } ei (is_call && argv[2].tag != VAR_LIST) {
         ch_error(channel, "third argument for call must be a list");
         if (p_verbose > 2)
            emsg(_(e_third_argument_for_call_must_be_list));
      } else {
         Var   *tv = NULL;
         Var   res_tv;
         Var   err_tv;
         Byte   *json = NULL;

         // Don't pollute the display with errors. Do generate the errors so that try/catch works.
         ++emsg_silent;
         if (!is_call) {
            ch_log(channel, "Evaluating expression '%s'", (char *)arg);
            tv = eval_expr(arg, NULL);
         } else {
            ch_log(channel, "Calling '%s'", (char *)arg);
            if (func_call(arg, &argv[2], NULL, NULL, &res_tv) == OK)
               tv = &res_tv;
         }

         if (argv[id_idx].tag == VAR_NUMBER) {
            int id = argv[id_idx].number;

            if (tv)
               json = json_encode_nr_expr(id, tv, JSON_NL);
            if (tv == NULL || (json != NULL && *json == ZERO)) {
               // If evaluation failed or the result can't be encoded
               // then return the string "ERROR".
               eeglFree(json);
               err_tv.tag = VAR_STRING;
               err_tv.string = (CS)"ERROR";
               json = json_encode_nr_expr(id, &err_tv, JSON_NL);
            }
            if (json) {
               channel_send(channel,
                   part == PART_SOCK ? PART_SOCK : PART_IN,
                   json, (int)STRLEN(json), (char *)cmd);
               eeglFree(json);
            }
         }
         --emsg_silent;
         if (tv == &res_tv)
            clearVar(tv);
         else
            freeVar(tv);
      }
   } ei (p_verbose > 2) {
      ch_error(channel, "Received unknown command: %s", (char *)cmd);
      showErrFmtMsg(_(e_received_unknown_command_str), cmd);
   } }

// Invoke the callback at "cbhead". Does not redraw but sets channel_need_redraw.
private void
invoke_one_time_callback(Channel* channel, CbNode* cbhead, CbNode* item, Var* argv) {
   ch_log(channel, "Invoking one-time callback %s", (char *)item->cq_callback.name);
   // Remove the item from the list first, if the callback
   // invokes ch_close() the list will be cleared.
   remove_cb_node(cbhead, item);
   invoke_callback(channel, &item->cq_callback, argv);
   evFreeCallback(&item->cq_callback);
   eeglFree(item);
}

private void
appendToBook(Book* book, CS msg, Channel* channel, ChannelFdKind part) {
   AutocommSave   aco;
   LineNr    lnum = book->mem.lineCount;
   int      save_write_to = book->writeToChannel;
   ChannelFd* fds = &channel->fds[part];
   Boole      save_p_ma = book->o.modifiable;
   int      empty = (book->mem.flags & ML_EMPTY) ? 1 : 0;

   if (!book->o.modifiable && !fds->ch_nomodifiable) {
      if (!fds->ch_nomod_error) {
         ch_error(channel, "Book is not modifiable, cannot append");
         fds->ch_nomod_error = true;
      }
      return;
   }

   // If the book is also used as input insert above the last line. Don't write these lines.
   if (save_write_to) {
      --lnum;
      book->writeToChannel = false;
   }

   // Append to the book
   ch_log(channel, "appending line %d to book %s", (int)lnum + 1 - empty, book->currFileName);

   book->o.modifiable = true;

   // Set curBook to "book", temporarily.
   auCommPrepareBook(&aco, book);
   if (curBook != book) {
      //Could not find a portal into this book, the following might cause trouble, better bail out.
      return;
   }

   u_sync(true);
   // ignore undo failure, undo is not very useful here
   (void)u_save(lnum - empty, lnum + 1);

   if (empty) {
      // The book is empty, replace the first (dummy) line.
      ml_replace(lnum, msg, true);
      lnum = 0;
   } else
      ml_append(lnum, msg, 0, false);
   appended_lines_mark(lnum, 1L);

   // reset notion of book
   auCommRestoreBook(&aco);

   if (fds->ch_nomodifiable) {
      book->o.modifiable = false;
   } else {
      book->o.modifiable = save_p_ma;
   }

   if (book->countPortals > 0) {
      Portal   *wp;
      FOR_ALL_PORTALS(wp) {
         if (wp->book == book) {
            int move_cursor = save_write_to
                   ? wp->cursor.lnum == lnum + 1
                   : (wp->cursor.lnum == lnum && wp->cursor.col == 0);

            // If the cursor is at or above the new line, move it one line
            // down.  If the topline is outdated update it now.
            if (move_cursor || wp->topLine > book->mem.lineCount) {
               Portal *save_curPor = curPor;

               if (move_cursor)
                  ++wp->cursor.lnum;
               curPor = wp;
               curBook = curPor->book;
               scroll_cursor_bot(0, false);
               curPor = save_curPor;
               curBook = curPor->book;
            }
         }
      }
      drawBookAndStatusLater(book, UPD_VALID);
      channel_need_redraw = true;
    }

   if (save_write_to) {
      Channel *ch;

      // Find channels reading from this book and adjust their next-to-read line number.
      book->writeToChannel = true;
      FOR_ALL_CHANNELS(ch) {
          ChannelFd  *in_part = &ch->fds[PART_IN];

          if (in_part->bookref.c == book)
         in_part->ch_buf_bot = book->mem.lineCount;
      }
   }
}

private void
drop_messages(Channel* channel, ChannelFdKind part) {
   CS msg;
   while ((msg = channel_get(channel, part, NULL)) != NULL) {
      ch_log(channel, "Dropping message '%s'", (char *)msg);
      eeglFree(msg);
   }
}

// true if for "channel" / "part" ch_json_head should be used.
private int
channel_use_json_head(Channel* channel, ChannelFdKind part) {
   ChannelMode   ch_mode = channel->fds[part].ch_mode;
   return ch_mode == CH_MODE_JSON || ch_mode == CH_MODE_LSP;
}

// Invoke a callback for "channel"/"part" if needed. This does not redraw but sets 
// channel_need_redraw when redraw is needed. Return true when a message was handled, there might 
// be another one.
private int
may_invoke_callback(Channel* channel, ChannelFdKind part) {
   Byte   *msg = NULL;
   Var   *listtv = NULL;
   Var   argv[CH_JSON_MAX_ARGS];
   int      seq_nr = -1;
   ChannelFd* fdData = &channel->fds[part];
   ChannelMode ch_mode = fdData->ch_mode;
   CbNode* cbhead = &fdData->ch_cb_head;
   CbNode* cbitem;
   Callback* callback = NULL;
   Byte   *p;
   int      called_otc;      // one time callbackup

   // Use a message-specific callback, part callback or channel callback
   for (cbitem = cbhead->cq_next; cbitem != NULL; cbitem = cbitem->cq_next) {
      if (cbitem->cq_seq_nr == 0)
          break;
   } 
   
   void (*nativeCallback)(Arr(Byte)) = NULL; // if non-null, overtakes non-native callbacks
   if (fdData->nativeCb != NULL) {
      nativeCallback = fdData->nativeCb;
   } else {
      if (cbitem != NULL)
         callback = &cbitem->cq_callback;
      ei (fdData->ch_callback.name != NULL)
         callback = &fdData->ch_callback;
      ei (channel->ch_callback.name != NULL)
         callback = &channel->ch_callback;
   } 

   Book* book = fdData->bookref.c;
   if (book && (!bookRefValid(&fdData->bookref) || book->mem.mfile == NULL)) {
      // book was wiped out or unloaded
      ch_log(channel, "%s book has been wiped out", chanFdNames[part]);
      fdData->bookref.c = NULL;
      book = NULL;
   }

   if (channel_use_json_head(channel, part)) {
      ListItem* item;
      int      argc = 0;

      // Get any json message in the queue.
      if (channel_get_json(channel, part, -1, false, &listtv) == FAIL) {
         if (ch_mode == CH_MODE_LSP)
            // In the "lsp" mode, the http header and the json payload may
            // be received in multiple messages. So concatenate all the received messages.
            (void)channel_collapse(channel, part, false);

         // Parse readahead, return when there is still no message.
         channel_parse_json(channel, part);
         if (channel_get_json(channel, part, -1, false, &listtv) == FAIL)
            return false;
      }

      if (ch_mode == CH_MODE_LSP) {
         Bag   *d = listtv->bag;
         seq_nr = 0;
         if (d) {
            DictItem* di = bagFind(d, tConst("id"));
            if (di && di->c.tag == VAR_NUMBER)
               seq_nr = di->c.number;
         }

         argv[1] = *listtv;
      } else {
         for (item = listtv->list->first;
             item != NULL && argc < CH_JSON_MAX_ARGS;
             item = item->next
         )
            argv[argc++] = item->c;
         while (argc < CH_JSON_MAX_ARGS)
            argv[argc++].tag = VAR_UNKNOWN;

         if (argv[0].tag == VAR_STRING) {
            // ["cmd", arg] or ["cmd", arg, arg] or ["cmd", arg, arg, arg]
            channel_exe_cmd(channel, part, argv);
            freeVar(listtv);
            return true;
         }

         if (argv[0].tag != VAR_NUMBER) {
            ch_error(channel, "Dropping message with invalid sequence number type");
            freeVar(listtv);
            return false;
         }
         seq_nr = argv[0].number;
      }
   } ei (channel_peek(channel, part) == NULL) {
      // nothing to read on RAW or NL channel
      return false;
   }  else {
      // If there is no callback or book, drop the message.
      if (!nativeCallback && !callback && !book) {
         // If there is a close callback it may use ch_read() to get the messages.
         if (channel->ch_close_cb.name == NULL && !channel->ch_drop_never)
            drop_messages(channel, part);
         return false;
      }

      if (ch_mode == CH_MODE_NL) {
         CS nl = NULL;
         ReadChunk *node;

         // See if we have a message ending in NL in the first book.  If
         // not try to concatenate the first and the second book.
         while (true) {
            node = channel_peek(channel, part);
            nl = channel_first_nl(node);
            if (nl)
                break;
            if (channel_collapse(channel, part, true) == FAIL) {
               if (fdData->ch_fd == INVALID_FD && node->len > 0)
                  break;
               return false; // incomplete message
            }
         }
         CS buf = node->c;

         // Convert ZERO to NL, the internal representation.
         for (p = buf; (nl == NULL || p < nl) && p < buf + node->len; ++p) {
            if (*p == ZERO)
               *p = NL;
         } 

         if (nl == NULL) {
            // get the whole buffer, drop the NL
            msg = channel_get(channel, part, NULL);
         } ei (nl + 1 == buf + node->len) {
            // get the whole buffer
            msg = channel_get(channel, part, NULL);
            *nl = ZERO;
         } else {
            // Copy the message into allocated memory (excluding the NL)
            // and remove it from the buffer (including the NL).
            msg = copySubstr(buf, nl - buf);
            channel_consume(channel, part, (int)(nl - buf) + 1);
         }
      } else {
          //For a raw channel we don't know where the message ends, just get everything we have.
          //Convert ZERO to NL, the internal representation.
          msg = channel_get_all(channel, part, NULL);
      }

      if (msg == NULL)
         return false; // out of memory (and avoids Coverity warning)

      argv[1].tag = VAR_STRING;
      argv[1].string = msg;
   }

   called_otc = false;
   if (seq_nr > 0) {
      // JSON or LSP mode: invoke the one-time callback with the matching nr
      int lsp_req_msg = false;

      // Don't use a LSP server request message with the same sequence number
      // as the client request message as the response message.
      if (ch_mode == CH_MODE_LSP && argv[1].tag == VAR_BAG 
            && bagHasKey(argv[1].bag, tConst("method"))) {
         lsp_req_msg = true;
      } 

      if (!lsp_req_msg) {
         for (cbitem = cbhead->cq_next; cbitem != NULL; cbitem = cbitem->cq_next) {
            if (cbitem->cq_seq_nr == seq_nr) {
               invoke_one_time_callback(channel, cbhead, cbitem, argv);
               called_otc = true;
               break;
            }
         }
      }
   }

   if (seq_nr > 0 && (ch_mode != CH_MODE_LSP || called_otc)) {
      if (!called_otc) {
          // If the 'drop' channel attribute is set to 'never' or if
          // ch_evalexpr() is waiting for this response message, then don't drop this message.
          if (channel->ch_drop_never) {
            // message must be read with ch_read()
            channel_push_json(channel, part, listtv);

            // Change the type to avoid the value being freed.
            listtv->tag = VAR_NUMBER;
            freeVar(listtv);
            listtv = NULL;
         } else
            ch_log(channel, "Dropping message %d without callback", seq_nr);
      }
   } ei (nativeCallback != NULL || callback != NULL || book != NULL) {
      if (book) {
         if (msg == NULL)
            // JSON or JS mode: re-encode the message.
            msg = json_encode(listtv, ch_mode);
         if (msg != NULL) {
            if (book->term != NULL)
               write_to_term(book, msg, channel);
            else
               appendToBook(book, msg, channel, part);
         }
      }
      if (nativeCallback != NULL && msg != NULL) {
         (*nativeCallback)(msg);
      } ei (callback != NULL) {
         if (cbitem != NULL)
            invoke_one_time_callback(channel, cbhead, cbitem, argv);
         else {
            // invoke the channel callback
            ch_log(channel, "Invoking channel callback %s", (char *)callback->name);
            invoke_callback(channel, callback, argv);
         }
      }
   } else
      ch_log(channel, "Dropping message %d", seq_nr);

   if (listtv != NULL)
      freeVar(listtv);
   eeglFree(msg);

   return true;
}

#if defined(PROTO)
// Return true when channel "channel" is open for writing to. false for invalid "channel".
int
channel_can_write_to(Channel* channel) {
   return channel != NULL && (channel->fds[PART_SOCK].ch_fd != INVALID_FD
           || channel->fds[PART_IN].ch_fd != INVALID_FD);
}
#endif

// Return true when channel "channel" is open for reading or writing. false for invalid "channel".
int
channel_is_open(Channel *channel) {
    return channel != NULL && (channel->fds[PART_SOCK].ch_fd != INVALID_FD
           || channel->fds[PART_IN].ch_fd != INVALID_FD
           || channel->fds[PART_OUT].ch_fd != INVALID_FD
           || channel->fds[PART_ERR].ch_fd != INVALID_FD);
}

// Return a pointer indicating the readahead.  Can only be compared between
// calls.  Returns NULL if there is no readahead.
private void *
channel_readahead_pointer(Channel* channel, ChannelFdKind part) {
   if (channel_use_json_head(channel, part)) {
      JsonQ   *head = &channel->fds[part].ch_json_head;

      if (head->jq_next == NULL)
          // Parse json from readahead, there might be a complete message to process.
          channel_parse_json(channel, part);

      return head->jq_next;
   }
   return channel_peek(channel, part);
}

// true if "channel" has JSON or other typeahead.
private int
channel_has_readahead(Channel *channel, ChannelFdKind part) {
   return channel_readahead_pointer(channel, part) != NULL;
}

// Return a string indicating the status of the channel.
// If "req_part" is not negative check that part.
private CS
channel_status(Channel *channel, int req_part) {
   ChannelFdKind part;
   int has_readahead = false;

   if (!channel)
      return S"fail";
   if (req_part == PART_OUT) {
      if (channel->fds[PART_OUT].ch_fd != INVALID_FD)
         return S"open";
      if (channel_has_readahead(channel, PART_OUT))
         has_readahead = true;
   } ei (req_part == PART_ERR) {
      if (channel->fds[PART_ERR].ch_fd != INVALID_FD)
         return S"open";
      if (channel_has_readahead(channel, PART_ERR))
         has_readahead = true;
   } else {
      if (channel_is_open(channel))
         return S"open";
      for (part = PART_SOCK; part < PART_IN; ++part) {
         if (channel_has_readahead(channel, part)) {
            has_readahead = true;
            break;
         }
      } 
   }

   if (has_readahead)
      return S"buffered";
   return S"closed";
}

private void
channel_part_info(Channel *channel, Bag *dict, CS name, ChannelFdKind part) {
   ChannelFd *chanpart = &channel->fds[part];
   Byte   namebuf[20];  // longest is "sock_timeout"
   Unt   tail;
   CS s = E;

   copySubstrToAllocation(namebuf, (Text){name, 4});
   STRCAT(namebuf, "_");
   tail = STRLEN(namebuf);

   STRCPY(namebuf + tail, "status");
   CS status;
   if (chanpart->ch_fd != INVALID_FD)
      status = S"open";
   ei (channel_has_readahead(channel, part))
      status = S"buffered";
   else
      status = S"closed";
   bagAddString(dict, namebuf, (CS)status);

   STRCPY(namebuf + tail, "mode");
   switch (chanpart->ch_mode) {
   case CH_MODE_NL: s = S"NL"; break;
   case CH_MODE_RAW: s = S"RAW"; break;
   case CH_MODE_JSON: s = S"JSON"; break;
   case CH_MODE_LSP: s = S"LSP"; break;
   }
   bagAddString(dict, namebuf, s);

   STRCPY(namebuf + tail, "io");
   if (part == PART_SOCK)
      s = S"socket";
   else switch (chanpart->ch_io) {
      case JIO_NULL: s = S"null"; break;
      case JIO_PIPE: s = S"pipe"; break;
      case JIO_FILE: s = S"file"; break;
      case JIO_BUFFER: s = S"buffer"; break;
      case JIO_OUT: s = S"out"; break;
   }
   bagAddString(dict, namebuf, (CS)s);

   STRCPY(namebuf + tail, "timeout");
   bagAddNumber(dict, namebuf, chanpart->ch_timeout);
}

private void
channelInfoIntoDict(Channel *channel, OUT Bag *dict) {
   bagAddNumber(dict, S"id", channel->id);
   bagAddString(dict, S"status", channel_status(channel, -1));

   if (channel->socketName) {
      bagAddString(dict, S"path", (CS)channel->socketName);
      channel_part_info(channel, dict, S"sock", PART_SOCK);
   } else {
      channel_part_info(channel, dict, S"out", PART_OUT);
      channel_part_info(channel, dict, S"err", PART_ERR);
      channel_part_info(channel, dict, S"in", PART_IN);
   }
}

// Close channel "channel".
// Trigger the close callback if "invoke_close_cb" is true. Does not clear the buffers.
private void
channel_close(Channel *channel, int invoke_close_cb) {
    ch_log(channel, "Closing channel");

    ch_close_part(channel, PART_SOCK);
    ch_close_part(channel, PART_IN);
    ch_close_part(channel, PART_OUT);
    ch_close_part(channel, PART_ERR);

   if (invoke_close_cb) {
      ChannelFdKind   part;

      // let the terminal know it is closing to avoid getting stuck
      term_channel_closing(channel);
      // Invoke callbacks and flush buffers before the close callback.
      if (channel->ch_close_cb.name != NULL)
         ch_log(channel, "Invoking callbacks and flushing buffers before closing");
      for (part = PART_SOCK; part < PART_IN; ++part) {
         if (channel->ch_close_cb.name || channel->fds[part].bookref.c) {
            //Increment the refcount to avoid the channel being freed halfway.
            ++channel->refCount;
            if (channel->ch_close_cb.name == NULL)
                ch_log(channel, "flushing %s buffers before closing", chanFdNames[part]);
            while (may_invoke_callback(channel, part))
               {} 
            --channel->refCount;
         }
      }

      if (channel->ch_close_cb.name) {
         Var argv[1];
         Var returnVar;

         // Increment the refcount to avoid the channel being freed halfway.
         ++channel->refCount;
         ch_log(channel, "Invoking close callback %s", (char *)channel->ch_close_cb.name);
         argv[0].tag = VAR_CHANNEL;
         argv[0].channel = channel;
         call_callback(&channel->ch_close_cb, -1, &returnVar, 1, argv);
         clearVar(&returnVar);
         channel_need_redraw = true;

          // the callback is only called once
          evFreeCallback(&channel->ch_close_cb);

          if (channel_need_redraw) {
             channel_need_redraw = false;
             redraw_after_callback(true, false);
          }

          if (!channel->ch_drop_never) {
             // any remaining messages are useless now
             for (part = PART_SOCK; part < PART_IN; ++part)
                 drop_messages(channel, part);
          } 

          --channel->refCount;
      }
   }

   term_channel_closed(channel);
}

// Close the "in" part channel "channel".
private void
channel_close_in(Channel *channel) {
   ch_close_part(channel, PART_IN);
}

private void
remove_from_writeque(WriteQueue *wq, WriteQueue *entry) {
   ga_clear(&entry->wq_ga);
   wq->next = entry->next;
   if (wq->next == NULL)
      wq->prev = NULL;
   else
      wq->next->prev = NULL;
    eeglFree(entry);
}

// Clear the read buffer on "channel"/"part".
private void
channel_clear_one(Channel *channel, ChannelFdKind part) {
    ChannelFd *fds = &channel->fds[part];
    JsonQ *json_head = &fds->ch_json_head;
    CbNode   *cb_head = &fds->ch_cb_head;

   while (channel_peek(channel, part) != NULL)
      eeglFree(channel_get(channel, part, NULL));

   while (cb_head->cq_next != NULL) {
      CbNode *node = cb_head->cq_next;

      remove_cb_node(cb_head, node);
      evFreeCallback(&node->cq_callback);
      eeglFree(node);
   }

   while (json_head->jq_next != NULL) {
      freeVar(json_head->jq_next->jq_value);
      remove_json_node(json_head, json_head->jq_next);
   }

   evFreeCallback(&fds->ch_callback);
   ga_clear(&fds->ch_block_ids);

   while (fds->ch_writeque.next)
      remove_from_writeque(&fds->ch_writeque, fds->ch_writeque.next);
}

// Clear all the read buffers on "channel".
void
channel_clear(Channel* channel) {
   ch_log(channel, "Clearing channel");
   EE_CLEAR(channel->socketName);
   channel_clear_one(channel, PART_SOCK);
   channel_clear_one(channel, PART_OUT);
   channel_clear_one(channel, PART_ERR);
   channel_clear_one(channel, PART_IN);
   evFreeCallback(&channel->ch_callback);
   evFreeCallback(&channel->ch_close_cb);
}

#if defined(EXITFREE) || defined(PROTO)
void
channel_free_all(void) {
   Channel *channel;

   lo("channel_free_all()");
   FOR_ALL_CHANNELS(channel)
      channel_clear(channel);
}
#endif

// Book size for reading incoming messages.
#define MAXMSGSIZE 4096

// Check if there are remaining data that should be written for "in_part".
private int
is_channel_write_remaining(ChannelFd* in_part) {
   Book* book = in_part->bookref.c;

   if (in_part->ch_writeque.next != NULL)
      return true;
   if (book == NULL)
      return false;
   return in_part->ch_buf_append
       ? (in_part->ch_buf_bot < book->mem.lineCount)
       : (in_part->ch_buf_top <= in_part->ch_buf_bot
             && in_part->ch_buf_top <= book->mem.lineCount);
}

// Add write fds where we are waiting for writing to be possible.
private int
channel_fill_wfds(int maxfd_arg, fd_set *wfds) {
   int      maxfd = maxfd_arg;
   Channel   *ch;

   FOR_ALL_CHANNELS(ch) {
      ChannelFd  *in_part = &ch->fds[PART_IN];

      if (in_part->ch_fd != INVALID_FD && is_channel_write_remaining(in_part)) {
         FD_SET((int)in_part->ch_fd, wfds);
         if ((int)in_part->ch_fd >= maxfd)
            maxfd = (int)in_part->ch_fd + 1;
      }
   }
   return maxfd;
}

typedef enum {
   CW_READY,
   CW_NOT_READY,
   CW_ERROR
} channel_wait_result;

// Check for reading from "fd" with "timeout" msec. Return CW_READY when there is something to read.
// CW_NOT_READY when there is nothing to read. CW_ERROR when there is an error.
private channel_wait_result
channel_wait(Channel* channel, Socket fd, int timeout) {
   if (timeout > 0)
      ch_log(channel, "Waiting for up to %d msec", timeout);

   {
   TimeVal   tval;
   fd_set      rfds;
   fd_set      wfds;
   int      ret;
   int      maxfd;

   tval.tv_sec = timeout / 1000;
   tval.tv_usec = (timeout % 1000) * 1000;
   for (;;) {
      FD_ZERO(&rfds);
      FD_SET((int)fd, &rfds);

      // Write lines to a pipe when a pipe can be written to.  Need to
      // set this every time, some buffers may be done.
      maxfd = (int)fd + 1;
      FD_ZERO(&wfds);
      maxfd = channel_fill_wfds(maxfd, &wfds);

      ret = select(maxfd, &rfds, &wfds, NULL, &tval);
      SOCK_ERRNO;
      if (ret == -1 && errno == EINTR)
         continue;
      if (ret > 0) {
         if (FD_ISSET(fd, &rfds))
            return CW_READY;
         channel_write_any_lines();
         continue;
      }
      break;
   }
   }
   return CW_NOT_READY;
}

private void
ch_close_part_on_error(Channel *channel, ChannelFdKind part, int is_err, char *func) {
   char   msg[] = "%s(): Read %s from fds[%d], closing";

   if (is_err)
      // Do not call emsg(), most likely the other end just exited.
      ch_error(channel, msg, func, "error", part);
   else
      ch_log(channel, msg, func, "EOF", part);


   // When reading is not possible close this part of the channel.  Don't
   // close the channel yet, there may be something to read on another part.
   // When stdout and stderr use the same FD we get the error only on one of
   // them, also close the other.
   if (part == PART_OUT || part == PART_ERR) {
      ChannelFdKind other = part == PART_OUT ? PART_ERR : PART_OUT;

      if (channel->fds[part].ch_fd == channel->fds[other].ch_fd)
          ch_close_part(channel, other);
   }
   ch_close_part(channel, part);
}

private void
channel_close_now(Channel *channel) {
   ch_log(channel, "Closing channel because all readable fds are closed");
   channel_close(channel, true);
}

// Read from channel "channel" for as long as there is something to read. "part" is PART_SOCK, 
// PART_OUT or PART_ERR. The data is put in the read queue.  No callbacks are invoked here.
private void
channel_read(Channel *channel, ChannelFdKind part, char *func) {
   static CS buf = NULL;
   int len = 0;
   int readlen = 0;
   int use_socket = false;

   Socket fd = channel->fds[part].ch_fd;
   if (fd == INVALID_FD) {
      ch_error(channel, "channel_read() called while %s part is closed", chanFdNames[part]);
      return;
   }
   use_socket = fd == channel->fds[PART_SOCK].ch_fd;

   // Allocate a buffer to read into.
   if (!buf) {
      buf = alloc(MAXMSGSIZE);
   }

   //Keep on reading for as long as there is something to read.
   //Use select() or poll() to avoid blocking on a message that is exactly MAXMSGSIZE long.
   for (;;) {
      if (channel_wait(channel, fd, 0) != CW_READY)
         break;
      if (use_socket)
         len = sock_read(fd, (char *)buf, MAXMSGSIZE);
      else
         len = fd_read(fd, (char *)buf, MAXMSGSIZE);
      if (len <= 0)
         break;   // error or nothing more to read

      // Store the read message in the queue.
      saveMsg(channel, part, buf, len, false, S"RECV ");
      readlen += len;
   }

   // Reading a disconnection (readlen == 0), or an error.
   if (readlen <= 0) {
      if (!channel->ch_keep_open)
         ch_close_part_on_error(channel, part, (len < 0), func);
   }
}

// Read from RAW or NL "channel"/"part".  Blocks until there is something to read or the timeout 
// expires. When "raw" is true don't block waiting on a NL. Does not trigger timers or handle 
// messages. Return what was read in allocated memory. NULL in case of error or timeout.
private CS
channel_read_block(Channel *channel, ChannelFdKind part, int timeout, int raw, int *outlen){
   CS buf;
   CS msg;
   ChannelMode   mode = channel->fds[part].ch_mode;
   Socket   fd = channel->fds[part].ch_fd;
   Byte* nl;
   ReadChunk   *node;

   ch_log(channel, "Blocking %s read, timeout: %d msec",
              mode == CH_MODE_RAW ? "RAW" : "NL", timeout);

   while (true) {
      node = channel_peek(channel, part);
      if (node != NULL) {
          if (mode == CH_MODE_RAW || (mode == CH_MODE_NL && channel_first_nl(node) != NULL))
            // got a complete message
            break;
         if (channel_collapse(channel, part, mode == CH_MODE_NL) == OK)
            continue;
         // If not blocking or nothing more is coming then return what we
         // have.
         if (raw || fd == INVALID_FD)
            break;
      }

      // Wait for up to the channel timeout.
      if (fd == INVALID_FD)
         return NULL;
      if (channel_wait(channel, fd, timeout) != CW_READY) {
         ch_log(channel, "Timed out");
         return NULL;
      }
      channel_read(channel, part, "channel_read_block");
   }

    // We have a complete message now.
   if (mode == CH_MODE_RAW || outlen != NULL) {
      msg = channel_get_all(channel, part, outlen);
   } else {
      buf = node->c;
      nl = channel_first_nl(node);

      // Convert ZERO to NL, the internal representation.
      for (CS p = buf; (nl == NULL || p < nl) && p < buf + node->len; ++p) {
         if (*p == ZERO)
            *p = NL;
      } 

      if (!nl) {
         // must be a closed channel with missing NL
         msg = channel_get(channel, part, NULL);
      } ei (nl + 1 == buf + node->len) {
         // get the whole buffer
         msg = channel_get(channel, part, NULL);
         *nl = ZERO;
      } else {
         // Copy the message into allocated memory and remove it from the buffer.
         msg = copySubstr(buf, nl - buf);
         channel_consume(channel, part, (int)(nl - buf) + 1);
      }
   }
   if (ch_log_active())
      ch_log(channel, "Returning %d bytes", (int)STRLEN(msg));
   return msg;
}

private int channel_blocking_wait = 0;

// Return true if in a blocking wait that might trigger callbacks.
int
channel_in_blocking_wait(void) {
   return channel_blocking_wait > 0;
}

// Read one JSON message with ID "id" from "channel"/"part" and store the result in "returnVar".
// When "id" is -1 accept any message;
// Blocks until the message is received or the timeout is reached.
// In corner cases this can be called recursively, that is why ch_block_ids is * a list.
private int
channel_read_json_block(
   Channel* channel,
   ChannelFdKind part,
   int timeout_arg,
   int id,
   Var** returnVar
) {
   int      more;
   Socket   fd;
   int      timeout;
   ChannelFd   *chanpart = &channel->fds[part];
   ChannelMode   mode = channel->fds[part].ch_mode;
   int      retval = FAIL;

   ch_log(channel, "Blocking read JSON for id %d", id);
   ++channel_blocking_wait;

   if (id >= 0)
      channel_add_block_id(chanpart, id);

   for (;;) {
      if (mode == CH_MODE_LSP)
          // In the "lsp" mode, the http header and the json payload may be
          // received in multiple messages. So concatenate all the received
          // messages.
          (void)channel_collapse(channel, part, false);

      more = channel_parse_json(channel, part);

      // search for message "id"
      if (channel_get_json(channel, part, id, true, returnVar) == OK) {
          ch_log(channel, "Received JSON for id %d", id);
          retval = OK;
          break;
      }

      if (!more) {
         void *prev_readahead_ptr = channel_readahead_pointer(channel, part);
         void *readahead_ptr;

         // Handle any other messages in the queue.  If done some more messages may have arrived.
         if (channel_parse_messages())
            continue;

         // channel_parse_messages() may fill the queue with new data to process.  Only loop when 
         // the readahead changed, otherwise we would busy-loop.
         readahead_ptr = channel_readahead_pointer(channel, part);
         if (readahead_ptr != NULL && readahead_ptr != prev_readahead_ptr)
            continue;

         // Wait for up to the timeout. If there was an incomplete message use the deadline for that
         timeout = timeout_arg;
         if (chanpart->ch_wait_len > 0) { {
             TimeVal now_tv;
             gettimeofday(&now_tv, NULL);
             timeout = (chanpart->deadline.tv_sec - now_tv.tv_sec) * 1000
                        + (chanpart->deadline.tv_usec - now_tv.tv_usec) / 1000
                        + 1;
         }
         if (timeout < 0) {
             // Something went wrong, channel_parse_json() didn't discard message.  Cancel waiting.
             chanpart->ch_wait_len = 0;
             timeout = timeout_arg;
         } ei (timeout > timeout_arg)
             timeout = timeout_arg;
         }
         fd = chanpart->ch_fd;
         if (fd == INVALID_FD || channel_wait(channel, fd, timeout) != CW_READY) {
            if (timeout == timeout_arg) {
               if (fd != INVALID_FD)
                  ch_log(channel, "Timed out on id %d", id);
               break;
            }
         } else
            channel_read(channel, part, "channel_read_json_block");
      }
   }
   if (id >= 0)
      channel_remove_block_id(chanpart, id);
   --channel_blocking_wait;

   return retval;
}

// Get the channel from the argument.
// Returns NULL if the handle is invalid.
// When "check_open" is true check that the channel can be used.
// When "reading" is true "check_open" considers typeahead useful.
// "part" is used to check typeahead, when PART_COUNT use the default part.
Channel *
get_channel_arg(Var* tv, int check_open, int reading, ChannelFdKind part) {
   Channel* channel = NULL;
   int has_readahead = false;

   if (tv->tag == VAR_JOB) {
      if (tv->job)
         channel = tv->job->channel;
   } ei (tv->tag == VAR_CHANNEL) {
      channel = tv->channel;
   } else {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(tv));
      return NULL;
   }
   if (channel != NULL && reading)
      has_readahead = 
         channel_has_readahead(channel, part != PART_COUNT ? part : channel_part_read(channel));

   if (check_open && 
         (channel == NULL || (!channel_is_open(channel) && !(reading && has_readahead)))
   ) {
      emsg(_(e_not_an_open_channel));
      return NULL;
   }
   return channel;
}

// Common for ch_read() and ch_readraw().
private void
commonChannelRead(Var* argvars, Var* returnVar, int raw, int blob) {
   Channel   *channel;
   ChannelFdKind   part = PART_COUNT;
   JobOptions   opt;
   int id = -1;
   Var* listtv = NULL;

   // return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CLEAR_POINTER(OUT &opt);
   if (get_job_options(&argvars[1], OUT &opt, JO_TIMEOUT + JO_PART + JO_ID, 0) == FAIL)
      goto theend;

   if (opt.set & JO_PART)
   part = opt.part;
    channel = get_channel_arg(&argvars[0], true, true, part);
   if (channel == NULL)
   goto theend;

   if (part == PART_COUNT)
      part = channel_part_read(channel);
   int mode = channel_get_mode(channel, part);
   int timeout = channel_get_timeout(channel, part);
   if (opt.set & JO_TIMEOUT)
      timeout = opt.jo_timeout;

   if (blob) {
      int       outlen = 0;
      Arr(Byte) channelContent = channel_read_block(channel, part, timeout, true, &outlen);
      if (channelContent != NULL) {
         Blob   *blob = blob_alloc();

         blob->c.len = outlen;
         if (ga_grow(&blob->c, outlen) == FAIL)
            blob_free(blob);
         else {
            memcpy(blob->c.c, channelContent, outlen);
            returnVar_blob_set(returnVar, blob);
         }
         eeglFree(channelContent);
      }
   } ei (raw || mode == CH_MODE_RAW || mode == CH_MODE_NL)
      returnVar->string = channel_read_block(channel, part, timeout, raw, NULL);
   else {
      if (opt.set & JO_ID)
         id = opt.id;
      channel_read_json_block(channel, part, timeout, id, &listtv);
      if (listtv) {
         *returnVar = *listtv;
         eeglFree(listtv);
      } else {
         returnVar->tag = VAR_SPECIAL;
         returnVar->number = VVAL_NONE;
      }
   }

theend:
   free_job_options(&opt);
}


// Set "channel"/"part" to non-blocking. Only works for sockets and pipes.
void
channel_set_nonblock(Channel *channel, ChannelFdKind part) {
   ChannelFd* fds = &channel->fds[part];
   int      fd = fds->ch_fd;

   if (fd == INVALID_FD)
      return;

   (void)fcntl(fd, F_SETFL, O_NONBLOCK);
   fds->ch_nonblocking = true;
}

// Write "buf" (ZERO terminated string) to "channel"/"part".
// When "fun" is not NULL an error message might be given. Return FAIL or OK.
int
channel_send(
   Channel* channel,
   ChannelFdKind part,
   CS buf_arg,
   int     len_arg,
   char* fun
) {
   int res;
   ChannelFd* fds = &channel->fds[part];
   int did_use_queue = false;

   Socket fd = fds->ch_fd;
   if (fd == INVALID_FD) {
      if (!channel->error && fun != NULL) {
         ch_error(channel, "%s(): write while not connected", fun);
         showErrFmtMsg(_(e_str_write_while_not_connected), fun);
      }
      channel->error = true;
      return FAIL;
   }

   if (channel->ch_nonblock && !fds->ch_nonblocking)
   channel_set_nonblock(channel, part);

   if (ch_log_active()) {
      ch_log_literal(S"SEND ", channel, part, OUT (Text){buf_arg, len_arg});
      did_repeated_msg = 0;
   }

   for (;;) {
      WriteQueue    *wq = &fds->ch_writeque;
      CS buf;
      int       len;

      if (wq->next != NULL) {
          // first write what was queued
          buf = wq->next->wq_ga.c;
          len = wq->next->wq_ga.len;
          did_use_queue = true;
      } else {
          if (len_arg == 0)
         // nothing to write, called from channel_select_check()
         return OK;
          buf = buf_arg;
          len = len_arg;
      }

      if (part == PART_SOCK)
         res = sock_write(fd, (char *)buf, len);
      else {
         res = fd_write(fd, (char *)buf, len);
      }
      if (res < 0 && (errno == EWOULDBLOCK
#ifdef EAGAIN
            || errno == EAGAIN
#endif
             ))
          res = 0; // nothing got written

      if (res >= 0 && fds->ch_nonblocking) {
         WriteQueue *entry = wq->next;

         if (did_use_queue)
            ch_log(channel, "Sent %d bytes now", res);
         if (res == len) {
            // Wrote all the buf[len] bytes.
            if (entry) {
               // Remove the entry from the write queue.
               remove_from_writeque(wq, entry);
               continue;
            }
            if (did_use_queue)
               ch_log(channel, "Write queue empty");
         }  else {
            // Wrote only buf[res] bytes, can't write more now.
            if (entry != NULL) {
               if (res > 0) {
                  // Remove the bytes that were written.
                  MEMMOVE(entry->wq_ga.c, (char *)entry->wq_ga.c + res, len - res);
                  entry->wq_ga.len -= res;
               }
               buf = buf_arg;
               len = len_arg;
            } else {
               buf += res;
               len -= res;
            }
            ch_log(channel, "Adding %d bytes to the write queue", len);

            // Append the not written bytes of the argument to the write buffer. Limit entries to 
            // 4000 bytes.
            if (wq->prev && wq->prev->wq_ga.len + len < 4000) {
               WriteQueue *last = wq->prev;

               // append to the last entry
               if (len > 0 && ga_grow(&last->wq_ga, len) == OK) {
                  MEMMOVE((char *)last->wq_ga.c + last->wq_ga.len, buf, len);
                  last->wq_ga.len += len;
               }
            } else {
               WriteQueue* last = ALLOC_ONE(WriteQueue);

               if (last != NULL) {
                  last->prev = wq->prev;
                  last->next = NULL;
                  if (wq->prev == NULL)
                      wq->next = last;
                  else
                      wq->prev->next = last;
                  wq->prev = last;
                  ga_init2(&last->wq_ga, 1, 1000);
                  if (len > 0 && ga_grow(&last->wq_ga, len) == OK) {
                      MEMMOVE(last->wq_ga.c, buf, len);
                      last->wq_ga.len = len;
                  }
               }
            }
         }
      } ei (res != len) {
         if (!channel->error && fun != NULL) {
            ch_error(channel, "%s(): write failed", fun);
            showErrFmtMsg(_(e_str_write_failed), fun);
         }
         channel->error = true;
         return FAIL;
      }

      channel->error = false;
      return OK;
    }
}

// Common for "ch_sendexpr()" and "ch_sendraw()". Return the channel if the caller should read the 
// response. Sets "part_read" to the read fd. Otherwise returns NULL.
private Channel *
send_common(
   Var* argvars,
   CS text,
   int len,
   int id,
   int eval,
   JobOptions    *opt,
   char* fun,
   ChannelFdKind* part_read
) {
   CLEAR_POINTER(opt);
   Channel* channel = get_channel_arg(&argvars[0], true, false, 0);
   if (channel == NULL)
      return NULL;
   ChannelFdKind part_send = channel_part_send(channel);
   *part_read = channel_part_read(channel);

   if (get_job_options(&argvars[2], OUT opt, JO_CALLBACK + JO_TIMEOUT, 0) == FAIL)
      return NULL;

   // Set the callback. An empty callback means no callback and not reading
   // the response. With "ch_evalexpr()" and "ch_evalraw()" a callback is not
   // allowed.
   if (opt->jo_callback.name != NULL && *opt->jo_callback.name != ZERO) {
      if (eval) {
          showErrFmtMsg(_(e_cannot_use_callback_with_str), fun);
          return NULL;
      }
      channel_set_req_callback(channel, *part_read, &opt->jo_callback, id);
   }

   if (channel_send(channel, part_send, text, len, fun) == OK
                  && opt->jo_callback.name == NULL)
      return channel;
   return NULL;
}

// common for "ch_evalexpr()" and "ch_sendexpr()"
private void
ch_expr_common(Arr(Var) argvars, Var* returnVar, int eval) {
   CS text;
   Var* listtv;
   int id;
   ChannelMode   ch_mode;
   JobOptions    opt;
   int      timeout;
   int      callback_present = false;

   // return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   Channel* channel = get_channel_arg(&argvars[0], true, false, 0);
   if (channel == NULL)
      return;
   ChannelFdKind part_send = channel_part_send(channel);

   ch_mode = channel_get_mode(channel, part_send);
   if (ch_mode == CH_MODE_RAW || ch_mode == CH_MODE_NL) {
      emsg(_(e_cannot_use_evalexpr_sendexpr_with_raw_or_nl_channel));
      return;
   }

   if (ch_mode == CH_MODE_LSP) {
      // return an empty dict by default
      allocReturnDict(returnVar);

      if (check_for_dict_arg(argvars, 1) == FAIL)
          return;

      Bag* d = argvars[1].bag;
      DictItem* di = bagFind(d, tConst("id"));
      if (di && di->c.tag != VAR_NUMBER) {
          // only number type is supported for the 'id' item
          showErrFmtMsg(_(e_invalid_value_for_argument_str), "id");
          return;
      }

      if (argvars[2].tag == VAR_BAG && bagHasKey(argvars[2].bag, tConst("callback")))
         callback_present = true;

      if (eval || callback_present) {
         // When evaluating an expression or sending an expression with a
         // callback, always assign a generated ID
         id = ++channel->lastMsgId;
         if (di == NULL)
            bagAddNumber(d, (CS)"id", id);
         else
            di->c.number = id;
      } else {
          // When sending an expression, if the message has an 'id' item,
          // then use it.
          id = 0;
          if (di != NULL)
         id = di->c.number;
      }
      if (!bagHasKey(d, tConst("jsonrpc")))
          bagAddString(d, (CS)"jsonrpc", (CS)"2.0");
      text = json_encode_lsp_msg(&argvars[1]);
   } else {
      id = ++channel->lastMsgId;
      text = json_encode_nr_expr(id, &argvars[1], JSON_NL);
   }
   if (!text)
      return;

   ChannelFdKind part_read;
   channel = send_common(argvars, text, (int)STRLEN(text), id, eval, &opt,
             eval ? "ch_evalexpr" : "ch_sendexpr", OUT &part_read);
   eeglFree(text);
   if (channel && eval) {
      if (opt.set & JO_TIMEOUT)
          timeout = opt.jo_timeout;
      else
          timeout = channel_get_timeout(channel, part_read);
      if (channel_read_json_block(channel, part_read, timeout, id, &listtv) == OK) {
         if (ch_mode == CH_MODE_LSP) {
            *returnVar = *listtv;
            // Change the type to avoid the value being freed.
            listtv->tag = VAR_NUMBER;
            freeVar(listtv);
         } else {
            List *list = listtv->list;

            // Move the item from the list and then change the type to
            // avoid the value being freed.
            *returnVar = list->lv_u.mat.last->c;
            list->lv_u.mat.last->c.tag = VAR_NUMBER;
            freeVar(listtv);
         }
      }
    }
    free_job_options(&opt);
   if (ch_mode == CH_MODE_LSP && !eval && callback_present) {
      // if ch_sendexpr() is used to send a LSP message and a callback
      // function is specified, then return the generated identifier for the
      // message.  The user can use this to cancel the request (if needed).
      if (returnVar->bag)
         bagAddNumber(returnVar->bag, S"id", id);
   }
}

// common for "ch_evalraw()" and "ch_sendraw()"
private void
ch_raw_common(Var* argvars, OUT Var* returnVar, int eval) {
   Byte buf[NUMBUFLEN];
   int len;
   Channel* channel;
   ChannelFdKind part_read;
   JobOptions opt;
   int timeout;

   // return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CS text;
   if (argvars[1].tag == VAR_BLOB) {
      text = argvars[1].blob->c.c;
      len = argvars[1].blob->c.len;
   } else {
      text = tv_get_string_buf(&argvars[1], buf);
      len = (int)STRLEN(text);
   }
   channel = send_common(argvars, text, len, 0, eval, &opt,
               eval ? "ch_evalraw" : "ch_sendraw", &part_read);
   if (channel != NULL && eval) {
      if (opt.set & JO_TIMEOUT)
          timeout = opt.jo_timeout;
      else
          timeout = channel_get_timeout(channel, part_read);
      returnVar->string = channel_read_block(channel, part_read, timeout, true, NULL);
   }
   free_job_options(&opt);
}

#define KEEP_OPEN_TIME 20  // msec

// The "fd_set" type is hidden to avoid problems with the function proto.
int
channel_select_setup(
   int maxfd_in,
   void *rfds_in,
   void *wfds_in,
   TimeVal *tv,
   TimeVal **tvp
) {
   int      maxfd = maxfd_in;
   Channel   *channel;
   fd_set   *rfds = rfds_in;
   fd_set   *wfds = wfds_in;
   ChannelFdKind   part;

   FOR_ALL_CHANNELS(channel) {
      for (part = PART_SOCK; part < PART_IN; ++part) {
         Socket fd = channel->fds[part].ch_fd;

         if (fd != INVALID_FD) {
            if (channel->ch_keep_open) {
               // For unknown reason select() returns immediately for a keep-open channel. 
               // Instead of adding it to the rfds add a short timeout and check, like polling.
               if (*tvp == NULL || tv->tv_sec > 0 || tv->tv_usec > KEEP_OPEN_TIME * 1000) {
                  *tvp = tv;
                  tv->tv_sec = 0;
                  tv->tv_usec = KEEP_OPEN_TIME * 1000;
               }
            } else {
               FD_SET((int)fd, rfds);
               if (maxfd < (int)fd)
                  maxfd = (int)fd;
            }
         }
      }
   }

   maxfd = channel_fill_wfds(maxfd, wfds);

   return maxfd;
}

// The "fd_set" type is hidden to avoid problems with the function proto.
int
channel_select_check(int ret_in, void *rfds_in, void *wfds_in) {
   int      ret = ret_in;
   Channel   *channel;
   fd_set   *rfds = rfds_in;
   fd_set   *wfds = wfds_in;
   ChannelFdKind   part;
   ChannelFd   *in_part;

   FOR_ALL_CHANNELS(channel) {
      for (part = PART_SOCK; part < PART_IN; ++part) {
         Socket fd = channel->fds[part].ch_fd;

         if (ret > 0 && fd != INVALID_FD && FD_ISSET(fd, rfds)) {
            channel_read(channel, part, "channel_select_check");
            FD_CLR(fd, rfds);
            --ret;
         } ei (fd != INVALID_FD && channel->ch_keep_open) {
            // polling a keep-open channel
            channel_read(channel, part, "channel_select_check_keep_open");
         }
      }

      in_part = &channel->fds[PART_IN];
      if (ret > 0 && in_part->ch_fd != INVALID_FD && FD_ISSET(in_part->ch_fd, wfds)) {
         // Clear the flag first, ch_fd may change in channel_write_input().
         FD_CLR(in_part->ch_fd, wfds);
         channel_write_input(channel);
         --ret;
      }
   }

   return ret;
}

// Execute queued up commands.
// Invoked from the main loop when it's safe to execute received commands,
// and during a blocking wait for ch_evalexpr(). Return true when something was done.
int
channel_parse_messages(void) {
   Channel   *channel = first_channel;
   int      ret = false;
   int      r;
   ChannelFdKind   part = PART_SOCK;
   static int   recursive = 0;
   Elapsed   start_tv;

   // The code below may invoke callbacks, which might call us back.
   // In a recursive call channels will not be closed.
   ++recursive;
   ++safe_to_invoke_callback;

   ELAPSED_INIT(start_tv);

   // Only do this message when another message was given, otherwise we get lots of them.
   if ((did_repeated_msg & REPEATED_MSG_LOOKING) == 0) {
      lo("looking for messages on channels");
      // now we should also give the message for SafeState
      did_repeated_msg = REPEATED_MSG_LOOKING;
   }
   while (channel != NULL) {
      if (recursive == 1) {
          if (channel_can_close(channel)) {
         channel->ch_to_be_closed = (1U << PART_COUNT);
         channel_close_now(channel);
         // channel may have been freed, start over
         channel = first_channel;
         continue;
          }
         if (channel->ch_to_be_freed || channel->isBeingKilled) {
            channel_free_contents(channel);
            if (channel->job != NULL)
                channel->job->channel = NULL;

            // free the channel and then start over
            channel_free_channel(channel);
            channel = first_channel;
            continue;
         }
         if (channel->refCount == 0 && !channel_still_useful(channel)) {
            // channel is no longer useful, free it
            channel_free(channel);
            channel = first_channel;
            part = PART_SOCK;
            continue;
         }
      }

      if (channel->fds[part].ch_fd != INVALID_FD || channel_has_readahead(channel, part)) {
         //Increase the refcount, in case the handler causes the channel to be unreferenced or 
         //closed
         ++channel->refCount;
         r = may_invoke_callback(channel, part);
         if (r == OK)
            ret = true;
         if (channel_unref(channel) || (r == OK
            // Limit the time we loop here to 100 msec, otherwise Eegl becomes unresponsive when 
            // the callback takes more than a bit of time.
            && ELAPSED_FUNC(start_tv) < 100L
            )
         )
            // channel was freed or something was done, start over
            channel = first_channel;
         part = PART_SOCK;
         continue;
      }
      if (part < PART_ERR)
         ++part;
      else {
         channel = channel->next;
         part = PART_SOCK;
      }
   }

   if (channel_need_redraw) {
      channel_need_redraw = false;
      redraw_after_callback(true, false);
   }

   --safe_to_invoke_callback;
   --recursive;

   return ret;
}

// Return true if any channel has readahead.  That means we should not block on waiting for input.
int
channel_any_readahead(void) {
   Channel* channel = first_channel;
   ChannelFdKind   part = PART_SOCK;

   while (channel) {
      if (channel_has_readahead(channel, part))
          return true;
      if (part < PART_ERR)
          ++part;
      else {
          channel = channel->next;
          part = PART_SOCK;
      }
   }
   return false;
}

// Mark references to lists used in channels.
int
set_ref_in_channel(int copyID) {
   int abort = false;
   Channel* channel;
   Var tv;

   for (channel = first_channel; !abort && channel; channel = channel->next) {
      if (channel_still_useful(channel)) {
         tv.tag = VAR_CHANNEL;
         tv.channel = channel;
         abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
      }
   } 
   return abort;
}

// Return the "part" to write to for "channel".
private ChannelFdKind
channel_part_send(Channel* channel) {
   if (channel->fds[PART_SOCK].ch_fd == INVALID_FD)
      return PART_IN;
   return PART_SOCK;
}

// Return the default "part" to read from for "channel".
private ChannelFdKind
channel_part_read(Channel* channel) {
   if (channel->fds[PART_SOCK].ch_fd == INVALID_FD)
      return PART_OUT;
   return PART_SOCK;
}

// Return the mode of "channel"/"part" If "channel" is invalid returns CH_MODE_JSON.
private ChannelMode
channel_get_mode(Channel* channel, ChannelFdKind part) {
   if (!channel)
      return CH_MODE_JSON;
   return channel->fds[part].ch_mode;
}

// The timeout of "channel"/"part"
private int
channel_get_timeout(Channel *channel, ChannelFdKind part) {
   return channel->fds[part].ch_timeout;
}

void
f_ch_canread(Var* argvars, Var* returnVar) {
   returnVar->number = 0;

   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (channel != NULL)
      returnVar->number = channel_has_readahead(channel, PART_SOCK)
                || channel_has_readahead(channel, PART_OUT)
                || channel_has_readahead(channel, PART_ERR);
}

void
f_ch_close(Arr(Var) argvars, Var* returnVar UNUSED) {
   Channel *channel;

   channel = get_channel_arg(&argvars[0], true, false, 0);
   if (channel != NULL) {
      channel_close(channel, false);
      channel_clear(channel);
   }
}

void
f_ch_close_in(Arr(Var) argvars, Var* returnVar UNUSED) {
   Channel *channel;

   channel = get_channel_arg(&argvars[0], true, false, 0);
   if (channel != NULL)
      channel_close_in(channel);
}

void
f_ch_getbufnr(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = -1;

   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (!channel)
      return;

   Byte* what = tv_get_string(&argvars[1]);
   int part;
   if (STRCMP(what, "err") == 0)
      part = PART_ERR;
   ei (STRCMP(what, "out") == 0)
      part = PART_OUT;
   ei (STRCMP(what, "in") == 0)
      part = PART_IN;
   else
      part = PART_SOCK;
   if (channel->fds[part].bookref.c != NULL)
   returnVar->number =
       channel->fds[part].bookref.c->fiNum;
}

void
f_ch_getjob(Arr(Var) argvars, Var* returnVar) {
   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (channel)
      return;

   returnVar->tag = VAR_JOB;
   returnVar->job = channel->job;
   if (channel->job != NULL)
      incRefCount(channel->job);
}

void
f_ch_info(Arr(Var) argvars, Var* returnVar UNUSED) {
   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (channel) {
      allocReturnDict(returnVar);
      channelInfoIntoDict(channel, OUT returnVar->bag);
   } 
}

void
f_ch_open(Arr(Var) argvars, Var* returnVar) {
   returnVar->tag = VAR_CHANNEL;
   returnVar->channel = channel_open_func(argvars);
}

void
f_ch_read(Arr(Var) argvars, Var* returnVar) {
   commonChannelRead(argvars, returnVar, false, false);
}

void
f_ch_readblob(Arr(Var) argvars, Var* returnVar) {
   commonChannelRead(argvars, returnVar, true, true);
}

void
f_ch_readraw(Arr(Var) argvars, Var* returnVar) {
   commonChannelRead(argvars, returnVar, true, false);
}

void
f_ch_evalexpr(Arr(Var) argvars, Var* returnVar) {
   ch_expr_common(argvars, returnVar, true);
}

void
f_ch_sendexpr(Arr(Var) argvars, Var* returnVar) {
   ch_expr_common(argvars, returnVar, false);
}

void
f_ch_evalraw(Arr(Var) argvars, Var* returnVar) {
   ch_raw_common(argvars, returnVar, true);
}

void
f_ch_sendraw(Arr(Var) argvars, Var* returnVar) {
   ch_raw_common(argvars, returnVar, false);
}

void
f_ch_setoptions(Arr(Var) argvars, Var* returnVar UNUSED) {
   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);
   if (!channel)
      return;
      
   JobOptions opt;
   CLEAR_POINTER(&opt);
   if (get_job_options(&argvars[1], OUT &opt, JO_CB_ALL + JO_TIMEOUT_ALL + JO_MODE_ALL, 0) == OK)
      channel_set_options(channel, &opt);
   free_job_options(&opt);
}

void
f_ch_status(Arr(Var) argvars, Var* returnVar) {
   JobOptions   opt;
   int part = -1;

   // return an empty string by default
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   Channel* channel = get_channel_arg(&argvars[0], false, false, 0);

   if (argvars[1].tag != VAR_UNKNOWN) {
      CLEAR_POINTER(&opt);
      if (get_job_options(&argvars[1], OUT &opt, JO_PART, 0) == OK && (opt.set & JO_PART))
         part = opt.part;
   }

   returnVar->string = copyStr(channel_status(channel, part));
}

// Get a string with information about the channel in "varp" into "builder".
// "builder" must be at least NUMBUFLEN long.
void
channel_to_string_buf(OUT CS builder, Var* varp) {
   Channel *channel = varp->channel;
   CS status = channel_status(channel, -1);

   if (channel)
      eeSnprintf(builder, NUMBUFLEN, "channel %d %s", channel->id, status);
   else
      eeSnprintf(builder, NUMBUFLEN, "channel %s", status);
}

// Build "argv[argc]" from the list "l".
// "argv[argc]" is set to NULL; Return FAIL when out of memory.
private int
build_argv_from_list(List *l, Byte*** argv, int *argc) {
   // Pass argv[] to chCallShell().
   *argv = ALLOC_MULT(CS, l->len + 1);
   *argc = 0;
   ListItem* li;
   FOR_ALL_LIST_ITEMS(l, li) {
      CS s = convertVarToStringSingleUse(&li->c);
      if (!s) {
         for (int i = 0; i < *argc; ++i) {
            EE_CLEAR((*argv)[i]);
         } 
         (*argv)[0] = NULL;
         return FAIL;
      }
      (*argv)[*argc] = copyStr(s);
      *argc += 1;
   }
   (*argv)[*argc] = NULL;
   return OK;
}


//}}}
//{{{channels, shell jobs and signals

private char* signal_stack;
private void sigcont_handler SIGPROTOARG;
private void deathtrap SIGPROTOARG;


#if defined(SIGUSR1)
static void catch_sigusr1 SIGPROTOARG;
#endif

#if defined(SIGPWR)
static void catch_sigpwr SIGPROTOARG;
#endif

static struct signalinfo {
   int       sig;   // Signal number, eg. SIGSEGV etc
   char    *name;   // Signal name (not Byte!).
   char    deadly;   // Catch as a deadly signal?
} signalInfos[] = {
    {SIGHUP,       "HUP",   true},
    {SIGQUIT,       "QUIT",   true},
#ifdef SIGILL
    {SIGILL,       "ILL",   true},
#endif
#ifdef SIGTRAP
    {SIGTRAP,       "TRAP",   true},
#endif
#ifdef SIGABRT
    {SIGABRT,       "ABRT",   true},
#endif
#ifdef SIGEMT
    {SIGEMT,       "EMT",   true},
#endif
#ifdef SIGFPE
    {SIGFPE,       "FPE",   true},
#endif
#ifdef SIGBUS
    {SIGBUS,       "BUS",   true},
#endif
#if defined(SIGSEGV)
    {SIGSEGV,       "SEGV",   true},
#endif
#ifdef SIGSYS
    {SIGSYS,       "SYS",   true},
#endif
#ifdef SIGALRM
    {SIGALRM,       "ALRM",   false},   // Perl's alarm() can trigger it
#endif
    {SIGTERM,       "TERM",   true},
#if defined(SIGVTALRM)
    {SIGVTALRM,       "VTALRM",   true},
#endif
#if defined(SIGPROF) && !defined(WE_ARE_PROFILING)
    // With profiling this makes Eegl exit. WE_ARE_PROFILING is defined in Makefile.
    {SIGPROF,       "PROF",   true},
#endif
#ifdef SIGXCPU
    {SIGXCPU,       "XCPU",   true},
#endif
#ifdef SIGXFSZ
    {SIGXFSZ,       "XFSZ",   true},
#endif
#ifdef SIGUSR1
    {SIGUSR1,       "USR1",   false},
#endif
#if defined(SIGUSR2)
    // Used for sysmouse handling
    {SIGUSR2,       "USR2",   true},
#endif
#ifdef SIGPIPE
    {SIGPIPE,       "PIPE",   false},
#endif
    {-1,       "Unknown!", false}
};


//{{{signal stack

//Support for using the signal stack.
//This helps when we run out of stack space, which causes a SIGSEGV.  The
//signal handler then must run on another stack, since the normal stack is completely full.

private stack_t sigstk;         // for sigaltstack()

#if (defined(HAVE_SETJMP_H) && ((defined(FEAT_X11)))) || defined(PROTO)
# define USING_SETJMP 1

// argument to SETJMP()
static JMP_BUF lc_jump_env;

// Caught signal number, 0 when no signal was caught. Volatile because it is used in signal handlers
static volatile SigAtomic lc_signal;

// true when lc_jump_env is valid. Volatile because it is used in signal handler deathtrap().
static volatile SigAtomic lc_active = false;

//A simplistic version of setjmp() that only allows one level of using.
//Used to protect areas where we could crash.
//Don't call twice before calling mch_endjmp()!.
//
//Usage:
//  mch_startjmp();
//  if (SETJMP(lc_jump_env) != 0) {
//     mch_didjmp();
//     emsg("crash!");
//  } else {
//     do_the_work;
//     mch_endjmp();
//  }
//Note: Can't move SETJMP() here, because a function calling setjmp() must
//not return before the saved environment is used.
//Returns OK for normal return, FAIL when the protected code caused a
//problem and LONGJMP() was used.

//private void
//mch_startjmp(void) {
//   lc_signal = 0;
//   lc_active = true;
//}
//
//private void
//mch_endjmp(void) {
//   lc_active = false;
//}

#endif

//Get a size of signal stack. Preference (if available): sysconf > SIGSTKSZ > guessed size
private long int get_signal_stack_size(void) {
   long int size = -1;

   // return size only if sysconf doesn't return an error
   if ((size = sysconf(_SC_SIGSTKSZ)) > -1)
      return size;

   // if sysconf() isn't available or gives error, return SIGSTKSZ if defined
   return SIGSTKSZ;
}


//}}}

void
setIgnoreSigTstp(int newVal) {
   ignore_sigtstp = newVal;
}

void
set_signals(void) {
   //WINDOW CHANGE signal is handled with sig_winch().
   mch_signal(SIGWINCH, sig_winch);

   // See mch_init() for the conditions under which we ignore SIGTSTP.
   // In the GUI default TSTP processing is OK.
   // Checking both gui.in_use and gui.starting because gui.in_use is not set
   // at this point (set after menus are displayed), but gui.starting is set.
   mch_signal(SIGTSTP, ignore_sigtstp ? SIG_IGN : sig_tstp);
   mch_signal(SIGCONT, sigcont_handler);
#ifdef SIGPIPE
   //We want to ignore breaking of PIPEs.
   mch_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGINT
   catch_int_signal();
#endif

#ifdef SIGUSR1
   //Call user's handler on SIGUSR1
   mch_signal(SIGUSR1, catch_sigusr1);
#endif

   //Ignore alarm signals (Perl's alarm() generates it).
#ifdef SIGALRM
   mch_signal(SIGALRM, SIG_IGN);
#endif

#ifdef SIGPWR
   //Catch SIGPWR (power failure?) to preserve the swap files, so that no work will be lost.
   mch_signal(SIGPWR, catch_sigpwr);
#endif

   //Arrange for other signals to gracefully shutdown Eegl.
   catch_signals(deathtrap, SIG_ERR);
}

private void
init_signal_stack(void) {
   if (signal_stack == NULL)
      return;

# ifdef HAVE_SS_BASE
    sigstk.ss_base = signal_stack;
# else
    sigstk.ss_sp = signal_stack;
# endif
    sigstk.ss_size = get_signal_stack_size();
    sigstk.ss_flags = 0;
    (void)sigaltstack(&sigstk, NULL);
}

private CS
get_signal_name(int sig) {
   Byte   numbuf[NUMBUFLEN];

   if (sig == SIGKILL)
      return copySubstr((CS)"kill", STRLEN_LITERAL("kill"));

   int i;
   for (i = 0; signalInfos[i].sig != -1; i++) {
      if (sig == signalInfos[i].sig)
          return strlow_save((CS)signalInfos[i].name);
   } 

   i = eeSnprintf(numbuf, NUMBUFLEN, "%d", sig);
   return copySubstr(numbuf, i);
}

private void
block_signals(sigset_t *set) {
   sigset_t   newset;
   sigemptyset(&newset);
   for (int i = 0; signalInfos[i].sig != -1; i++)
      sigaddset(&newset, signalInfos[i].sig);

   // SIGCONT isn't in the list, because its default action is ignore
   sigaddset(&newset, SIGCONT);
   sigprocmask(SIG_BLOCK, &newset, set);
}

private void
unblock_signals(sigset_t *set) {
   sigprocmask(SIG_SETMASK, set, NULL);
}

// Send SIGINT to a child process if "c" is an interrupt character.
private void
may_send_sigint(Unt c, ProId pid UNUSED, ProId wpid) {
   if (c == Ctrl_C || c == extraInterruptCharG) {
      kill(-pid, SIGINT);
   if (wpid > 0)
      kill(wpid, SIGINT);
   }
}

// Wait for process "child" to end. Return "child" if it exited properly, <= 0 on error.
private ProId
wait4pid(ProId child, waitstatus *status) {
   ProId wait_pid = 0;
   long delay_msec = 1;

   while (wait_pid != child) {
      // When compiled with Python threads are probably used, in which case wait() sometimes hangs
      // for no obvious reason.  Use waitpid() instead and loop (like the GUI). Also needed for 
      // other interfaces, they might call system().
      wait_pid = waitpid(child, status, WNOHANG);
      if (wait_pid == 0) {
         // Wait for 1 to 10 msec before trying again.
         mch_delay(delay_msec, MCH_DELAY_IGNOREINPUT | MCH_DELAY_SETTMODE);
         if (++delay_msec > 10)
            delay_msec = 10;
         continue;
      }
      if (wait_pid <= 0
# ifdef ECHILD
         && errno == ECHILD
# endif
      )
         break;
    }
    return wait_pid;
}

// Append the text in "gap" below the cursor line and clear "gap".
private void
append_ga_line(ArrayList* gap) {
   // Remove trailing CR.
   if (gap->len > 0
          && !curBook->o.binary
          && ((CS)gap->c)[gap->len - 1] == ENTER)
      --gap->len;
   _bp(true); 
   ga_append(gap, ZERO);
   ml_append(curPor->cursor.lnum++, gap->c, 0, false);
   gap->len = 0;
}

private Multistring
shellArgsNew() {
}

//Don't use system(), use fork()/exec().
private PolyWithStatus
callShellImpl(CS cmd, Unt options){   // SHELL_*, see eegl.h
   TermInputMode tmode = cur_tmode;
   ProId  pid;
   ProId  wpid = 0;
   ProId  wait_pid = 0;
   int status = -1;
   Byte* tofree2 = NULL;
   int i;
   int pty_master_fd = -1; // for pty's
   int fd_toshell[2];      // for pipes
   int fd_fromshell[2];
   int pipe_error = false;
   int did_termSetMode = false;   // termSetMode(TMODE_RAW) called
   Polystring shellResponse = {};
   PolyWithStatus retVal = { .status = 0, .c = shellResponse };

   out_flush();
   if ((options & SHELL_COOKED) != 0)
      termSetMode(TMODE_COOK);      // set to normal mode
   if (tmode == TMODE_RAW)
      // The shell may have messed with the mode, always set it later.
      cur_tmode = TMODE_UNKNOWN;
      
   ShellArgs shellArgs = shellArgsNew();
   Arr(CS) argv = unix_build_argv(cmd, OUT &shellArgs);

   if ((options & (SHELL_READ|SHELL_WRITE)) != 0) {
      pipe_error = (pipe(fd_toshell) < 0);
      if (!pipe_error) {            // pipe create OK
         pipe_error = (pipe(fd_fromshell) < 0);
         if (pipe_error) {            // pipe create failed
            close(fd_toshell[0]);
            close(fd_toshell[1]);
         }
      }
      if (pipe_error) {
         msg_puts(_("\nCannot create pipes\n"));
         out_flush();
      }
   }

   if (!pipe_error) {        // pty or pipe opened or not used
      SIGSET_DECL(curset)
      BLOCK_SIGNALS(&curset);
      pid = fork();   // maybe we should use vfork()
      if (pid == -1) {
         UNBLOCK_SIGNALS(&curset);

         msg_puts(_("\nCannot fork\n"));
         if ((options & (SHELL_READ|SHELL_WRITE)) != 0) {
            close(fd_toshell[0]);
            close(fd_toshell[1]);
            close(fd_fromshell[0]);
            close(fd_fromshell[1]);
         }
      } ei (pid == 0) {  // child
         reset_signals();      // handle signals normally
         UNBLOCK_SIGNALS(&curset);

         if (ch_log_active()) {
            lo("closing channel log in the child process");
            ch_logfile(S"", S"");
         }

         if (!(options & SHELL_SHOW_MSG) || (options & SHELL_EXPAND)) {
            //Don't want to show any message from the shell. Can't just close stdout and stderr 
            //though, because some systems will break if you try to write to them after that, so 
            //we must use dup() to replace them with something else -- webb
            //Connect stdin to /dev/null too, so ":n `cat`" doesn't hang while waiting for input.
            int fd = open("/dev/null", O_RDWR | O_EXTRA, 0);
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);

            //If any of these open()'s and dup()'s fail, we just continue anyway. It's not fatal, 
            //and on most systems it will make no difference at all. On a few it will cause the 
            //execvp() to exit with a non-zero status even when the completion could be done, 
            //which is nothing too serious. If the open() or dup() failed we'd just do the same 
            //thing ourselves anyway -- webb
            if (fd >= 0) {
               (void)dup(fd); // To replace stdin  (fd 0)
               (void)dup(fd); // To replace stdout (fd 1)
               (void)dup(fd); // To replace stderr (fd 2)

               //Don't need this now that we've duplicated it
               close(fd);
            }
         } ei ((options & (SHELL_READ|SHELL_WRITE)) != 0) {
            set_default_child_environment(false);

            //stderr is only redirected when using the GUI, so that a program like gpg can still 
            //access the terminal to get a passphrase using stderr.
            //set up stdin for the child
            close(fd_toshell[1]);
            close(0);
            (void)dup(fd_toshell[0]);
            close(fd_toshell[0]);

            // set up stdout for the child
            close(fd_fromshell[0]);
            close(1);
            (void)dup(fd_fromshell[1]);
            close(fd_fromshell[1]);
         }

         //There is no type cast for the argv, because the type may be different on different 
         //machines. This may cause a warning message with strict compilers, don't worry about it.
         //Call _exit() instead of exit() to avoid closing the connection
         //to the Wayland server (esp. with GTK, which uses atexit()).
         execvp((char*)argv[0], (char**)argv);
         _exit(EXEC_FAILED);       // exec failed, return failure code
      } else {        // parent
         //While child is running, ignore terminating signals.
         //Do catch CTRL-C, so that "gotInterruptG" is set.
         catch_signals(SIG_IGN, SIG_ERR);
         catch_int_signal();
         UNBLOCK_SIGNALS(&curset);
         ++dontCheckJobEndedS;
         //For the GUI we redirect stdin, stdout and stderr to our window.
         //This is also used to pipe stdin/stdout to/from the external command.
         if ((options & (SHELL_READ|SHELL_WRITE))) {
# define BUFLEN 100      // length for buffer, pseudo tty limit is 128
            Byte buffer[BUFLEN + 1];
            int buffer_off = 0;   // valid bytes in buffer[]
            Byte ta_buf[BUFLEN + 1];   // TypeAHead
            int ta_len = 0;      // valid bytes in ta_buf[]
            int len;
            int noread_cnt;
            Elapsed start_tv;

            close(fd_toshell[0]);
            close(fd_fromshell[1]);
            int toshell_fd = fd_toshell[1];
            int fromshell_fd = fd_fromshell[0];

            //Write to the child if there are typed characters. Read from the child if there are 
            //characters available.
            //  Repeat the reading a few times if more characters are
            //  available. Need to check for typed keys now and then, but
            //  not too often (delays when no chars are available).
            //This loop is quit if no characters can be read from the pty (waitForChar detected 
            //special condition), or there are no characters available and the child has exited.
            //Only check if the child has exited when there is no more output. The child may exit 
            //before all the output has been printed.
            //
            //Currently this busy loops! This can probably dead-lock when the write blocks!
            Boole p_more_save = p_more;
            p_more = false;
            Unt modeSaved = stateG;
            stateG = MODE_EXTERNCMD;   // don't redraw at window resize

            if ((options & SHELL_WRITE) != 0 && toshell_fd >= 0) {
               // Fork a process that will write the lines to the external program.
               if ((wpid = fork()) == -1) {
                  msg_puts(_("\nCannot fork\n"));
               } ei (wpid == 0) { // child
                  LineNr lnum = curBook->opStart.lnum;
                  Unt written = 0;
                  Byte* lp = ml_get(lnum);
                  Unt lplen = (Unt)ml_get_len(lnum);

                  close(fromshell_fd);
                  for (;;) {
                     if (lplen == 0)
                        len = 0;
                     ei (lp[written] == NL)
                        // NL -> ZERO translation
                        len = write(toshell_fd, "", (Unt)1);
                     else {
                        CS s = firstOccurrence(lp + written, NL);
                        len = write(
                           toshell_fd, 
                           (char *)lp + written,
                           s ? (Unt)(s - (lp + written)) : lplen - written 
                        );
                     }
                     if (len == (int)(lplen - written)) {
                        // Finished a line, add a NL, unless this line should not have one.
                        if (lnum != curBook->opEnd.lnum
                              || (lnum != curBook->noEolLnum && (lnum != curBook->mem.lineCount))
                        )
                            (void)write(toshell_fd, "\n", (Unt)1);
                        ++lnum;
                        if (lnum > curBook->opEnd.lnum) {
                           // finished all the lines, close pipe
                           close(toshell_fd);
                           break;
                        }
                        lp = ml_get(lnum);
                        lplen = ml_get_len(lnum);
                        written = 0;
                     } ei (len > 0)
                        written += (Unt)len;
                  }
                  _exit(0);
               } else { // parent
                  close(toshell_fd);
                  toshell_fd = -1;
               }
            }

            ArrayList shellResponse;
            if ((options & SHELL_READ) != 0)
               ga_init2(&shellResponse, 1, BUFLEN);

            noread_cnt = 0;
            ELAPSED_INIT(start_tv);
            for (;;) {
               //Check if keys have been typed, write them to the child if there are any.
               //Don't do this if we are expanding wild cards (would eat typeahead).
               //Don't do this when filtering and terminal is in cooked mode, the shell command 
               //will handle the I/O.  Avoids that a typed password is echoed for ssh or gpg 
               //command. Don't get characters when the child has already finished (wait_pid == 0).
               //Don't read characters unless we didn't get output for a
               //while (noread_cnt > 4), avoids that ":r !ls" eats typeahead.
               len = 0;
               if (!(options & SHELL_EXPAND)
                   && ((options & (SHELL_READ|SHELL_WRITE|SHELL_COOKED))
                        != (SHELL_READ|SHELL_WRITE|SHELL_COOKED))
                   && wait_pid == 0
                   && (ta_len > 0 || noread_cnt > 4)
               ){
                  if (ta_len == 0) {
                     // Get extra characters when we don't have any. Reset the counter and timer.
                     noread_cnt = 0;
                     ELAPSED_INIT(start_tv);
                     len = ui_inchar(ta_buf, BUFLEN, 10L, 0);
                  }
                  if (ta_len > 0 || len > 0) {
                    //For pipes:
                    //Check for CTRL-C: send interrupt signal to child.
                    //Check for CTRL-D: EOF, close pipe to child.
                    if (len == 1 && (pty_master_fd < 0 || cmd != NULL)) {
                        //Send SIGINT to the child's group or all processes in our group.
                        may_send_sigint(ta_buf[ta_len], pid, wpid);

                        if (pty_master_fd < 0 && toshell_fd >= 0 && ta_buf[ta_len] == Ctrl_D) {
                           close(toshell_fd);
                           toshell_fd = -1;
                        }
                     }

                     // Remove Eegl-specific codes from the input.
                     len = term_replace_keycodes(ta_buf, ta_len, len);

                     //For pipes: echo the typed characters. For a pty this does not seem to work.
                     if (pty_master_fd < 0) {
                        for (i = ta_len; i < ta_len + len; ++i) {
                           if (ta_buf[i] == '\n' || ta_buf[i] == '\b')
                              msg_putchar(ta_buf[i]);
                           else
                              msgTranslatedSlice((Text){ta_buf + i, 1});
                        }
                        windgoto(msgRowG, msgColG);
                        out_flush();
                     }

                     ta_len += len;

                     //Write the characters to the child, unless EOF has been typed for pipes. Write 
                     //one character at a time, to avoid losing too much typeahead.
                     //When writing buffer lines, drop the typed characters (only check for CTRL-C).
                     if ((options & SHELL_WRITE) != 0)
                        ta_len = 0;
                     ei (toshell_fd >= 0) {
                        len = write(toshell_fd, (char *)ta_buf, (Unt)1);
                        if (len > 0) {
                           ta_len -= len;
                           MEMMOVE(ta_buf, ta_buf + len, ta_len);
                        }
                     }
                  }
               }

               if (gotInterruptG) {
                  // CTRL-C sends a signal to the child, we ignore it ourselves
                  kill(-pid, SIGINT);
                  if (wpid > 0)
                     kill(wpid, SIGINT);
                  gotInterruptG = false;
               }

               //Check if the child has any characters to be printed. Read them and write them to 
               //our window. Repeat this as long as there is something to do, avoid the 10ms wait
               //for mch_inchar(), or sending typeahead characters to the external process.
               //TODO: This should handle escape sequences, compatible to some terminal (vt52?).
               ++noread_cnt;
               while (realWaitForChar(fromshell_fd, 10L, NULL, NULL)) {
                  len = fiReadEintr(
                        fromshell_fd, OUT buffer + buffer_off, (Unt)(BUFLEN - buffer_off)
                  );
                  if (len <= 0)          // end of file or error
                     goto finished;

                  noread_cnt = 0;
                  if ((options & SHELL_READ) != 0) {
                     // Do ZERO -> NL translation, append NL separated lines to the current buffer
                     for (i = 0; i < len; ++i) {
                        if (buffer[i] == NL)
                           append_ga_line(OUT &shellResponse);
                        ei (buffer[i] == ZERO)
                           ga_append(&shellResponse, NL);
                        else
                           ga_append(&shellResponse, buffer[i]);
                      }
                  } else {
                     buffer[len] = ZERO;
                     msg_puts(buffer);
                  }

                  windgoto(msgRowG, msgColG);
                  cursor_on();
                  out_flush();
                  if (gotInterruptG)
                     break;

                  if (wait_pid == 0) {
                     long msec = ELAPSED_FUNC(start_tv);

                     //Avoid that we keep looping here without checking for a CTRL-C for a long time.
                     //Don't break out too often to avoid losing typeahead.
                     if (msec > 2000) {
                        noread_cnt = 5;
                        break;
                     }
                  }
               }

               // If we already detected the child has finished, continue
               // reading output for a short while.  Some text may be buffered.
               if (wait_pid == pid) {
                  if (noread_cnt < 5)
                     continue;
                  break;
               }

               //Check if the child still exists, before checking for
               //typed characters (otherwise we would lose typeahead).
               wait_pid = waitpid(pid, &status, WNOHANG);
               if ((wait_pid == (ProId)-1 && errno == ECHILD)
                   || (wait_pid == pid && WIFEXITED(status))
               ) {
                  // Don't break the loop yet, try reading more
                  // characters from "fromshell_fd" first.  When using
                  // pipes there might still be something to read and
                  // then we'll break the loop at the "break" above.
                  wait_pid = pid;
               } else
                  wait_pid = 0;

                // Handle Wayland events such as sending data as the source client.
                wayland_client_update();
            }
      finished:
            p_more = p_more_save;
            if ((options & SHELL_READ) != 0) {
               if (shellResponse.len > 0) {
                  append_ga_line(&shellResponse);
                  // remember that the NL was missing
                  curBook->noEolLnum = curPor->cursor.lnum;
               } else
                  curBook->noEolLnum = 0;
               ga_clear(&shellResponse);
            }

            // Give all typeahead that wasn't used back to ui_inchar().
            if (ta_len)
               ui_inBytendo(ta_buf, ta_len);
            stateG = modeSaved;
            if (toshell_fd >= 0)
               close(toshell_fd);
            close(fromshell_fd);
         } else {
            long delay_msec = 1;

            if (tmode == TMODE_RAW)
               // Possibly disables modifyOtherKeys, so that the system can recognize CTRL-C.
               out_str_t_TE();

            //Similar to the loop above, but only handle X and Wayland events, no I/O.
            for (;;) {
               if (gotInterruptG) {
                  // CTRL-C sends a signal to the child, we ignore it ourselves
                  kill(-pid, SIGINT);
                  gotInterruptG = false;
               }
               wait_pid = waitpid(pid, &status, WNOHANG);
               if ((wait_pid == (ProId)-1 && errno == ECHILD) || (wait_pid == pid && WIFEXITED(status))) {
                  wait_pid = pid;
                  break;
               }

               // Handle Wayland events such as sending data as the source client.
               wayland_client_update();

               // Wait for 1 to 10 msec. 1 is faster but gives the child
               // less time, gradually wait longer.
               mch_delay(delay_msec, MCH_DELAY_IGNOREINPUT | MCH_DELAY_SETTMODE);
               if (++delay_msec > 10)
                  delay_msec = 10;
            }

            if (tmode == TMODE_RAW)
               // possibly enables modifyOtherKeys again
               out_str_t_TI();
         }

         //Wait until our child has exited.
         //Ignore wait() returning pids of other children and returning because of some signal 
         //like SIGWINCH. Don't wait if wait_pid was already set above, indicating the
         //child already exited.
         if (wait_pid != pid)
            (void)wait4pid(pid, &status);

         // Make sure the child that writes to the external program is dead.
         if (wpid > 0) {
            kill(wpid, SIGKILL);
            wait4pid(wpid, NULL);
         }

         --dontCheckJobEndedS;

         //Set to raw mode right now, otherwise a CTRL-C after catch_signals() will kill Eegl.
         if (tmode == TMODE_RAW)
            termSetMode(TMODE_RAW);
         did_termSetMode = true;
         set_signals();

         if (WIFEXITED(status)) {
            // LINTED avoid "bitwise operation on signed value"
            int retStatus = WEXITSTATUS(status);
            if (retStatus != 0 && !emsg_silent) {
               if (retStatus == EXEC_FAILED) {
                  msg_puts(_("\nCannot execute shell "));
                  msg_outtrans(S"bash");
                  msg_putchar('\n');
               } ei ((options & SHELL_SILENT) == 0) {
                  msg_puts(_("\nshell returned "));
                  msg_outnum((long)retStatus);
                  msg_putchar('\n');
               }
            }
         } else
            msg_puts(_("\nCommand terminated\n"));
      }
   }

   if (!did_termSetMode && tmode == TMODE_RAW)
      termSetMode(TMODE_RAW);
   eeglFree(argv);
   eeglFree(shellArgs.c);

   return retVal;
}

PolyWithStatus
chCallShell(Multistring* cmd, Unt options) {   // SHELL_*, see eegl.h
   lo("executing shell command: %s", cmd);
   return callShellImpl(cmd, options);
}

int
mch_create_pty_channel(Job* job, JobOptions* options) {
   int pty_master_fd = -1;
   int pty_slave_fd = -1;

   open_pty(&pty_master_fd, &pty_slave_fd, &job->ttyOut, &job->ttyIn);
   if (pty_master_fd < 0 || pty_slave_fd < 0)
      return FAIL;
   close(pty_slave_fd);

   Channel* channel = add_channel();
   if (channel == NULL) {
      close(pty_master_fd);
      return FAIL;
   }
   if (job->ttyOut != NULL)
      ch_log(channel, "using pty %s on fd %d", job->ttyOut, pty_master_fd);
   job->channel = channel;  // refcount was set by add_channel()
   channel->ch_keep_open = true;

   // Only set the pty_master_fd for stdout, do not duplicate it for stderr,
   // it only needs to be read once.
   channel_set_pipes(channel, pty_master_fd, pty_master_fd, INVALID_FD);
   channel_set_job(channel, job, options);
   return OK;
}

//Check for CTRL-C typed by reading all available characters.
//In cooked mode we should get SIGINT, no need to check.
void
chBreakcheck(Boole force) {
   if ((mch_cur_tmode == TMODE_RAW || force) && realWaitForChar(read_cmd_fd, 0L, NULL, NULL)) {
      fill_input_buf(false);
   } 
}

SigHandler
mch_signal(int sig, SigHandler func) {
   // Modern implementation: use sigaction().
   struct sigaction   sa, old;
   sigset_t curset;

   if (sigprocmask(SIG_BLOCK, NULL, &curset) == -1)
      return SIG_ERR;

   int blocked = sigismember(&curset, sig);

   if (func == SIG_HOLD) {
      if (blocked)
          return SIG_HOLD;

      sigemptyset(&curset);
      sigaddset(&curset, sig);

      if (sigaction(sig, NULL, &old) == -1 || sigprocmask(SIG_BLOCK, &curset, NULL) == -1)
         return SIG_ERR;
      return old.sa_handler;
   }

   if (blocked) {
      sigemptyset(&curset);
      sigaddset(&curset, sig);

      if (sigprocmask(SIG_UNBLOCK, &curset, NULL) == -1)
         return SIG_ERR;
   }

   sa.sa_handler = func;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART;
   if (sigaction(sig, &sa, &old) == -1)
      return SIG_ERR;
   return blocked ? SIG_HOLD: old.sa_handler;
}

void
mch_early_init(void) {
#ifdef HAVE_CHECK_STACK_GROWTH
   int         i;

   check_stack_growth((char *)&i);

#endif

   //Setup an alternative stack for signals. Helps to catch signals when running out of stack 
   //space. Use of sigaltstack() is preferred, it's more portable. Ignore any errors.
   signal_stack = alloc(get_signal_stack_size());
   init_signal_stack();
}

//return process ID
long
mch_get_pid(void) {
   return (long)getpid();
}

//return true if process "pid" is still running
int
mch_process_running(long pid) {
   // If there is no error the process must be running.
   if (kill(pid, 0) == 0)
      return true;
#ifdef ESRCH
   // If the error is ESRCH then the process is not running.
   if (errno == ESRCH)
      return false;
#endif
   // If the process is running and owned by another user we get EPERM.  With
   // other errors the process might be running, assuming it is then.
   return true;
}

//Open a PTY, with FD for the master and slave side.
//When failing "pty_master_fd" and "pty_slave_fd" are -1.
//When successful both file descriptors are stored and the allocated pty name
//is stored in both "*name1" and "*name2".
private void
open_pty(int *pty_master_fd, int *pty_slave_fd, Byte **name1, Byte **name2) {
   char   *tty_name;

   if (name1 != NULL)
      *name1 = NULL;
   if (name2 != NULL)
      *name2 = NULL;

   *pty_master_fd = openpty(&tty_name);       // open pty
   if (*pty_master_fd < 0)
      return;

   // Leaving out O_NOCTTY may lead to waitpid() always returning
   // 0 on Mac OS X 10.7 thereby causing freezes. Let's assume
   // adding O_NOCTTY always works when defined.
#ifdef O_NOCTTY
   *pty_slave_fd = open(tty_name, O_RDWR | O_NOCTTY | O_EXTRA, 0);
#else
   *pty_slave_fd = open(tty_name, O_RDWR | O_EXTRA, 0);
#endif
   if (*pty_slave_fd < 0) {
      close(*pty_master_fd);
      *pty_master_fd = -1;
   } else {
      if (name1)
         *name1 = copyStr((CS)tty_name);
      if (name2)
         *name2 = copyStr((CS)tty_name);
   }
}

void
may_core_dump(void) {
   if (deadlySignalS != 0) {
      mch_signal(deadlySignalS, SIG_DFL);
      kill(getpid(), deadlySignalS);   // Die using the signal we caught
   }
}

//}}}
//{{{signal handlers


//We need correct prototypes for a signal function, otherwise mean compilers
//will barf when the second argument to signal() is ``wrong''.
//Let me try it with a few tricky defines from my own osdef.h   (jw).
void
sig_winch SIGDEFARG(sigarg) {
   // this is not required on all systems, but it doesn't hurt anybody
   mch_signal(SIGWINCH, sig_winch);
   doResizeG = true;
}

void
sig_tstp SIGDEFARG(sigarg) {
   mch_signal(SIGTSTP, sig_tstp);
}

private void
catch_sigint SIGDEFARG(sigarg) {
   // this is not required on all systems, but it doesn't hurt anybody
   mch_signal(SIGINT, catch_sigint);
   gotInterruptG = true;
}

#if defined(SIGUSR1)
private void
catch_sigusr1 SIGDEFARG(sigarg) {
    // this is not required on all systems, but it doesn't hurt anybody
    mch_signal(SIGUSR1, catch_sigusr1);
    got_sigusr1 = true;
}
#endif

#if defined(SIGPWR)
private void
catch_sigpwr SIGDEFARG(sigarg) {
   // this is not required on all systems, but it doesn't hurt anybody
   mch_signal(SIGPWR, catch_sigpwr);
   //I'm not sure we get the SIGPWR signal when the system is really going down or when the 
   //batteries are almost empty. Just preserve the swap files and don't exit, that can't do any 
   //harm.
   ml_sync_all(false, false);
}
#endif

#ifdef SET_SIG_ALARM
//signal function for alarm().
//private void
//sig_alarm SIGDEFARG(sigarg) {
//   // doesn't do anything, just to break a system call
//   sig_alarm_called = true;
//}
#endif


//This function handles deadly signals.
//It tries to preserve any swap files and exit properly.
//(partly from Elvis).
//NOTE: Avoid unsafe functions, such as allocating memory, they can result in a deadlock.
private void
deathtrap SIGDEFARG(sigarg) {
   static int   entered = 0;       // count the number of times we got here.
                // Note: when memory has been corrupted this may get an arbitrary value!
   int      i;

#if defined(USING_SETJMP)
   //Catch a crash in protected code.
   //Restores the environment saved in lc_jump_env, which looks like SETJMP() returns 1.
   if (lc_active) {
      lc_signal = sigarg;
      lc_active = false;   // don't jump again
      LONGJMP(lc_jump_env, 1);
      // NOTREACHED
   }
#endif

   //While in mch_delay() we go to cooked mode to allow a CTRL-C to interrupt us. But in cooked 
   //mode we may also get SIGQUIT, e.g., when pressing CTRL-\, but we don't want Eegl to exit then.
   if (inMchDelayS && sigarg == SIGQUIT)
      return;

   // When SIGHUP, SIGQUIT, etc. are blocked: postpone the effect and return
   // here.  This avoids that a non-reentrant function is interrupted, e.g.,
   // free().  Calling free() again may then cause a crash.
   if (entered == 0
       && (0
      || sigarg == SIGHUP
      || sigarg == SIGQUIT
      || sigarg == SIGTERM
#ifdef SIGPWR
      || sigarg == SIGPWR
#endif
#ifdef SIGUSR1
      || sigarg == SIGUSR1
#endif
#ifdef SIGUSR2
      || sigarg == SIGUSR2
#endif
      )
          && !eeHandleSignal(sigarg))
      return;

   // Remember how often we have been called.
   ++entered;

   // Executing autocommands is likely to use more stack space than we have
   // available in the signal stack.
   block_autocmds();

   // Set the v:dying variable.
   set_EeglVar_nr(VV_DYING, (long)entered);
   v_dying = entered;

#if 0
   // This is for opening gdb the moment Eegl crashes.
   // You need to manually adjust the file name and Eegl executable name.
   // Suggested by SungHyun Nam.
    {
# define EE_GDB_FILE "/tmp/eegdb"
# define EE_NAME "/usr/bin/eegl"
   FILE *fp = fopen(VI_GDB_FILE, "w");
   if (fp)
   {
       fprintf(fp,
          "file %s\n"
          "attach %d\n"
          "set height 1000\n"
          "bt full\n"
          , EE_NAME, getpid());
       fclose(fp);
       system("xterm -e gdb -x "EE_GDB_FILE);
       unlink(EE_GDB_FILE);
   }
   }
#endif

   // try to find the name of this signal
   for (i = 0; signalInfos[i].sig != -1; i++) {
      if (sigarg == signalInfos[i].sig)
         break;
   } 
   deadlySignalS = sigarg;

   fullScreenG = false; // don't write messages to the UI, it might be part of the problem...
   //If something goes wrong after entering here, we may get here again.
   //When this happens, give a message and try to exit nicely (resetting the terminal mode, etc.)
   //When this happens twice, just exit, don't even try to give a message,
   //stack may be corrupt or something weird.
   //When this still happens again (or memory was corrupted in such a way
   //that "entered" was clobbered) use _exit(), don't try freeing resources.
   if (entered >= 3) {
      reset_signals();   // don't catch any signals anymore
      may_core_dump();
      if (entered >= 4)
         _exit(8);
      exit(7);
   }
   if (entered == 2) {
      // No translation, it may call malloc().
      OUT_STR("Eegl: Double signal, exiting\n");
      out_flush();
      exitEegl(1);
   }

   // No translation, it may call malloc().
   sprintf((char *)IObuff, "Eegl: Caught deadly signal %s\r\n", signalInfos[i].name);

   // Preserve files and exit.  This sets the really_exiting flag to prevent calling free().
   preserve_exit();

   // NOTREACHED
}

//Invoked after receiving SIGCONT.  We don't know what happened while
//sleeping, deal with part of that.
private void
after_sigcont(void) {
   termSetMode(TMODE_RAW);
   need_check_timestamps = true;
   did_check_timestamps = false;
}


//With multi-threading, suspending might not work immediately.  Catch the
//SIGCONT signal, which will be used as an indication whether the suspending
//has been done or not.
//
//On Linux, signal is not always handled immediately either.
//See https://bugs.launchpad.net/bugs/291373
//Probably because the signal is handled in another thread.
//
//volatile because it is used in signal handler sigcont_handler().
private volatile SigAtomic sigcont_received;
private void sigcont_handler SIGPROTOARG;

//signal handler for SIGCONT
private void
sigcont_handler SIGDEFARG(sigarg) {
   // We didn't suspend ourselves, assume we were stopped by a SIGSTOP signal (which can't 
   // be intercepted) and get a SIGCONT. Need to get back to a sane mode. We should redraw, but 
   // we can't really do that in a signal handler, do a redraw later.
   after_sigcont();
   redraw_later(UPD_CLEAR);
   cursor_on_force();
   out_flush();
}



//Catch CTRL-C (only works while in Cooked mode).
private void
catch_int_signal(void) {
   mch_signal(SIGINT, catch_sigint);
}

void
reset_signals(void) {
   catch_signals(SIG_DFL, SIG_DFL);
   // SIGCONT isn't in the list, because its default action is ignore
   mch_signal(SIGCONT, SIG_DFL);
}

private void
catch_signals(void (*func_deadly)(int), void (*func_other)(int)) {
   for (int i = 0; signalInfos[i].sig != -1; i++) {
      if (signalInfos[i].deadly) {
         struct sigaction sa;

         // Setup to use the alternate stack for the signal function.
         sa.sa_handler = func_deadly;
         sigemptyset(&sa.sa_mask);
         sa.sa_flags = 0;
         sigaction(signalInfos[i].sig, &sa, NULL);
      } ei (func_other != SIG_ERR) {
         // Deal with non-deadly signals.
         mch_signal(
            signalInfos[i].sig, 
            signalInfos[i].sig == SIGTSTP && ignore_sigtstp ? SIG_IGN : func_other
         );
      }
   }
}

//Handling of SIGHUP, SIGQUIT and SIGTERM:
//"when" == a signal:       when busy, postpone and return false, otherwise return true
//"when" == SIGNAL_BLOCK:   Going to be busy, block signals
//"when" == SIGNAL_UNBLOCK: Going to wait, unblock signals, use postponed signal
//Return true when Eegl should exit.
int
eeHandleSignal(int sig) {
   static int got_signal = 0;
   static int blocked = true;

   switch (sig) {
   case SIGNAL_BLOCK:   
      blocked = true;
      break;

   case SIGNAL_UNBLOCK: 
      blocked = false;
      if (got_signal != 0) {
         kill(getpid(), got_signal);
         got_signal = 0;
      }
      break;

   default:
      if (!blocked)
         return true;   // exit!
      got_signal = sig;
#ifdef SIGPWR
      if (sig != SIGPWR)
#endif
         gotInterruptG = true;    // break any loops
      break;
    }
    return false;
}
//}}}
//{{{operating system interaction

//Insert user name for "uid" in s[len]. Return OK if a name found.
int
mch_get_uname(uid_t uid, CS s, int len) {
   struct passwd   *pw;

   if ((pw = getpwuid(uid)) != NULL && pw->pw_name != NULL && *(pw->pw_name) != ZERO) {
      copySubstrToAllocation(s, (Text){(CS)pw->pw_name, len - 1});
      return OK;
   }
   sprintf((char *)s, "%d", (int)uid);       // assumes s is long enough
   return FAIL;             // a number is not a name
}

//Insert host name is s[len].
void
mch_get_host_name(CS s, int len) {
   struct utsname vutsname;

   if (uname(&vutsname) < 0)
      *s = ZERO;
   else
      copySubstrToAllocation(s, (Text){(CS)vutsname.nodename, len - 1});
}

// Set the environment for a child process.
private void
set_child_environment(
   long rows,
   long columns,
   CS term,
   int is_terminal UNUSED
) {
   char   envbuf[50];

   setenv("TERM", (char*)term, 1);
   sprintf((char *)envbuf, "%ld", rows);
   setenv("ROWS", (char *)envbuf, 1);
   sprintf((char *)envbuf, "%ld", rows);
   setenv("LINES", (char *)envbuf, 1);
   sprintf((char *)envbuf, "%ld", columns);
   setenv("COLUMNS", (char *)envbuf, 1);
   sprintf((char *)envbuf, "%d", 256);
   setenv("COLORS", (char *)envbuf, 1);
   if (is_terminal) {
      sprintf((char *)envbuf, "%ld",  (long)get_EeglVar_nr(VV_VERSION));
      setenv("EEGL_TERMINAL", (char *)envbuf, 1);
   }
   setenv("EEGL_SERVERNAME", serverName == NULL ? "" : (char *)serverName, 1);
}

private void
set_default_child_environment(int is_terminal) {
   set_child_environment(visibleRowsG, visibleColsG, S"dumb", is_terminal);
}

//}}}
//{{{job runnin' and controllin'

int
chJobGetCopyId(Job* job) {
   return job->copyId;
}

void
chJobSetCopyId(Job* job, int newVal) {
   job->copyId = newVal;
}

Channel*
chJobGetChannel(Job* job) {
   return job->channel;
}

Callback
chJobGetExitCb(Job* job) {
   return job->exitCb;
}

JobStatus
chJobGetStatus(Job* job) {
   return job->status;
}

void
chJobSetStatus(Job* job, JobStatus newVal) {
   job->status = newVal;
}
    
Arr(Byte)
chJobGetTty(Job* job, Boole out) {
   if (out) {
      return job->ttyOut;
   } else {
      return job->ttyIn;
   }
}
    
private void
mch_job_start(Byte** argv, Job* job, JobOptions *options, int is_terminal) {
   ProId   pid;
   int fd_in[2] = {-1, -1};   // for stdin
   int fd_out[2] = {-1, -1};   // for stdout
   int fd_err[2] = {-1, -1};   // for stderr
   int pty_master_fd = -1;
   int pty_slave_fd = -1;
   Channel* channel = NULL;
   int use_null_for_in = options->ioMode[PART_IN] == JIO_NULL;
   int use_null_for_out = options->ioMode[PART_OUT] == JIO_NULL;
   int use_null_for_err = options->ioMode[PART_ERR] == JIO_NULL;
   int use_file_for_in = options->ioMode[PART_IN] == JIO_FILE;
   int use_file_for_out = options->ioMode[PART_OUT] == JIO_FILE;
   int use_file_for_err = options->ioMode[PART_ERR] == JIO_FILE;
   int use_buffer_for_in = options->ioMode[PART_IN] == JIO_BUFFER;
   int use_out_for_err = options->ioMode[PART_ERR] == JIO_OUT;
   SIGSET_DECL(curset)

   if (use_out_for_err && use_null_for_out)
      use_null_for_err = true;

   // default is to fail
   job->status = JOB_FAILED;

   if (options->jo_pty
          && (!(use_file_for_in || use_null_for_in)
            || !(use_file_for_out || use_null_for_out)
            || !(use_out_for_err || use_file_for_err || use_null_for_err))) {
      open_pty(&pty_master_fd, &pty_slave_fd, &job->ttyOut, &job->ttyIn);
   } 

   // TODO: without the channel feature connect the child to /dev/null?
   // Open pipes for stdin, stdout, stderr.
   if (use_file_for_in) {
      CS fname = options->name[PART_IN];

      fd_in[0] = open((char *)fname, O_RDONLY, 0);
      if (fd_in[0] < 0) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         goto failed;
      }
   } ei (!use_null_for_in && (pty_master_fd < 0 || use_buffer_for_in) && pipe(fd_in) < 0) {
      //When writing buffer lines to the input don't use the pty, so that the pipe can be closed 
      //when all lines were written.
      goto failed;
   } 

   if (use_file_for_out) {
      CS fname = options->name[PART_OUT];

      fd_out[1] = open((char *)fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd_out[1] < 0) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         goto failed;
      }
   } ei (!use_null_for_out && pty_master_fd < 0 && pipe(fd_out) < 0)
      goto failed;

   if (use_file_for_err) {
      CS fname = options->name[PART_ERR];

      fd_err[1] = open((char *)fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd_err[1] < 0) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         goto failed;
      }
   }
   // only create a pipe for the error fd, when either a callback has been setup
   // or pty is not used (e.g. terminal uses pty by default)
   ei (!use_out_for_err && !use_null_for_err
         && (pty_master_fd < 0 || (options->set & JO_ERR_CALLBACK)) && pipe(fd_err) < 0) {
      goto failed;
   } 

   if (!use_null_for_in || !use_null_for_out || !use_null_for_err) {
      if (options->set & JO_CHANNEL) {
         channel = options->jo_channel;
         if (channel)
            ++channel->refCount;
      } else
         channel = add_channel();
      if (!channel)
         goto failed;
      if (job->ttyOut)
         ch_log(channel, "using pty %s on fd %d", job->ttyOut, pty_master_fd);
   }

   BLOCK_SIGNALS(&curset);
   pid = fork();   // maybe we should use vfork()
   if (pid == -1) {
      // failed to fork
      UNBLOCK_SIGNALS(&curset);
      goto failed;
   }
   if (pid == 0) {
      int   null_fd = -1;
      int   stderr_works = true;

      // child
      reset_signals();      // handle signals normally
      UNBLOCK_SIGNALS(&curset);

      if (ch_log_active())
         // close the log file in the child
         ch_logfile(S"", S"");

      //Create our own process group, so that the child and all its
      //children can be kill()ed.  Don't do this when using pipes,
      //because stdin is not a tty, we would lose /dev/tty.
      (void)setsid();

      if (options->jo_term_rows > 0) {
         CS term = termCodesG[KS_NAME];

         //Use 'term' or $TERM if it starts with "xterm", otherwise fall
         //back to "xterm" or "xterm-color".
         if (!term || *term == ZERO || STRNCMP(term, "xterm", 5) != 0) {
            term = S"xterm-256color";
         }
         set_child_environment(
            (long)options->jo_term_rows,
            (long)options->jo_term_cols,
            term,
            is_terminal
         );
      } else
          set_default_child_environment(is_terminal);

      if (options->env != NULL) {
         Bag* dict = options->env;
         EeSetItem* hi;
         int todo = (int)dict->hashTable.count;

         FOR_ALL_HASHTAB_ITEMS(&dict->hashTable, hi, todo) {
            if (!HASHITEM_EMPTY(hi)) {
               Var *item = &bagLookup(hi)->c;

               eeSetenv(hi->hi_key, tv_get_string(item));
               --todo;
            }
         } 
      }

      if (use_null_for_in || use_null_for_out || use_null_for_err) {
         null_fd = open("/dev/null", O_RDWR | O_EXTRA, 0);
         if (null_fd < 0) {
            perror("opening /dev/null failed");
            _exit(OPEN_NULL_FAILED);
         }
      }

      if (pty_slave_fd >= 0) {
         // push stream discipline modules
         setup_slavepty(pty_slave_fd);
#  ifdef TIOCSCTTY
         // Try to become controlling tty (probably doesn't work, unless run by root)
         ioctl(pty_slave_fd, TIOCSCTTY, (char *)NULL);
#  endif
      }

      // set up stdin for the child
      close(0);
      if (use_null_for_in && null_fd >= 0)
         (void)dup(null_fd);
      ei (fd_in[0] < 0)
         (void)dup(pty_slave_fd);
      else
         (void)dup(fd_in[0]);

      // set up stderr for the child
      close(2);
      if (use_null_for_err && null_fd >= 0) {
         (void)dup(null_fd);
         stderr_works = false;
      } ei (use_out_for_err)
         (void)dup(fd_out[1]);
      ei (fd_err[1] < 0)
         (void)dup(pty_slave_fd);
      else
         (void)dup(fd_err[1]);

      // set up stdout for the child
      close(1);
      if (use_null_for_out && null_fd >= 0)
         (void)dup(null_fd);
      ei (fd_out[1] < 0)
         (void)dup(pty_slave_fd);
      else
         (void)dup(fd_out[1]);

      if (fd_in[0] >= 0)
         close(fd_in[0]);
      if (fd_in[1] >= 0)
         close(fd_in[1]);
      if (fd_out[0] >= 0)
         close(fd_out[0]);
      if (fd_out[1] >= 0)
         close(fd_out[1]);
      if (fd_err[0] >= 0)
         close(fd_err[0]);
      if (fd_err[1] >= 0)
         close(fd_err[1]);
      if (pty_master_fd >= 0) {
         close(pty_master_fd); // not used in the child
         close(pty_slave_fd);  // was duped above
      }

      if (null_fd >= 0)
         close(null_fd);

      if (options->currentWorkingDir != NULL && mch_chdir(options->currentWorkingDir) != 0)
         _exit(EXEC_FAILED);

      // See above for type of argv.
      execvp((char*)argv[0], (char**)argv);

      if (stderr_works)
          perror("executing job failed");
# ifdef EXITFREE
      //calling free_all_mem() here causes problems. Ignore valgrind
      //reporting possibly leaked memory.
# endif
      _exit(EXEC_FAILED);       // exec failed, return failure code
   }

   // parent
   UNBLOCK_SIGNALS(&curset);

   job->pid = pid;
   job->status = JOB_STARTED;
   job->channel = channel;  // refcount was set above

   if (pty_master_fd >= 0)
      close(pty_slave_fd); // not used in the parent
   // close child stdin, stdout and stderr
   if (fd_in[0] >= 0)
      close(fd_in[0]);
   if (fd_out[1] >= 0)
      close(fd_out[1]);
   if (fd_err[1] >= 0)
      close(fd_err[1]);
   if (channel != NULL) {
      int in_fd = INVALID_FD;
      int out_fd = INVALID_FD;
      int err_fd = INVALID_FD;

      if (!(use_file_for_in || use_null_for_in))
         in_fd = fd_in[1] >= 0 ? fd_in[1] : pty_master_fd;

      if (!(use_file_for_out || use_null_for_out))
         out_fd = fd_out[0] >= 0 ? fd_out[0] : pty_master_fd;

      // When using pty_master_fd only set it for stdout, do not duplicate
      // it for stderr, it only needs to be read once.
      if (!(use_out_for_err || use_file_for_err || use_null_for_err)) {
         if (fd_err[0] >= 0)
            err_fd = fd_err[0];
         ei (out_fd != pty_master_fd)
            err_fd = pty_master_fd;
      }

      channel_set_pipes(channel, in_fd, out_fd, err_fd);
      channel_set_job(channel, job, options);
   } else {
      if (fd_in[1] >= 0)
         close(fd_in[1]);
      if (fd_out[0] >= 0)
         close(fd_out[0]);
      if (fd_err[0] >= 0)
         close(fd_err[0]);
      if (pty_master_fd >= 0)
         close(pty_master_fd);
   }

   // success!
   return;

failed:
   channel_unref(channel);
   if (fd_in[0] >= 0)
      close(fd_in[0]);
   if (fd_in[1] >= 0)
      close(fd_in[1]);
   if (fd_out[0] >= 0)
      close(fd_out[0]);
   if (fd_out[1] >= 0)
      close(fd_out[1]);
   if (fd_err[0] >= 0)
      close(fd_err[0]);
   if (fd_err[1] >= 0)
      close(fd_err[1]);
   if (pty_master_fd >= 0)
      close(pty_master_fd);
   if (pty_slave_fd >= 0)
      close(pty_slave_fd);
}

private CS
mch_job_status(Job* job) {
   int status = -1;
   ProId wait_pid = 0;

   wait_pid = waitpid(job->pid, &status, WNOHANG);
   if (wait_pid == -1) {
      int waitpid_errno = errno;
      if (waitpid_errno == ECHILD && mch_process_running(job->pid))
          // The process is alive, but it was probably reparented (for
          // example by ptrace called by a debugger like lldb or gdb).
          // Note: This assumes that process IDs are not reused.
          return S"run";

      // process must have exited
      if (job->status < JOB_ENDED)
         ch_log(job->channel, "Job no longer exists: %s", strerror(waitpid_errno));
      goto return_dead;
   }
   if (wait_pid == 0)
      return S"run";
   if (WIFEXITED(status)) {
      // LINTED avoid "bitwise operation on signed value"
      job->exitVal = WEXITSTATUS(status);
      if (job->status < JOB_ENDED)
         ch_log(job->channel, "Job exited with %d", job->exitVal);
      goto return_dead;
   }
   if (WIFSIGNALED(status)) {
      job->exitVal = -1;
      job->jv_termsig = get_signal_name(WTERMSIG(status));
      if (job->status < JOB_ENDED && job->jv_termsig != NULL)
          ch_log(job->channel, "Job terminated by signal \"%s\"", job->jv_termsig);
      goto return_dead;
   }
   return S"run";

return_dead:
   if (job->status < JOB_ENDED) {
      job->status = JOB_ENDED;
   } 
   return S"dead";
}

//Send a (deadly) signal to "job". Return FAIL if "how" is not a valid name.
int
mch_signal_job(Job* job, CS how) {
   int sig = -1;

   if (*how == ZERO || STRCMP(how, "term") == 0)
      sig = SIGTERM;
   ei (STRCMP(how, "hup") == 0)
      sig = SIGHUP;
   ei (STRCMP(how, "quit") == 0)
      sig = SIGQUIT;
   ei (STRCMP(how, "int") == 0)
      sig = SIGINT;
   ei (STRCMP(how, "kill") == 0)
      sig = SIGKILL;
   ei (STRCMP(how, "winch") == 0)
      sig = SIGWINCH;
   ei (SAFE_isdigit(*how))
      sig = atoi((char *)how);
   else
      return FAIL;

   // Never kill ourselves!
   if (job->pid != 0) {
      // TODO: have an option to only kill the process, not the group?
      kill(-job->pid, sig);
      kill(job->pid, sig);
   }

   return OK;
}



private Job *
mch_detect_ended_job(Job* job_list) {
   int      status = -1;

   // Do not do this when waiting for a shell command to finish, we would get
   // the exit value here (and discard it), the exit value obtained there would then be wrong.
   if (dontCheckJobEndedS > 0)
      return NULL;

   ProId wait_pid = waitpid(-1, &status, WNOHANG);
   if (wait_pid <= 0)
      // no process ended
      return NULL;
   for (Job* job = job_list; job; job = job->next) {
      if (job->pid == wait_pid) {
         if (WIFEXITED(status))
            // LINTED avoid "bitwise operation on signed value"
            job->exitVal = WEXITSTATUS(status);
         ei (WIFSIGNALED(status)) {
            job->exitVal = -1;
            job->jv_termsig = get_signal_name(WTERMSIG(status));
         }
         if (job->status < JOB_ENDED) {
            ch_log(job->channel, "Job ended");
            job->status = JOB_ENDED;
         }
         return job;
      }
   }
   return NULL;
}

private int
handle_mode(Var* item, JobOptions* opt, ChannelMode* modep, int jo) {
   CS val = tv_get_string(item);
   opt->set |= jo;
   if (STRCMP(val, "nl") == 0)
      *modep = CH_MODE_NL;
   ei (STRCMP(val, "raw") == 0)
      *modep = CH_MODE_RAW;
   ei (STRCMP(val, "json") == 0)
      *modep = CH_MODE_JSON;
   ei (STRCMP(val, "lsp") == 0)
      *modep = CH_MODE_LSP;
   else {
      showErrFmtMsg(_(e_invalid_argument_str), val);
      return FAIL;
   }
   return OK;
}

private int
handle_io(Var* item, ChannelFdKind part, JobOptions* opt) {
   CS val = tv_get_string(item);

   opt->set |= JO_OUT_IO << (part - PART_OUT);
   if (STRCMP(val, "null") == 0)
      opt->ioMode[part] = JIO_NULL;
   ei (STRCMP(val, "pipe") == 0)
      opt->ioMode[part] = JIO_PIPE;
   ei (STRCMP(val, "file") == 0)
      opt->ioMode[part] = JIO_FILE;
   ei (STRCMP(val, "buffer") == 0)
      opt->ioMode[part] = JIO_BUFFER;
   ei (STRCMP(val, "out") == 0 && part == PART_ERR)
      opt->ioMode[part] = JIO_OUT;
   else {
      showErrFmtMsg(_(e_invalid_argument_str), val);
      return FAIL;
   }
   return OK;
}

private void
unref_job_callback(Callback *cb) {
   if (cb->cb_partial)
      partial_unref(cb->cb_partial);
   ei (cb->name) {
      func_unref(cb->name);
   if (cb->needsFreeing)
      eeglFree(cb->name);
   }
}

// Free any members of a JobOptions.
void
free_job_options(JobOptions* opt) {
   unref_job_callback(&opt->jo_callback);
   unref_job_callback(&opt->jo_out_cb);
   unref_job_callback(&opt->jo_err_cb);
   unref_job_callback(&opt->closeCb);
   unref_job_callback(&opt->exitCb);

   if (opt->env)
      bagUnref(opt->env);
}

// Get the PART_ number from the first character of an option name.
private int
part_from_char(int c) {
   return c == 'i' ? PART_IN : c == 'o' ? PART_OUT: PART_ERR;
}

//Clear the data related to "job".
void
mch_clear_job(Job* job) {
   // call waitpid because child process may become zombie
   (void)waitpid(job->pid, NULL, WNOHANG);
}


//Get the option entries from the dict in "tv", parse them and put the result in "opt".
//Only accept JO_ options in "supported" and JO2_ options in "supported2".
//If an option value is invalid, return FAIL.
int
get_job_options(Var* tv, OUT JobOptions* opt, int supported, int supported2) {
   Var   *item;
   CS val;
   EeSetItem* hi;
   ChannelFdKind part;

   if (tv->tag == VAR_UNKNOWN)
      return OK;
   if (tv->tag != VAR_BAG) {
      emsg(_(e_dictionary_required));
      return FAIL;
   }
   Bag* dict = tv->bag;
   if (!dict)
      return OK;

   int todo = (int)dict->hashTable.count;
   FOR_ALL_HASHTAB_ITEMS(&dict->hashTable, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         item = &bagLookup(hi)->c;

         if (STRCMP(hi->hi_key, "mode") == 0) {
            if (!(supported & JO_MODE))
                break;
            if (handle_mode(item, opt, &opt->mode, JO_MODE) == FAIL)
                return FAIL;
         } ei (STRCMP(hi->hi_key, "in_mode") == 0) {
            if (!(supported & JO_IN_MODE))
                break;
            if (handle_mode(item, opt, &opt->jo_in_mode, JO_IN_MODE) == FAIL)
                return FAIL;
          } ei (STRCMP(hi->hi_key, "out_mode") == 0) {
            if (!(supported & JO_OUT_MODE))
               break;
            if (handle_mode(item, opt, &opt->jo_out_mode, JO_OUT_MODE) == FAIL)
               return FAIL;
         } ei (STRCMP(hi->hi_key, "err_mode") == 0) {
            if (!(supported & JO_ERR_MODE))
                break;
            if (handle_mode(item, opt, &opt->jo_err_mode, JO_ERR_MODE) == FAIL)
                return FAIL;
         } ei (STRCMP(hi->hi_key, "noblock") == 0) {
            if (!(supported & JO_MODE))
               break;
            opt->jo_noblock = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "in_io") == 0
                || STRCMP(hi->hi_key, "out_io") == 0
                || STRCMP(hi->hi_key, "err_io") == 0
         ) {
            if (!(supported & JO_OUT_IO))
                break;
            if (handle_io(item, part_from_char(*hi->hi_key), opt) == FAIL)
                return FAIL;
         } ei (STRCMP(hi->hi_key, "in_name") == 0
             || STRCMP(hi->hi_key, "out_name") == 0
             || STRCMP(hi->hi_key, "err_name") == 0
         ) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_OUT_NAME << (part - PART_OUT);
            opt->name[part] = convertVarToString(item,  opt->nameText[part]);
         } ei (STRCMP(hi->hi_key, "pty") == 0) {
            if (!(supported & JO_MODE))
               break;
            opt->jo_pty = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "in_buf") == 0
             || STRCMP(hi->hi_key, "out_buf") == 0
             || STRCMP(hi->hi_key, "err_buf") == 0
         ) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_OUT_BUF << (part - PART_OUT);
            opt->ioText[part] = tv_get_number(item);
            if (opt->ioText[part] <= 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), hi->hi_key, tv_get_string(item));
               return FAIL;
            }
            if (bookFindFileByBookNr(opt->ioText[part]) == NULL) {
               showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt->ioText[part]);
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "out_modifiable") == 0
             || STRCMP(hi->hi_key, "err_modifiable") == 0
         ) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_OUT_MODIFIABLE << (part - PART_OUT);
            opt->jo_modifiable[part] = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "out_msg") == 0 || STRCMP(hi->hi_key, "err_msg") == 0) {
            part = part_from_char(*hi->hi_key);

            if (!(supported & JO_OUT_IO))
               break;
            opt->set1 |= JO2_OUT_MSG << (part - PART_OUT);
            opt->jo_message[part] = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "in_top") == 0 || STRCMP(hi->hi_key, "in_bot") == 0) {
            LineNr *lp;

            if (!(supported & JO_OUT_IO))
               break;
            if (hi->hi_key[3] == 't') {
               lp = &opt->jo_in_top;
               opt->set |= JO_IN_TOP;
            } else {
               lp = &opt->jo_in_bot;
               opt->set |= JO_IN_BOT;
            }
            *lp = tv_get_number(item);
            if (*lp < 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), hi->hi_key, tv_get_string(item));
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "channel") == 0) {
            if (!(supported & JO_OUT_IO))
               break;
            opt->set |= JO_CHANNEL;
            if (item->tag != VAR_CHANNEL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "channel");
               return FAIL;
            }
            opt->jo_channel = item->channel;
         }
         ei (STRCMP(hi->hi_key, "callback") == 0) {
            if (!(supported & JO_CALLBACK))
                break;
            opt->set |= JO_CALLBACK;
            opt->jo_callback = get_callback(item);
            if (opt->jo_callback.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "callback");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "out_cb") == 0) {
            if (!(supported & JO_OUT_CALLBACK))
                break;
            opt->set |= JO_OUT_CALLBACK;
            opt->jo_out_cb = get_callback(item);
            if (opt->jo_out_cb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "out_cb");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "err_cb") == 0) {
            if (!(supported & JO_ERR_CALLBACK))
               break;
            opt->set |= JO_ERR_CALLBACK;
            opt->jo_err_cb = get_callback(item);
            if (opt->jo_err_cb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "err_cb");
               return FAIL;
            }
          } ei (STRCMP(hi->hi_key, "close_cb") == 0) {
            if (!(supported & JO_CLOSE_CALLBACK))
                break;
            opt->set |= JO_CLOSE_CALLBACK;
            opt->closeCb = get_callback(item);
            if (opt->closeCb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "close_cb");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "drop") == 0) {
            int never = false;
            val = tv_get_string(item);

            if (STRCMP(val, "never") == 0)
               never = true;
            ei (STRCMP(val, "auto") != 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "drop", val);
               return FAIL;
            }
            opt->dropNever = never;
         } ei (STRCMP(hi->hi_key, "exit_cb") == 0) {
            if (!(supported & JO_EXIT_CB))
                break;
            opt->set |= JO_EXIT_CB;
            opt->exitCb = get_callback(item);
            if (opt->exitCb.name == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "exit_cb");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "term_name") == 0) {
            if (!(supported2 & JO2_TERM_NAME))
                break;
            opt->set1 |= JO2_TERM_NAME;
            opt->jo_term_name = convertVarToString(item, opt->jo_term_name_buf);
            if (opt->jo_term_name == NULL) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_name");
                return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "term_finish") == 0) {
            if (!(supported2 & JO2_TERM_FINISH))
               break;
            val = tv_get_string(item);
            if (STRCMP(val, "open") != 0 && STRCMP(val, "close") != 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "term_finish", val);
               return FAIL;
            }
            opt->set1 |= JO2_TERM_FINISH;
            opt->jo_term_finish = *val;
         } ei (STRCMP(hi->hi_key, "term_opencmd") == 0) {
            if (!(supported2 & JO2_TERM_OPENCMD))
               break;
            opt->set1 |= JO2_TERM_OPENCMD;
            CS p = opt->jo_term_opencmd = convertVarToString(item, opt->jo_term_opencmd_buf);
            if (p) {
               // Must have %d and no other %.
               p = firstOccurrence(p, '%');
               if (p && (p[1] != 'd' || firstOccurrence(p + 2, '%') != NULL))
                  p = NULL;
            }
            if (!p) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_opencmd");
                return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "eof_chars") == 0) {
            if (!(supported2 & JO2_EOF_CHARS))
               break;
            opt->set1 |= JO2_EOF_CHARS;
            opt->jo_eof_chars = convertVarToString(item,
                               opt->jo_eof_chars_buf);
            if (opt->jo_eof_chars == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "eof_chars");
               return FAIL;
            }
          } ei (STRCMP(hi->hi_key, "term_rows") == 0) {
            Boole error = false;

            if (!(supported2 & JO2_TERM_ROWS))
               break;
            opt->set1 |= JO2_TERM_ROWS;
            opt->jo_term_rows = varGetNumberChk(item, OUT &error);
            if (error)
               return FAIL;
            if (opt->jo_term_rows < 0 || opt->jo_term_rows > 1000) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_rows");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "term_cols") == 0) {
            Boole error = false;

            if (!(supported2 & JO2_TERM_COLS))
               break;
            opt->set1 |= JO2_TERM_COLS;
            opt->jo_term_cols = varGetNumberChk(item, OUT &error);
            if (error)
               return FAIL;
            if (opt->jo_term_cols < 0 || opt->jo_term_cols > 1000) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_cols");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "vertical") == 0) {
            if (!(supported2 & JO2_VERTICAL))
               break;
            opt->set1 |= JO2_VERTICAL;
            opt->vertical = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "curPor") == 0) {
            if (!(supported2 & JO2_CURPOR))
               break;
            opt->set1 |= JO2_CURPOR;
            opt->curPor = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "bufnr") == 0) {
            if (!(supported2 & JO2_CURPOR))
               break;
            opt->set1 |= JO2_BUFNR;
            int nr = tv_get_number(item);
            if (nr <= 0) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), hi->hi_key, tv_get_string(item));
               return FAIL;
            }
            opt->jo_bufnr_buf = bookFindFileByBookNr(nr);
            if (opt->jo_bufnr_buf == NULL) {
               showErrFmtMsg(_(e_book_nr_does_not_exist), (long)nr);
               return FAIL;
            }
            if (opt->jo_bufnr_buf->countPortals == 0 || opt->jo_bufnr_buf->term == NULL) {
               showErrFmtMsg(_(e_invalid_argument_str), "bufnr");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "hidden") == 0) {
            if (!(supported2 & JO2_HIDDEN))
               break;
            opt->set1 |= JO2_HIDDEN;
            opt->jo_hidden = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "norestore") == 0) {
            if (!(supported2 & JO2_NORESTORE))
               break;
            opt->set1 |= JO2_NORESTORE;
            opt->jo_term_norestore = tv_get_bool(item);
         } ei (STRCMP(hi->hi_key, "term_kill") == 0) {
            if (!(supported2 & JO2_TERM_KILL))
               break;
            opt->set1 |= JO2_TERM_KILL;
            opt->jo_term_kill = convertVarToString(item,
                               opt->jo_term_kill_buf);
            if (opt->jo_term_kill == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_kill");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "tty_type") == 0) {
            if (!(supported2 & JO2_TTY_TYPE))
               break;
            opt->set1 |= JO2_TTY_TYPE;
            CS p = convertVarToStringSingleUse(item);
            if (p == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "tty_type");
               return FAIL;
            }
            // Allow empty string, "winpty", "conpty".
            if (!(*p == ZERO || STRCMP(p, "winpty") == 0 || STRCMP(p, "conpty") == 0)) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "tty_type");
               return FAIL;
            }
            opt->jo_tty_type = p[0];
         } ei (STRCMP(hi->hi_key, "term_highlight") == 0) {
            if (!(supported2 & JO2_TERM_HIGHLIGHT))
                break;
            opt->set1 |= JO2_TERM_HIGHLIGHT;
            CS p = convertVarToString(item, opt->jo_term_highlight_buf);
            if (!p || *p == ZERO) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_highlight");
                return FAIL;
            }
            opt->jo_term_highlight = p;
         } ei (STRCMP(hi->hi_key, "term_api") == 0) {
            if (!(supported2 & JO2_TERM_API))
                break;
            opt->set1 |= JO2_TERM_API;
            opt->jo_term_api = convertVarToString(item, opt->jo_term_api_buf);
            if (opt->jo_term_api == NULL) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "term_api");
                return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "env") == 0) {
            if (!(supported2 & JO2_ENV))
                break;
            if (item->tag != VAR_BAG) {
                showErrFmtMsg(_(e_invalid_value_for_argument_str), "env");
                return FAIL;
            }
            opt->set1 |= JO2_ENV;
            opt->env = item->bag;
            if (opt->env)
               ++opt->env->refCount;
         } ei (STRCMP(hi->hi_key, "cwd") == 0) {
            if (!(supported2 & JO2_CWD))
               break;
            opt->currentWorkingDir = convertVarToString(item, opt->cwdText);
            if (!opt->currentWorkingDir || !mch_isdir(opt->currentWorkingDir)
                  || mch_access(opt->currentWorkingDir, X_OK) != 0
            ){
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "cwd");
               return FAIL;
            }
            opt->set1 |= JO2_CWD;
         } ei (STRCMP(hi->hi_key, "waittime") == 0) {
            if (!(supported & JO_WAITTIME))
               break;
            opt->set |= JO_WAITTIME;
            opt->jo_waittime = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "timeout") == 0) {
            if (!(supported & JO_TIMEOUT))
               break;
            opt->set |= JO_TIMEOUT;
            opt->jo_timeout = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "out_timeout") == 0) {
            if (!(supported & JO_OUT_TIMEOUT))
               break;
            opt->set |= JO_OUT_TIMEOUT;
            opt->jo_out_timeout = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "err_timeout") == 0) {
            if (!(supported & JO_ERR_TIMEOUT))
               break;
            opt->set |= JO_ERR_TIMEOUT;
            opt->jo_err_timeout = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "part") == 0) {
            if (!(supported & JO_PART))
               break;
            opt->set |= JO_PART;
            val = tv_get_string(item);
            if (STRCMP(val, "err") == 0)
               opt->part = PART_ERR;
            ei (STRCMP(val, "out") == 0)
               opt->part = PART_OUT;
            else {
               showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "part", val);
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "id") == 0) {
            if (!(supported & JO_ID))
               break;
            opt->set |= JO_ID;
            opt->id = tv_get_number(item);
         } ei (STRCMP(hi->hi_key, "stoponexit") == 0) {
            if (!(supported & JO_STOPONEXIT))
               break;
            opt->set |= JO_STOPONEXIT;
            opt->jo_stoponexit = convertVarToString(item, opt->jo_stoponexit_buf);
            if (opt->jo_stoponexit == NULL) {
               showErrFmtMsg(_(e_invalid_value_for_argument_str), "stoponexit");
               return FAIL;
            }
         } ei (STRCMP(hi->hi_key, "block_write") == 0) {
            if (!(supported & JO_BLOCK_WRITE))
               break;
            opt->set |= JO_BLOCK_WRITE;
            opt->jo_block_write = tv_get_number(item);
         } else
            break;
         --todo;
      }
   } 
   if (todo > 0) {
      showErrFmtMsg(_(e_invalid_argument_str), hi->hi_key);
      return FAIL;
   }

   return OK;
}

private Job* firstJobS = NULL;

private void
job_free_contents(Job* job) {
   ch_log(job->channel, "Freeing job");
   if (job->channel) {
      //The link from the channel to the job doesn't count as a reference, thus don't decrement 
      //the refcount of the job. The reference from the job to the channel does count the 
      //reference, decrement it and NULL the reference.  We don't set job_killed, unreferencing the
      //job doesn't mean it stops running.
      job->channel->job = NULL;
      channel_unref(job->channel);
   }
   mch_clear_job(job);

   eeglFree(job->ttyIn);
   eeglFree(job->ttyOut);
   eeglFree(job->jv_stoponexit);
   eeglFree(job->jv_termsig);
   evFreeCallback(&job->exitCb);
   if (job->argv) {
      for (int i = 0; job->argv[i] != NULL; i++)
         eeglFree(job->argv[i]);
      eeglFree(job->argv);
   }
}

// Remove "job" from the list of jobs.
private void
job_unlink(Job* job) {
   if (job->next)
      job->next->prev = job->prev;
   if (!job->prev)
      firstJobS = job->next;
   else
      job->prev->next = job->next;
}

private void
job_free_job(Job* job) {
   job_unlink(job);
   eeglFree(job);
}

private void
job_free(Job* job) {
   if (in_free_unref_items)
      return;

   job_free_contents(job);
   job_free_job(job);
}

private Arr(Job) jobs_to_free = NULL;

// Put "job" on a list to be freed later, when it's no longer referenced.
private void
job_free_later(Job* job) {
   job_unlink(job);
   job->next = jobs_to_free;
   jobs_to_free = job;
}

private void
free_jobs_to_free_later(void) {
   Job* job;

   while (jobs_to_free) {
      job = jobs_to_free;
      jobs_to_free = job->next;
      job_free_contents(job);
      eeglFree(job);
   }
}

#if defined(EXITFREE) || defined(PROTO)
void
job_free_all(void) {
   while (firstJobS)
      job_free(firstJobS);
   free_jobs_to_free_later();

   free_unused_terminals();
}
#endif

// true if we need to check if the process of "job" has ended.
private int
job_need_end_check(Job* job) {
   return job->status == JOB_STARTED && (job->jv_stoponexit || job->exitCb.name);
}

// true if the channel of "job" is still useful.
private int
job_channel_still_useful(Job* job) {
   return job->channel != NULL && channel_still_useful(job->channel);
}

// true if the channel of "job" is closeable.
private int
job_channel_can_close(Job* job) {
   return job->channel != NULL && channel_can_close(job->channel);
}

// Return true if the job should not be freed yet.  Do not free the job when
// it has not ended yet and there is a "stoponexit" flag, an exit callback
// or when the associated channel will do something with the job output.
private int
job_still_useful(Job* job) {
    return job_need_end_check(job) || job_channel_still_useful(job);
}

#if defined(GUI_MAY_FORK) || defined(PROTO)
// Return true when there is any running job that we care about.
int
job_any_running(void) {
   Job* job;
   FOR_ALL_JOBS(job) {
      if (job_still_useful(job)) {
         lo("GUI not forking because a job is running");
         return true;
      }
   } 
   return false;
}
#endif

//NOTE: Must call job_cleanup() only once right after the status of "job"
//changed to JOB_ENDED (i.e. after job_status() returned "dead" first or
//mch_detect_ended_job() returned non-NULL).
//If the job is no longer used it will be removed from the list of jobs, and deleted a bit later.
private void
job_cleanup(Job* job) {
   if (job->status != JOB_ENDED)
      return;

   // Ready to cleanup the job.
   job->status = JOB_FINISHED;

   // When only channel-in is kept open, close explicitly.
   if (job->channel)
      ch_close_part(job->channel, PART_IN);

   if (job->nativeCb) { // call the native callback first
      (*job->nativeCb)();
   }
   if (job->exitCb.name) { // call the script callback
      Var argv[3];
      Var returnVar;

      // Invoke the exit callback. Make sure the refcount is > 0.
      
      ch_log(job->channel, "Invoking exit callback %s", job->exitCb.name);
      incRefCount(job);
      argv[0].tag = VAR_JOB;
      argv[0].job = job;
      argv[1].tag = VAR_NUMBER;
      argv[1].number = job->exitVal;
      call_callback(&job->exitCb, -1, &returnVar, 2, argv);
      clearVar(&returnVar);
      decRefCount(job);
      channel_need_redraw = true;
   }

   if (job->channel && job->channel->ch_anonymous_pipe)
      job->channel->isBeingKilled = true;

   //Do not free the job in case the close callback of the associated channel
   //isn't invoked yet and may get information by job_info().
   if (job->refCount == 0 && !job_channel_still_useful(job))
      //The job was already unreferenced and the associated channel was
      //detached, now that it ended it can be freed. However, a caller might
      //still use it, thus free it a bit later.
      job_free_later(job);
}

// Mark references in jobs that are still useful.
int
set_ref_in_job(int copyID) {
   int abort = false;
   Var tv;

   for (Job* job = firstJobS; !abort && job != NULL; job = job->next) {
      if (job_still_useful(job)) {
         tv.tag = VAR_JOB;
         tv.job = job;
         abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
      }
   } 
   return abort;
}

// Dereference "job".  Note that after this "job" may have been freed.
void
job_unref(Job* job) {
   if (!job || --job->refCount > 0)
      return;

   //Do not free the job if there is a channel where the close callback may get the job info.
   if (job_channel_still_useful(job))
      return;

   //Do not free the job when it has not ended yet and there is a
   //"stoponexit" flag or an exit callback.
   if (!job_need_end_check(job)) {
      job_free(job);
   } ei (job->channel != NULL) {
      //Do remove the link to the channel, otherwise it hangs
      //around until Eegl exits. See job_free() for refcount.
      ch_log(job->channel, "detaching channel from job");
      job->channel->job = NULL;
      channel_unref(job->channel);
      job->channel = NULL;
   }
}

int
free_unused_jobs_contents(int copyID, int mask) {
   int did_free = false;
   Job* job;

   FOR_ALL_JOBS(job) {
      if ((job->copyId & mask) != (copyID & mask) && !job_still_useful(job)) {
         // Free the channel and ordinary items it contains, but don't
         // recurse into Lists, Dictionaries etc.
         job_free_contents(job);
         did_free = true;
      }
   }
   return did_free;
}

void
free_unused_jobs(int copyID, int mask) {
   Job* job_next;

   for (Job* job = firstJobS; job; job = job_next) {
      job_next = job->next;
      if ((job->copyId & mask) != (copyID & mask) && !job_still_useful(job)) {
         // Free the job struct itself.
         job_free_job(job);
      }
   }
}

// Allocate a job. Sets the refcount to one and sets options default.
Job *
job_alloc(void) {
   Job* job = ALLOC_CLEAR_ONE(Job);
   job->refCount = 1;
   job->jv_stoponexit = copyStr(S"term");

   if (firstJobS) {
      firstJobS->prev = job;
      job->next = firstJobS;
   }
   firstJobS = job;
   return job;
}

void
job_set_options(Job* job, JobOptions* opt) {
   if ((opt->set & JO_STOPONEXIT) != 0) {
      eeglFree(job->jv_stoponexit);
      if (!opt->jo_stoponexit || *opt->jo_stoponexit == ZERO)
         job->jv_stoponexit = NULL;
      else
         job->jv_stoponexit = copyStr(opt->jo_stoponexit);
   }
   if (opt->set & JO_EXIT_CB) {
      evFreeCallback(&job->exitCb);
      if (!opt->exitCb.name || *opt->exitCb.name == ZERO) {
         job->exitCb.name = NULL;
         job->exitCb.cb_partial = NULL;
      } else
         evCopyCallback(&job->exitCb, &opt->exitCb);
   }
}

// Called when Eegl is exiting: kill all jobs that have the "stoponexit" flag.
void
job_stop_on_exit(void) {
   Job* job;

   FOR_ALL_JOBS(job) {
      if (job->status == JOB_STARTED && job->jv_stoponexit != NULL)
          mch_signal_job(job, job->jv_stoponexit);
   } 
}

// Return true when there is any job that has an exit callback and might exit,
// which means job_check_ended() should be called more often.
int
has_pending_job(void) {
   Job* job;

   FOR_ALL_JOBS(job) {
      //Only should check if the channel has been closed, if the channel is
      //open the job won't exit.
      if ((job->status == JOB_STARTED && !job_channel_still_useful(job))
             || (job->status == JOB_FINISHED && job_channel_can_close(job))
      )
         return true;
   }
   return false;
}

#define MAX_CHECK_ENDED 8

// Called once in a while: check if any jobs that seem useful have ended. true if a job did end.
int
job_check_ended(void) {
   int did_end = false;

   // be quick if there are no jobs to check
   if (!firstJobS)
      return did_end;

   for (int i = 0; i < MAX_CHECK_ENDED; ++i) {
      // NOTE: mch_detect_ended_job() must only return a job of which the
      // status was just set to JOB_ENDED.
      Job* job = mch_detect_ended_job(firstJobS);
      if (!job)
         break;
         
      did_end = true;
      job_cleanup(job); // may add "job" to jobs_to_free
   }

   // Actually free jobs that were cleaned up.
   free_jobs_to_free_later();

   if (channel_need_redraw) {
      channel_need_redraw = false;
      redraw_after_callback(true, false);
   }
   return did_end;
}

// Create a job and return it.  Implements startJob().
// When "argv_arg" is NULL then "argvars" is used. The returned job has a refcount of one.
// Return NULL when out of memory.
Job*
startJob(Arr(Var) argvars, Byte** argv_arg, JobOptions* opt_arg, Job** term_job) {
   Byte** argv = NULL;
   int argc = 0;
   int i;
   ArrayList   ga;
   JobOptions   opt;
   ChannelFdKind   part;

   Job* job = job_alloc();

   job->status = JOB_FAILED;
   ga_init2(&ga, sizeof(char*), 20);

   if (opt_arg)
      opt = *opt_arg;
   else {
      // Default mode is NL.
      CLEAR_POINTER(&opt);
      opt.mode = CH_MODE_NL;
      if (get_job_options(&argvars[1], OUT &opt,
             JO_MODE_ALL + JO_CB_ALL + JO_TIMEOUT_ALL + JO_STOPONEXIT + JO_EXIT_CB 
                + JO_OUT_IO + JO_BLOCK_WRITE,
             JO2_ENV + JO2_CWD
         ) == FAIL
      ) {
         goto theend;
      } 
   }

   // Check that when io is "file" that there is a file name.
   for (part = PART_OUT; part < PART_COUNT; ++part) {
      if ((opt.set & (JO_OUT_IO << (part - PART_OUT))
            && opt.ioMode[part] == JIO_FILE
            && (!(opt.set & (JO_OUT_NAME << (part - PART_OUT)))
                   || *opt.name[part] == ZERO))
      ){
         emsg(_(e_io_file_requires_name_to_be_set));
         goto theend;
      }
   } 

   if ((opt.set & JO_IN_IO) && opt.ioMode[PART_IN] == JIO_BUFFER) {
      Book* book = NULL;

      // check that we can find the book before starting the job
      if (opt.set & JO_IN_BUF) {
         book = bookFindFileByBookNr(opt.ioText[PART_IN]);
         if (!book)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)opt.ioText[PART_IN]);
      } ei (!(opt.set & JO_IN_NAME)) {
         emsg(_(e_in_io_buffer_requires_in_buf_or_in_name_to_be_set));
      } else
         book = bookFindByName(opt.name[PART_IN], false);
      if (!book)
          goto theend;
      if (book->mem.mfile == NULL) {
         Byte   numbuf[NUMBUFLEN];
         CS s;

         if (opt.set & JO_IN_BUF) {
            sprintf((char *)numbuf, "%d", opt.ioText[PART_IN]);
            s = numbuf;
         } else
            s = opt.name[PART_IN];
         showErrFmtMsg(_(e_buffer_must_be_loaded_str), s);
         goto theend;
      }
      job->inBook = book;
   }

   job_set_options(job, &opt);

   if (argv_arg) {
      // Make a copy of argv_arg for job->argv.
      for (i = 0; argv_arg[i] != NULL; i++)
         argc++;
      argv = ALLOC_MULT(CS, argc + 1);
      for (i = 0; i < argc; i++)
         argv[i] = copyStr((CS)argv_arg[i]);
      argv[argc] = NULL;
   } ei (argvars[0].tag == VAR_STRING) {
      // Command is a string.
      
      emsg(_(e_invalid_argument));
   } ei (argvars[0].tag != VAR_LIST || argvars[0].list == NULL || argvars[0].list->len < 1){
      emsg(_(e_invalid_argument));
      goto theend;
   } else {
      List* l = argvars[0].list;
      if (build_argv_from_list(l, &argv, &argc) == FAIL)
         goto theend;

      //Empty command is invalid.
      if (argc == 0 || *skipwhite((CS)argv[0]) == ZERO) {
         emsg(_(e_invalid_argument));
         goto theend;
      }
   }

   job->nativeCb = opt_arg->finishNativeCb;
   //Save the command used to start the job.
   job->argv = argv;

   if (term_job)
      *term_job = job;

   if (ch_log_active()) {
      ArrayList ga;

      ga_init2(&ga, sizeof(char), 200);
      for (i = 0; i < argc; ++i) {
         if (i > 0)
            ga_concat(&ga, (CS)"  ");
         ga_concat(&ga, (CS)argv[i]);
      }
      ga_append(&ga, ZERO);
      lo("Starting job: %s", (char *)ga.c);
      ga_clear(&ga);
   }
   mch_job_start(argv, job, &opt, term_job != NULL);
   // If the channel is reading from a buffer, write lines now.
   if (job->channel)
      channel_write_in(job->channel);

theend:
   if (argv && argv != job->argv) {
      for (i = 0; argv[i] != NULL; i++)
         eeglFree(argv[i]);
      eeglFree(argv);
   }
   free_job_options(&opt);
   return job;
}

//Get the status of "job" and invoke the exit callback when needed.
//The returned string is not allocated.
CS
job_status(Job* job) {
   CS result;

   if (job->status >= JOB_ENDED)
      // No need to check, dead is dead.
      result = S"dead";
   ei (job->status == JOB_FAILED)
      result = S"fail";
   else {
      result = mch_job_status(job);
      if (job->status == JOB_ENDED)
         job_cleanup(job);
   }
   return result;
}

// Send a signal to "job".  Implements job_stop(). When "type" is not NULL use this for the type.
// Otherwise use argvars[1] for the type.
int
job_stop(Job* job, Arr(Var) argvars, CS type) {
   CS arg;

   if (type)
      arg = (CS)type;
   ei (argvars[1].tag == VAR_UNKNOWN)
      arg = S"";
   else {
      arg = convertVarToStringSingleUse(&argvars[1]);
      if (!arg) {
         emsg(_(e_invalid_argument));
         return 0;
      }
   }
   if (job->status == JOB_FAILED) {
      ch_log(job->channel, "Job failed to start, job_stop() skipped");
      return 0;
   }
   if (job->status == JOB_ENDED) {
      ch_log(job->channel, "Job has already ended, job_stop() skipped");
      return 0;
   }
   ch_log(job->channel, "Stopping job with '%s'", (char *)arg);
   if (mch_signal_job(job, arg) == FAIL)
      return 0;

   //Assume that only "kill" will kill the job.
   if (job->channel != NULL && STRCMP(arg, "kill") == 0)
      job->channel->isBeingKilled = true;

   //We don't try freeing the job, obviously the caller still has a reference to it.
   return 1;
}

void
invoke_prompt_callback(void) {
   Var argv[2];
   LineNr lnum = curBook->mem.lineCount;

   //Add a new line for the prompt before invoking the callback, so that
   //text can always be inserted above the last line.
   ml_append(lnum, (Byte  *)"", 0, false);
   curPor->cursor.lnum = lnum + 1;
   curPor->cursor.col = 0;

   if (curBook->promptCallback.name == NULL || *curBook->promptCallback.name == ZERO)
      return;
   CS text = ml_get(lnum);
   CS prompt = prompt_text();
   if (STRLEN(text) >= STRLEN(prompt))
      text += STRLEN(prompt);
   argv[0].tag = VAR_STRING;
   argv[0].string = copyStr(text);
   argv[1].tag = VAR_UNKNOWN;

   Var returnVar;
   call_callback(&curBook->promptCallback, -1, &returnVar, 1, argv);
   clearVar(&argv[0]);
   clearVar(&returnVar);
}

// Return true when the interrupt callback was invoked.
int
invoke_prompt_interrupt(void) {
   Var returnVar;
   Var argv[1];
   int ret;

   if (curBook->promptInterrupt.name == NULL || *curBook->promptInterrupt.name == ZERO)
      return false;
   argv[0].tag = VAR_UNKNOWN;

   gotInterruptG = false; // don't skip executing commands
   ret = call_callback(&curBook->promptInterrupt, -1, &returnVar, 0, argv);
   clearVar(&returnVar);
   return ret == FAIL ? false : true;
}

// Return the effective prompt for the specified book.
private CS
buf_prompt_text(Book* book) {
   if (!book->promptText)
      return S"% ";
   return book->promptText;
}

// Return the effective prompt for the current book.
CS
prompt_text(void) {
   return buf_prompt_text(curBook);
}


//Prepare for prompt mode: Make sure the last line has the prompt text.
//Move the cursor to this line.
void
init_prompt(int cmdchar_todo) {
   CS prompt = prompt_text();
   curPor->cursor.lnum = curBook->mem.lineCount;
   CS text = ml_get_curline();
   if (STRNCMP(text, prompt, STRLEN(prompt)) != 0) {
      // prompt is missing, insert it or append a line with it
      if (*text == ZERO)
         ml_replace(curBook->mem.lineCount, prompt, true);
      else
         ml_append(curBook->mem.lineCount, prompt, 0, false);
      curPor->cursor.lnum = curBook->mem.lineCount;
      coladvance((ColNr)MAXCOL);
      changed_bytes(curBook->mem.lineCount, 0);
   }

   // Insert always starts after the prompt, allow editing text after it.
   if (insertStartOrigG.lnum != curPor->cursor.lnum
               || insertStartOrigG.col != (int)STRLEN(prompt))
      set_insstart(curPor->cursor.lnum, (int)STRLEN(prompt));

   if (cmdchar_todo == 'A')
      coladvance((ColNr)MAXCOL);
   if (curPor->cursor.col < (int)STRLEN(prompt))
      curPor->cursor.col = (int)STRLEN(prompt);
   // Make sure the cursor is in a valid position.
   check_cursor();
}

// Return true if the cursor is in the editable position of the prompt line.
int
prompt_curpos_editable(void) {
   return curPor->cursor.lnum == curBook->mem.lineCount 
      && curPor->cursor.col >= (int)STRLEN(prompt_text());
}

// "prompt_setcallback({buffer}, {callback})" function
void
f_prompt_setcallback(Arr(Var) argvars, Var* returnVar UNUSED) {
   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   Callback callback = get_callback(&argvars[1]);
   if (!callback.name)
      return;

   evFreeCallback(&book->promptCallback);
   set_callback(&book->promptCallback, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);
}

// "prompt_setinterrupt({buffer}, {callback})" function
void
f_prompt_setinterrupt(Arr(Var) argvars, Var* returnVar UNUSED) {
   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   Callback callback = get_callback(&argvars[1]);
   if (!callback.name)
      return;

   evFreeCallback(&book->promptInterrupt);
   set_callback(&book->promptInterrupt, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);
}


// "prompt_getprompt({buffer})" function
void
f_prompt_getprompt(Arr(Var) argvars, Var* returnVar) {
   // return an empty string by default, e.g. it's not a prompt buffer
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   Book* book = daGetBookFromArg(&argvars[0]);
   if (!book)
      return;

   if (!bt_prompt(book))
      return;

   returnVar->string = copyStr(buf_prompt_text(book));
}

// "prompt_setprompt({book}, {text})" function
void
f_prompt_setprompt(Arr(Var) argvars, Var* returnVar UNUSED) {
   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   CS text = tv_get_string(&argvars[1]);
   eeglFree(book->promptText);
   book->promptText = copyStr(text);
}

// Get the job from the argument. Returns NULL if the job is invalid.
private Job *
get_job_arg(Var* tv) {
   if (tv->tag != VAR_JOB) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(tv));
      return NULL;
   }
   Job* job = tv->job;
   if (!job)
      emsg(_(e_not_valid_job));
      
   return job;
}

void
f_job_getchannel(Arr(Var) argvars, OUT Var* returnVar) {
   Job* job = get_job_arg(&argvars[0]);
   if (!job)
      return;

   returnVar->tag = VAR_CHANNEL;
   returnVar->channel = job->channel;
   if (job->channel != NULL)
      ++job->channel->refCount;
}

private void
job_info(Job* job, Bag* bag) {
   bagAddString(bag, S"status", job_status(job));

   DictItem* item = dictitem_alloc(tConst("channel"));
   item->c.tag = VAR_CHANNEL;
   item->c.channel = job->channel;
   if (job->channel)
      ++job->channel->refCount;
   if (bagAdd(bag, item) == FAIL)
      dictitem_free(item);

   Long nr = job->pid;
   bagAddNumber(bag, S"process", nr);
   bagAddString(bag, S"tty_in", job->ttyIn);
   bagAddString(bag, S"tty_out", job->ttyOut);

   bagAddNumber(bag, S"exitval", job->exitVal);
   bagAddString(bag, S"exit_cb", job->exitCb.name);
   bagAddString(bag, S"stoponexit", job->jv_stoponexit);
   bagAddString(bag, S"termsig", job->jv_termsig);

   List* l = list_alloc();

   bagAddList(bag, S"cmd", l);
   if (job->argv) {
      for (int i = 0; job->argv[i]; i++)
         list_append_string(l, (CS)job->argv[i], -1);
   } 
}

private void
job_info_all(List* l) {
   Var tv;

   Job* job;
   FOR_ALL_JOBS(job) {
      tv.tag = VAR_JOB;
      tv.job = job;

      if (list_append_tv(l, &tv) != OK)
         return;
   }
}

void
f_job_info(Var* argvars, Var* returnVar) {
   if (argvars[0].tag != VAR_UNKNOWN) {
      Job* job = get_job_arg(&argvars[0]);
      if (job) {
         allocReturnDict(returnVar);
         job_info(job, returnVar->bag);
      } 
   } else {
      allocReturnList(returnVar);
      job_info_all(returnVar->list);
   } 
}

void
f_job_setoptions(Arr(Var) argvars, Var* returnVar UNUSED) {
   Job* job = get_job_arg(&argvars[0]);
   if (!job)
      return;
      
   JobOptions   opt;
   CLEAR_POINTER(&opt);
   if (get_job_options(&argvars[1], OUT &opt, JO_STOPONEXIT + JO_EXIT_CB, 0) == OK)
      job_set_options(job, &opt);
   free_job_options(&opt);
}

void
f_startJob(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_JOB;
   returnVar->job = startJob(argvars, NULL, NULL, NULL);
}

void
f_job_status(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_JOB && argvars[0].job == NULL) {
      // A job that never started returns "fail".
      returnVar->tag = VAR_STRING;
      returnVar->string = copyStr(S"fail");
   } else {
      Job* job = get_job_arg(&argvars[0]);
      if (job) {
          returnVar->tag = VAR_STRING;
          returnVar->string = copyStr(job_status(job));
      }
   }
}

void
f_job_stop(Arr(Var) argvars, Var* returnVar) {
   Job* job = get_job_arg(&argvars[0]);
   if (job)
      returnVar->number = job_stop(job, argvars, NULL);
}

// Get a string with information about the job in "varp" into "builder".
// "builder" must be at least NUMBUFLEN long.
void
job_to_string_buf(OUT CS builder, Var* varp) {
   Job *job = varp->job;
   if (!job) {
      eeSnprintf(builder, NUMBUFLEN, "no process");
      return;
   }
   CS status = (CS)(job->status == JOB_FAILED 
      ? "fail"
      : (job->status >= JOB_ENDED ? "dead" : "run")
   );
   eeSnprintf(builder, NUMBUFLEN, "process %ld %s", (long)job->pid, status);
}

//}}}
//{{{command-line arguments

//Construct an array of strings that spell out `bash -c "bla bla"`
void
unix_build_argv(OUT Polystring* shellArgs, CS cmd) {
   if (!cmd)
      return;
      
   Unt count = 1;
   appendToBuf(tConst("bash"), OUT shellArgs);
   appendToBuf(tConst("-c"), OUT shellArgs);
   appendToBuf(text(cmd), OUT shellArgs);
}

//}}}
//{{{logging

// Implements logging.  Originally intended for the channel feature, which is
// why the "ch_" prefix is used.  Also useful for any kind of low-level and async debugging.

// Log file opened with ch_logfile().
private FILE* log_fd = NULL;
private CS log_name = NULL;
private ProfTime log_start;

void
ch_logfile(CS fname, CS opt) {
   FILE* file = NULL;
   CS mode = S"a";

   if (log_fd) {
      if (*fname != ZERO)
         lo("closing this logfile, opening %s", fname);
      else
         lo("closing logfile %s", log_name);
      fclose(log_fd);
   }

   // The "a" flag overrules the "w" flag.
   if (firstOccurrence(opt, 'a') == NULL && firstOccurrence(opt, 'w') != NULL)
      mode = S"w";
   ch_log_output = firstOccurrence(opt, 'o') != NULL ? LOG_ALWAYS : false;

   if (*fname != ZERO) {
      file = FOPEN(fname, mode);
      if (file == NULL) {
         showErrFmtMsg(_(e_cant_open_file_str), fname);
         return;
      }
      eeglFree(log_name);
      log_name = copyStr(fname);
   }
   log_fd = file;

   if (log_fd) {
      fprintf(log_fd, "==== start log session %s ====\n", get_ctime(time(NULL), false));
      // flush now, if fork/exec follows it could be written twice
      fflush(log_fd);
      profile_start(&log_start);
   }
}

int
ch_log_active(void) {
   return log_fd != NULL;
}

private void
logLead(CS what, Channel* ch, ChannelFdKind part) {
   if (!log_fd)
      return;

   ProfTime log_now;
   profile_start(&log_now);
   profile_sub(&log_now, &log_start);
   fprintf(log_fd, "%s ", profile_msg(&log_now));
   if (ch != NULL) {
      if (part < PART_COUNT)
         fprintf(log_fd, "%son %d(%s): ", what, ch->id, chanFdNames[part]);
      else
         fprintf(log_fd, "%son %d: ", what, ch->id);
   } else
      fprintf(log_fd, "%s: ", what);
}

#ifndef PROTO  // prototype is in eegl.h

void
ch_log(Channel* ch, char const* fmt, ...) {
   if (!log_fd)
      return;

   va_list ap;

   logLead(S"", ch, PART_COUNT);
   va_start(ap, fmt);
   vfprintf(log_fd, fmt, ap);
   va_end(ap);
   fputc('\n', log_fd);
   fflush(log_fd);
   did_repeated_msg = 0;
}

void
lo(char const* fmt, ...) {
   if (!log_fd)
      return;

   va_list ap;

   logLead(S"", null, PART_COUNT);
   va_start(ap, fmt);
   vfprintf(log_fd, fmt, ap);
   va_end(ap);
   fputc('\n', log_fd);
   fflush(log_fd);
   did_repeated_msg = 0;
}

void
ch_error(Channel* ch, char const* fmt, ...) {
   if (log_fd == NULL)
      return;

   va_list ap;

   logLead(S"ERR ", ch, PART_COUNT);
   va_start(ap, fmt);
   vfprintf(log_fd, fmt, ap);
   va_end(ap);
   fputc('\n', log_fd);
   fflush(log_fd);
   did_repeated_msg = 0;
}
#endif

//Log a message "builder[len]" for channel "ch" part "part".
//Only to be called when ch_log_active() returns true.
private void
ch_log_literal(CS lead, Channel* ch, ChannelFdKind part, OUT Text builder) {
   logLead(lead, ch, part);
   fprintf(log_fd, "'");
   (void)fwrite(builder.c, builder.len, 1, log_fd);
   fprintf(log_fd, "'\n");
   fflush(log_fd);
}

void
f_ch_log(Arr(Var) argvars, Var* returnVar UNUSED) {
   Channel	*channel = NULL;
   CS msg = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN)
      channel = get_channel_arg(&argvars[1], false, false, 0);

   // Prepend "ch_log()" to make it easier to find these entries in the logfile.
   ch_log(channel, "ch_log(): %s", msg);
}

void
f_ch_logfile(Arr(Var) argvars, Var* returnVar UNUSED) {
   Byte builder[NUMBUFLEN];
   CS fname = tv_get_string(&argvars[0]);
   CS opt = (argvars[1].tag == VAR_STRING) ? tv_get_string_buf(&argvars[1], builder) : S"";
   ch_logfile(fname, opt);
}

//}}}
