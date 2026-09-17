typedef enum {
   FLUSH_MINIMAL,
   FLUSH_TYPEAHEAD,   // flush current typebuf contents
   FLUSH_INPUT      // flush typebuf and inchar() input
} FlushBuffers;
