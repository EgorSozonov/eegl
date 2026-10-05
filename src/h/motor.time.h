void time_push(TimeSpec* tv_rel, TimeSpec* tv_start);
void time_pop(TimeSpec* tp);
void time_msg(CS mesg, TimeSpec* tv_start);
Long motElapsedMs(TimeSpec since);
Long time_diff_ms(TimeSpec* t0, TimeSpec* t1);
Long motTimeDiffMs(TimeSpec* since, TimeSpec* to);
