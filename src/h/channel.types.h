GEN_TYPE_L(PollFd);
typedef struct {
   int fd;       // socket/stdin/stdout/stderr, -1 if not used

   int pollIdx;   // used by channel_poll_setup()
   ChannelMode ch_mode;
   JobIoMode ch_io;
   int ch_timeout;   // request timeout in msec

   ReadChunk head;   // header for circular raw read queue
   JsonQ ch_json_head;   // header for circular json read queue
   ArrayList ch_block_ids;   // list of IDs that channel_read_json_block() is waiting for
   // When ch_wait_len is non-zero use deadline to wait for incomplete message to be complete. 
   // The value is the length of the incomplete message when the deadline was set.  If it gets 
   // longer (something was received) the deadline is reset.
   Unt ch_wait_len;
   TimeVal deadline;
   int ch_block_write; // for testing: 0 when not used, -1 when write
                       // does not block, 1 simulate blocking
   int ch_nonblocking; // write() is non-blocking
   WriteQueue ch_writeque;   // header for write queue

   CbNode ch_cb_head;   // dummy node for per-request callbacks
   void (*nativeCb)(Arr(Byte));
   Callback ch_callback;   // call when a msg is not handled

   BookRef bookref;   // book to read from or write to
   int ch_nomodifiable; // TRUE when book can be not 'modifiable'
   int ch_nomod_error;   // TRUE when e_modifiable was given
   int ch_buf_append;   // write appended lines instead top-bot
   LineNr ch_buf_top;   // next line to send
   LineNr ch_buf_bot;   // last line to send
} ChannelFd;
struct Channel {
   Channel* next;
   Channel* prev;

   int id;      // ID of the channel
   int lastMsgId;   // ID of the last message
   CS socketName;      //Unix domain socket name
   ChannelFd fds[PART_COUNT]; // info for socket, out, err and in
   int writeTextMode; // write book lines with CR, not NL

   Boole ch_to_be_closed; // bitset of readable fds to be closed.
            // When all readable fds have been closed, set to (1 << PART_COUNT).
   Boole ch_to_be_freed; // When TRUE, channel must be freed when it's safe to invoke callbacks
   int error;   //When TRUE an error was reported.  Avoids giving pages full of error 
                //messages when the other side has exited, only mention the first error 
                //until the connection works again.

   Callback ch_callback;   // call when any msg is not handled
   Callback ch_close_cb;   // call when channel is closed
   int ch_drop_never;
   int ch_keep_open;   // do not close on read error
   int ch_nonblock;

   Job* job;   // Job that uses this channel; this does not count as a reference to avoid a 
                  // circular reference, the job refers to the channel.
   int ch_job_killed;   // TRUE when there was a job and it was killed or we know it died.
   int ch_anonymous_pipe;  // ConPTY
   int isBeingKilled;       // TerminateJobObject() was called

   Unt refCount;   // reference count
   int copyId;
};
#define MAX_OPEN_CHANNELS 16
