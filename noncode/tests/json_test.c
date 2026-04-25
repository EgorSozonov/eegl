/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

//## json_test.c: Unittests for the JSON part of var.c

#undef NDEBUG
#include <assert.h>

// Must include main.c because it contains much more than just main()
#define NO_VIM_MAIN
#include "main.c"

// This file has to be included because the tested functions are static
#include "var.c"

/*
 * Test json_find_end() with incomplete items.
 */
    static void
test_decode_find_end(void)
{
    js_read_T reader;

    reader.js_fill = NULL;
    reader.js_used = 0;

    // string and incomplete string
    reader.js_buf = (Byte *)"\"hello\"";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"  \"hello\" ";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"\"hello";
    assert(json_find_end(&reader, 0) == MAYBE);

    // number and dash (incomplete number)
    reader.js_buf = (Byte *)"123";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"-";
    assert(json_find_end(&reader, 0) == MAYBE);

    // false, true and null, also incomplete
    reader.js_buf = (Byte *)"false";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"f";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"fa";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"fal";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"fals";
    assert(json_find_end(&reader, 0) == MAYBE);

    reader.js_buf = (Byte *)"true";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"t";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"tr";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"tru";
    assert(json_find_end(&reader, 0) == MAYBE);

    reader.js_buf = (Byte *)"null";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"n";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"nu";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"nul";
    assert(json_find_end(&reader, 0) == MAYBE);

    // object without white space
    reader.js_buf = (Byte *)"{\"a\":123}";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"{\"a\":123";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"{\"a\":";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"{\"a\"";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"{\"a";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"{\"";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"{";
    assert(json_find_end(&reader, 0) == MAYBE);

    // object with white space
    reader.js_buf = (Byte *)"  {  \"a\"  :  123  }  ";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"  {  \"a\"  :  123  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  {  \"a\"  :  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  {  \"a\"  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  {  \"a  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  {   ";
    assert(json_find_end(&reader, 0) == MAYBE);

    // JS object with white space
    reader.js_buf = (Byte *)"  {  a  :  123  }  ";
    assert(json_find_end(&reader, JSON_JS) == OK);
    reader.js_buf = (Byte *)"  {  a  :   ";
    assert(json_find_end(&reader, JSON_JS) == MAYBE);

    // array without white space
    reader.js_buf = (Byte *)"[\"a\",123]";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"[\"a\",123";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"[\"a\",";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"[\"a\"";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"[\"a";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"[\"";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"[";
    assert(json_find_end(&reader, 0) == MAYBE);

    // array with white space
    reader.js_buf = (Byte *)"  [  \"a\"  ,  123  ]  ";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"  [  \"a\"  ,  123  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  [  \"a\"  ,  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  [  \"a\"  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  [  \"a  ";
    assert(json_find_end(&reader, 0) == MAYBE);
    reader.js_buf = (Byte *)"  [  ";
    assert(json_find_end(&reader, 0) == MAYBE);
}

    static int
fill_from_cookie(js_read_T *reader)
{
    reader->js_buf = reader->js_cookie;
    return TRUE;
}

/*
 * Test json_find_end with an incomplete array, calling the fill function.
 */
    static void
test_fill_called_on_find_end(void)
{
    js_read_T reader;

    reader.js_fill = fill_from_cookie;
    reader.js_used = 0;
    reader.js_buf = (Byte *)"  [  \"a\"  ,  123  ";
    reader.js_cookie =	      "  [  \"a\"  ,  123  ]  ";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"  [  \"a\"  ,  ";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"  [  \"a\"  ";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"  [  \"a";
    assert(json_find_end(&reader, 0) == OK);
    reader.js_buf = (Byte *)"  [  ";
    assert(json_find_end(&reader, 0) == OK);
}

/*
 * Test json_find_end with an incomplete string, calling the fill function.
 */
    static void
test_fill_called_on_string(void)
{
    js_read_T reader;

    reader.js_fill = fill_from_cookie;
    reader.js_used = 0;
    reader.js_buf = (Byte *)" \"foo";
    reader.js_end = reader.js_buf + STRLEN(reader.js_buf);
    reader.js_cookie =	      " \"foobar\"  ";
    assert(json_decode_string(&reader, NULL, '"') == OK);
}

    int
main(void)
{
    test_decode_find_end();
    test_fill_called_on_find_end();
    test_fill_called_on_string();
    return 0;
}
