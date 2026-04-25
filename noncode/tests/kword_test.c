/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

//## kword_test.c: Unittests for vim_iswordc() and vim_iswordp().

#undef NDEBUG
#include <assert.h>

// Must include main.c because it contains much more than just main()
#define NO_VIM_MAIN
#include "main.c"

// This file has to be included because the tested functions are static
#include "charset.c"

/*
 * Test the results of vim_iswordc() and vim_iswordp() are matched.
 */
    static void
test_isword_funcs_utf8(void)
{
    Buffer buf;
    int c;

    CLEAR_FIELD(buf);
    p_enc = (Byte *)"utf-8";
    p_isi = (Byte *)"";
    p_isp = (Byte *)"";
    p_isf = (Byte *)"";
    buf.b_p_isk = (Byte *)"@,48-57,_,128-167,224-235";

    curbuf = &buf;
    mb_init(); // calls init_chartab()

    for (c = 0; c < 0x10000; ++c)
    {
	Byte p[4] = {0};
	int c1;
	int retc;
	int retp;

	mb_char2bytes(c, p);
	c1 = mb_ptr2char(p);
	if (c != c1)
	{
	    fprintf(stderr, "Failed: ");
	    fprintf(stderr,
		    "[c = %#04x, p = {%#02x, %#02x, %#02x}] ",
		    c, p[0], p[1], p[2]);
	    fprintf(stderr, "c != mb_ptr2char(p) (=%#04x)\n", c1);
	    abort();
	}
	retc = vim_iswordc_buf(c, &buf);
	retp = vim_iswordp_buf(p, &buf);
	if (retc != retp)
	{
	    fprintf(stderr, "Failed: ");
	    fprintf(stderr,
		    "[c = %#04x, p = {%#02x, %#02x, %#02x}] ",
		    c, p[0], p[1], p[2]);
	    fprintf(stderr, "vim_iswordc(c) (=%d) != vim_iswordp(p) (=%d)\n",
		    retc, retp);
	    abort();
	}
    }
}

    int
main(void)
{
    estack_init();
    test_isword_funcs_utf8();
    return 0;
}
