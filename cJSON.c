/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
  This file has been patched to fix compiler warnings and bugs in the parsing logic.
*/

#if !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

#include "cJSON.h"

/* define our own boolean type */
typedef int cJSON_bool;
#define true ((cJSON_bool)1)
#define false ((cJSON_bool)0)

static const char *global_ep = NULL;

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

static unsigned char* cJSON_strdup(const unsigned char* str, size_t len)
{
    unsigned char* copy = (unsigned char*)cJSON_malloc(len + 1);
    if (copy == NULL)
    {
        return NULL;
    }
    memcpy(copy, str, len);
    copy[len] = '\0';

    return copy;
}

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks) { /* Reset hooks */
        cJSON_malloc = malloc;
        cJSON_free = free;
        return;
    }

    cJSON_malloc = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    cJSON_free   = (hooks->free_fn) ? hooks->free_fn : free;
}

/* Internal constructor. */
static cJSON *cJSON_New_Item(void)
{
    cJSON* node = (cJSON*)cJSON_malloc(sizeof(cJSON));
    if (node)
    {
        memset(node, '\0', sizeof(cJSON));
    }

    return node;
}

/* Delete a cJSON structure. */
void cJSON_Delete(cJSON *c)
{
    cJSON *next;
    while (c)
    {
        next = c->next;
        if (!(c->type & cJSON_IsReference) && c->child)
        {
            cJSON_Delete(c->child);
        }
        if (!(c->type & cJSON_IsReference) && c->valuestring)
        {
            cJSON_free(c->valuestring);
        }
        if (!(c->type & cJSON_StringIsConst) && c->string)
        {
            cJSON_free(c->string);
        }
        cJSON_free(c);
        c = next;
    }
}

/* Parse the input text to generate a number, and populate the result into item. */
static const unsigned char *parse_number(cJSON *item, const unsigned char *num)
{
    double n = 0, sign = 1, scale = 0;
    int subscale = 0, signsubscale = 1;

    if (*num == '-')
    {
        sign = -1;
        num++;
    }
    if (*num == '0')
    {
        num++;
    }
    if (*num >= '1' && *num <= '9')
    {
        do
        {
            n = (n * 10.0) + (*num++ - '0');
        } while (*num >= '0' && *num <= '9');
    }
    if (*num == '.' && num[1] >= '0' && num[1] <= '9')
    {
        num++;
        do
        {
            n = (n * 10.0) + (*num++ - '0');
            scale--;
        } while (*num >= '0' && *num <= '9');
    }
    if (*num == 'e' || *num == 'E')
    {
        num++;
        if (*num == '+')
        {
            num++;
        }
        else if (*num == '-')
        {
            signsubscale = -1;
            num++;
        }
        while (*num >= '0' && *num <= '9')
        {
            subscale = (subscale * 10) + (*num++ - '0');
        }
    }

    n = sign * n * pow(10.0, (scale + subscale * signsubscale));

    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return num;
}

/* calculate the next largest power of 2, but don't overflow */
static int pow2gt (int x)
{
    --x;
    x|=x>>1;
    x|=x>>2;
    x|=x>>4;
    x|=x>>8;
    x|=x>>16;
    return x+1;
}

typedef struct
{
    unsigned char *buffer;
    size_t length;
    size_t offset;
} printbuffer;

/* realloc printbuffer if necessary to have at least "needed" bytes more */
static unsigned char* ensure(printbuffer *p, size_t needed)
{
    unsigned char *newbuffer = NULL;
    size_t newsize = 0;

    if (!p || !p->buffer)
    {
        return NULL;
    }

    if ((p->length > 0) && (p->offset >= p->length))
    {
        /* make sure that offset is not overrunning buffer */
        return NULL;
    }

    if (needed > INT_MAX)
    {
        /* sizes bigger than INT_MAX are not handled */
        return NULL;
    }

    needed += p->offset + 1;
    if (needed <= p->length)
    {
        return p->buffer + p->offset;
    }

    newsize = pow2gt(needed);
    if (newsize > INT_MAX)
    {
        /* sizes bigger than INT_MAX are not handled */
        return NULL;
    }

    newbuffer = (unsigned char*)cJSON_malloc(newsize);
    if (!newbuffer)
    {
        cJSON_free(p->buffer);
        p->length = 0;
        p->buffer = NULL;
        return NULL;
    }
    if (newbuffer)
    {
        memcpy(newbuffer, p->buffer, p->offset);
    }
    cJSON_free(p->buffer);
    p->length = newsize;
    p->buffer = newbuffer;

    return newbuffer + p->offset;
}

/* get the decimal point character of the current locale */
static unsigned char get_decimal_point(void)
{
    return '.';
}

/* Render the number nicely from the given item into a string. */
static char *print_number(const cJSON *item, printbuffer *p)
{
    char *str = NULL;
    double d = item->valuedouble;
    int length = 0;
    size_t i = 0;
    unsigned char number_buffer[26] = {0}; /* temporary buffer to print the number into */
    unsigned char decimal_point = get_decimal_point();
    double test = 0.0;

    if (p)
    {
        str = (char*)ensure(p, 64);
    }
    else
    {
        str = (char*)cJSON_malloc(64);
    }
    if (!str)
    {
        return NULL;
    }


    /* This checks for NaN and Infinity */
    if ((d * 0) != 0)
    {
        length = sprintf(str, "null");
    }
    else
    {
        /* Try 15 decimal places of precision to avoid nonsignificant nonzero digits */
        length = sprintf((char*)number_buffer, "%.15g", d);

        /* Check whether the original double can be recovered */
        if ((sscanf((char*)number_buffer, "%lg", &test) != 1) || (test != d))
        {
            /* If not, print with 17 decimal places of precision */
            length = sprintf((char*)number_buffer, "%.17g", d);
        }
    }


    /* copy the printed number to the output and replace locale
     * dependent decimal point with a . */
    if (p)
    {
        for (i = 0; i < (size_t)length; i++)
        {
            if (number_buffer[i] == decimal_point)
            {
                p->buffer[p->offset + i] = '.';
                continue;
            }

            p->buffer[p->offset + i] = number_buffer[i];
        }
        p->buffer[p->offset + i] = '\0';
        p->offset += length;
    }
    else
    {
        for (i = 0; i < (size_t)length; i++)
        {
            if (number_buffer[i] == decimal_point)
            {
                str[i] = '.';
                continue;
            }

            str[i] = number_buffer[i];
        }
        str[length] = '\0';
    }

    return str;
}

/* Parse 4 hex digits and place the result in a utf16 LE output buffer, returning 1 on success, 0 on failure. */
static cJSON_bool parse_hex4(const unsigned char * const input, unsigned int * const output)
{
    unsigned int h = 0;
    size_t i = 0;

    for (i = 0; i < 4; i++)
    {
        /* parse symbol */
        if ((input[i] >= '0') && (input[i] <= '9'))
        {
            h += (unsigned int)input[i] - '0';
        }
        else if ((input[i] >= 'A') && (input[i] <= 'F'))
        {
            h += (unsigned int)10 + input[i] - 'A';
        }
        else if ((input[i] >= 'a') && (input[i] <= 'f'))
        {
            h += (unsigned int)10 + input[i] - 'a';
        }
        else /* invalid */
        {
            return false;
        }

        if (i < 3)
        {
            /* shift left to make place for the next nibble */
            h = h << 4;
        }
    }
    *output = h;

    return true;
}

/* converts a UTF-16 literal to UTF-8
 * A literal can be one or two sequences of the form \uXXXX */
static unsigned char utf16_literal_to_utf8(const unsigned char * const input_pointer, const unsigned char * const input_end, unsigned char **output_pointer)
{
    long unsigned int codepoint = 0;
    unsigned int first_code = 0;
    const unsigned char *first_sequence = input_pointer;
    unsigned char utf8_length = 0;
    unsigned char utf8_position = 0;
    unsigned char sequence_length = 0;
    unsigned char first_byte_mark = 0;

    if ((input_end - first_sequence) < 6)
    {
        /* input ends unexpectedly */
        goto fail;
    }

    /* get the first utf16 sequence */
    if (!parse_hex4(first_sequence + 2, &first_code))
    {
        /* failed to parse hex */
        goto fail;
    }

    /* check that the first sequence is valid utf16 */
    if ((first_code >= 0xDC00) && (first_code <= 0xDFFF))
    {
        /* UTF16 low surrogate must be preceded by a high surrogate */
        goto fail;
    }

    /* input ended unexpectedly after the first sequence */
    if ((input_end - first_sequence) < 6)
    {
        goto fail;
    }

    /* handle high surrogate pairs */
    if ((first_code >= 0xD800) && (first_code <= 0xDBFF))
    {
        unsigned int second_code = 0;
        const unsigned char *second_sequence = first_sequence + 6;

        if ((input_end - second_sequence) < 6)
        {
            /* input ends unexpectedly after the first sequence */
            goto fail;
        }

        if ((second_sequence[0] != '\\') || (second_sequence[1] != 'u'))
        {
            /* missing second half of the surrogate pair */
            goto fail;
        }

        /* get the second utf16 sequence */
        if (!parse_hex4(second_sequence + 2, &second_code))
        {
            /* failed to parse hex */
            goto fail;
        }

        /* check that the second sequence is a valid low surrogate */
        if ((second_code < 0xDC00) || (second_code > 0xDFFF))
        {
            /* invalid second half of the surrogate pair */
            goto fail;
        }


        /* calculate the unicode codepoint from the surrogate pair */
        codepoint = 0x10000 + (((first_code & 0x3FF) << 10) | (second_code & 0x3FF));
        sequence_length = 12; /* \uXXXX\uXXXX */
    }
    else
    {
        codepoint = first_code;
        sequence_length = 6; /* \uXXXX */
    }

    /* encode as UTF-8
     * takes at maximum 4 bytes to encode:
     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (codepoint < 0x80)
    {
        /* normal ascii, encoding 0xxxxxxx */
        utf8_length = 1;
    }
    else if (codepoint < 0x800)
    {
        /* two bytes, encoding 110xxxxx 10xxxxxx */
        utf8_length = 2;
        first_byte_mark = 0xC0; /* 11000000 */
    }
    else if (codepoint < 0x10000)
    {
        /* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
        utf8_length = 3;
        first_byte_mark = 0xE0; /* 11100000 */
    }
    else if (codepoint <= 0x10FFFF)
    {
        /* four bytes, encoding 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        utf8_length = 4;
        first_byte_mark = 0xF0; /* 11110000 */
    }
    else
    {
        /* invalid unicode codepoint */
        goto fail;
    }

    /* encode as utf8 */
    for (utf8_position = (unsigned char)(utf8_length - 1); utf8_position > 0; utf8_position--)
    {
        /* 10xxxxxx */
        (*output_pointer)[utf8_position] = (unsigned char)((codepoint | 0x80) & 0xBF);
        codepoint >>= 6;
    }
    /* encode first byte */
    if (utf8_length > 1)
    {
        (*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
    }
    else
    {
        (*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);
    }

    *output_pointer += utf8_length;

    return sequence_length;

fail:
    return 0;
}

/* Parse the input text into an unescaped cinput, and populate item. */
static const unsigned char *parse_string(cJSON *item, const unsigned char *str, const unsigned char *input_end)
{
    const unsigned char *ptr = str + 1;
    unsigned char *ptr2 = NULL;
    unsigned char *out = NULL;
    size_t len = 0;

    /* not a string! */
    if (*str != '\"')
    {
        global_ep = (const char*)str;
        return NULL;
    }

    while ((ptr < input_end) && (*ptr != '\"'))
    {
        if (*ptr != '\\')
        {
            ptr++;
        }
        else
        {
            ptr++;
            if ((ptr < input_end) && ((*ptr == '\"') || (*ptr == '\\') || (*ptr == '/') || (*ptr == 'b') || (*ptr == 'f') || (*ptr == 'n') || (*ptr == 'r') || (*ptr == 't')))
            {
                ptr++;
            }
            else if ((ptr < input_end) && (*ptr == 'u'))
            {
                /* transcode utf16 to utf8. */
                unsigned char sequence_length = utf16_literal_to_utf8(ptr, input_end, (unsigned char**)&ptr);
                if (!sequence_length)
                {
                    /* invalid utf16 sequence */
                    global_ep = (const char*)str;
                    return NULL;
                }
                len += sequence_length - 6;
            }
            else
            {
                global_ep = (const char*)str;
                return NULL;
            }
        }
    }
    len += (size_t)(ptr - str) - 1;

    out = (unsigned char*)cJSON_malloc(len + 1);
    if (!out)
    {
        return NULL;
    }

    ptr = str + 1;
    ptr2 = out;
    /* while string is not terminated */
    while (ptr < input_end && *ptr != '\"')
    {
        if (*ptr != '\\')
        {
            *ptr2++ = *ptr++;
        }
        else
        {
            ptr++;
            switch (*ptr)
            {
                case 'b':
                    *ptr2++ = '\b';
                    break;
                case 'f':
                    *ptr2++ = '\f';
                    break;
                case 'n':
                    *ptr2++ = '\n';
                    break;
                case 'r':
                    *ptr2++ = '\r';
                    break;
                case 't':
                    *ptr2++ = '\t';
                    break;
                case 'u':
                    /* transcode utf16 to utf8. */
                    ptr += utf16_literal_to_utf8(ptr, input_end, &ptr2) -1;
                    break;
                default:
                    *ptr2++ = *ptr;
                    break;
            }
            ptr++;
        }
    }
    *ptr2 = '\0';
    if (*ptr == '\"')
    {
        ptr++;
    }
    item->valuestring = (char*)out;
    item->type = cJSON_String;

    return ptr;
}

/* Render the cstring provided to an escaped version that can be printed. */
static char *print_string_ptr(const unsigned char *str, printbuffer *p)
{
    const unsigned char *ptr = NULL;
    unsigned char *ptr2 = NULL;
    char *out = NULL;
    size_t len = 0, flag = 0;

    if (!str)
    {
        if (p)
        {
            out = (char*)ensure(p, 3);
        }
        else
        {
            out = (char*)cJSON_malloc(3);
        }
        if (!out)
        {
            return NULL;
        }
        strcpy(out, "\"\"");

        return out;
    }

    /* first pass determines the length of the output string */
    for (ptr = str; *ptr; ptr++)
    {
        flag = 0;
        switch (*ptr)
        {
            case '\"':
                flag = 1;
                break;
            case '\\':
                flag = 1;
                break;
            case '\b':
                flag = 1;
                break;
            case '\f':
                flag = 1;
                break;
            case '\n':
                flag = 1;
                break;
            case '\r':
                flag = 1;
                break;
            case '\t':
                flag = 1;
                break;
            default:
                if (*ptr < 32)
                {
                    /* UTF-8 characters less than 32 must be escaped */
                    flag = 1;
                }
                break;
        }
        if (flag)
        {
            len += 5;
        }
    }
    len += (size_t)(ptr - str) + 3;

    if (p)
    {
        out = (char*)ensure(p, len);
    }
    else
    {
        out = (char*)cJSON_malloc(len);
    }
    if (!out)
    {
        return NULL;
    }

    ptr2 = (unsigned char*)out;
    ptr = str;
    *ptr2++ = '\"';
    while (*ptr)
    {
        if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\')
        {
            *ptr2++ = *ptr++;
        }
        else
        {
            *ptr2++ = '\\';
            switch (*ptr++)
            {
                case '\\':
                    *ptr2++ = '\\';
                    break;
                case '\"':
                    *ptr2++ = '\"';
                    break;
                case '\b':
                    *ptr2++ = 'b';
                    break;
                case '\f':
                    *ptr2++ = 'f';
                    break;
                case '\n':
                    *ptr2++ = 'n';
                    break;
                case '\r':
                    *ptr2++ = 'r';
                    break;
                case '\t':
                    *ptr2++ = 't';
                    break;
                default:
                    sprintf((char*)ptr2, "u%04x", *ptr - 1);
                    ptr2 += 5;
                    break;
            }
        }
    }
    *ptr2++ = '\"';
    *ptr2++ = '\0';
    if (p)
    {
        p->offset = (size_t)(ptr2 - (unsigned char*)p->buffer) - 1;
        return (char*)p->buffer;
    }
    else
    {
        return out;
    }
}

/* Invote print_string_ptr (which is useful) on an item. */
static char *print_string(const cJSON *item, printbuffer *p)
{
    return print_string_ptr((unsigned char*)item->valuestring, p);
}

/* Predeclare these prototypes. */
static const unsigned char *parse_value(cJSON *item, const unsigned char *value, const unsigned char *input_end);
static char *print_value(const cJSON *item, int depth, cJSON_bool fmt, printbuffer *p);
static const unsigned char *parse_array(cJSON *item, const unsigned char *value, const unsigned char *input_end);
static char *print_array(const cJSON *item, int depth, cJSON_bool fmt, printbuffer *p);
static const unsigned char *parse_object(cJSON *item, const unsigned char *value, const unsigned char *input_end);
static char *print_object(const cJSON *item, int depth, cJSON_bool fmt, printbuffer *p);

/* Utility to jump whitespace and cr/lf */
static const unsigned char *skip(const unsigned char *in, const unsigned char *input_end)
{
    while (in && (in < input_end) && *in <= 32)
    {
        in++;
    }
    return in;
}

/* Parse an object - create a new root, and populate. */
cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    const size_t len = strlen(value);
    const unsigned char *value_uchar = (const unsigned char*)value;
    const unsigned char *end_of_buffer = value_uchar + len;
    const unsigned char *parse_end = NULL;

    cJSON *c = cJSON_New_Item();
    global_ep = NULL;
    if (!c) /* memory fail */
    {
        return NULL;
    }

    parse_end = parse_value(c, skip(value_uchar, end_of_buffer), end_of_buffer);
    if (!parse_end)
    {
        cJSON_Delete(c);
        return NULL;
    }

    /* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
    if (require_null_terminated)
    {
        const unsigned char *end_of_parse = skip(parse_end, end_of_buffer);
        if (end_of_parse != end_of_buffer)
        {
            cJSON_Delete(c);
            global_ep = (const char*)end_of_parse;
            return NULL;
        }
    }
    if (return_parse_end)
    {
        *return_parse_end = (const char*)global_ep;
    }

    return c;
}

/* Default options for cJSON_Parse */
cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);
}

/* Render a cJSON item/entity/structure to text. */
char *cJSON_Print(const cJSON *item)
{
    return print_value(item, 0, true, 0);
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    return print_value(item, 0, false, 0);
}

char *cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt)
{
    printbuffer p;
    p.buffer = (unsigned char*)cJSON_malloc((size_t)prebuffer);
    if (!p.buffer)
    {
        return NULL;
    }
    p.length = (size_t)prebuffer;
    p.offset = 0;
    return print_value(item, 0, fmt, &p);
}

/* Parser core - when encountering text, process appropriately. */
static const unsigned char *parse_value(cJSON *item, const unsigned char *value, const unsigned char *input_end)
{
    if (!value)
    {
        return NULL; /* Fail on null. */
    }

    /* parse the different types of values */
    if (!strncmp((const char*)value, "null", 4))
    {
        item->type = cJSON_NULL;
        return value + 4;
    }
    if (!strncmp((const char*)value, "false", 5))
    {
        item->type = cJSON_False;
        return value + 5;
    }
    if (!strncmp((const char*)value, "true", 4))
    {
        item->type = cJSON_True;
        item->valueint = 1;
        return value + 4;
    }
    if (*value == '\"')
    {
        return parse_string(item, value, input_end);
    }
    if (*value == '-' || (*value >= '0' && *value <= '9'))
    {
        return parse_number(item, value);
    }
    if (*value == '[')
    {
        return parse_array(item, value, input_end);
    }
    if (*value == '{')
    {
        return parse_object(item, value, input_end);
    }

    global_ep = (const char*)value;
    return NULL; /* failure. */
}

/* Render a value to text. */
static char *print_value(const cJSON *item, int depth, cJSON_bool fmt, printbuffer *p)
{
    char *out = NULL;

    if (!item)
    {
        return NULL;
    }
    if (p)
    {
        switch ((item->type) & 255)
        {
            case cJSON_NULL:
            {
                out = (char*)ensure(p, 5);
                if (out)
                {
                    strcpy(out, "null");
                }
                break;
            }
            case cJSON_False:
            {
                out = (char*)ensure(p, 6);
                if (out)
                {
                    strcpy(out, "false");
                }
                break;
            }
            case cJSON_True:
            {
                out = (char*)ensure(p, 5);
                if (out)
                {
                    strcpy(out, "true");
                }
                break;
            }
            case cJSON_Number:
                out = print_number(item, p);
                break;
            case cJSON_Raw:
            {
                size_t raw_length = 0;
                if (item->valuestring == NULL)
                {
                    if (!ensure(p, 3))
                    {
                        return NULL;
                    }
                    strcpy((char*)p->buffer, "\"\"");
                    break;
                }

                raw_length = strlen(item->valuestring) + 2;
                if (!ensure(p, raw_length))
                {
                    return NULL;
                }

                p->offset = sprintf((char*)p->buffer, "%s", item->valuestring);
                break;
            }
            case cJSON_String:
                out = print_string(item, p);
                break;
            case cJSON_Array:
                out = print_array(item, depth, fmt, p);
                break;
            case cJSON_Object:
                out = print_object(item, depth, fmt, p);
                break;
        }
    }
    else
    {
        switch ((item->type) & 255)
        {
            case cJSON_NULL:
                out = (char*)cJSON_strdup((const unsigned char*)"null", 4);
                break;
            case cJSON_False:
                out = (char*)cJSON_strdup((const unsigned char*)"false", 5);
                break;
            case cJSON_True:
                out = (char*)cJSON_strdup((const unsigned char*)"true", 4);
                break;
            case cJSON_Number:
                out = print_number(item, 0);
                break;
            case cJSON_Raw:
                out = (char*)cJSON_strdup((unsigned char*)item->valuestring, strlen(item->valuestring));
                break;
            case cJSON_String:
                out = print_string(item, 0);
                break;
            case cJSON_Array:
                out = print_array(item, depth, fmt, 0);
                break;
            case cJSON_Object:
                out = print_object(item, depth, fmt, 0);
                break;
        }
    }

    return out;
}

/* Build an array from input text. */
static const unsigned char *parse_array(cJSON *item, const unsigned char *value, const unsigned char *input_end)
{
    cJSON *child = NULL;
    if (*value != '[') /* not an array! */
    {
        global_ep = (const char*)value;
        return NULL;
    }

    item->type = cJSON_Array;
    value = skip(value + 1, input_end);
    if (*value == ']') /* empty array. */
    {
        return value + 1;
    }

    item->child = child = cJSON_New_Item();
    if (!item->child) /* memory fail */
    {
        return NULL;
    }
    value = skip(parse_value(child, skip(value, input_end), input_end), input_end); /* skip any spacing, get the value. */
    if (!value)
    {
        return NULL;
    }

    while (*value == ',')
    {
        cJSON *new_item = NULL;
        value = skip(value + 1, input_end);
        if (*value == ']') /* allow trailing comma */
        {
            break;
        }

        new_item = cJSON_New_Item();
        if (!new_item)
        {
            return NULL; /* memory fail */
        }
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_value(child, skip(value, input_end), input_end), input_end);
        if (!value)
        {
            return NULL; /* memory fail */
        }
    }

    if (*value == ']')
    {
        return value + 1; /* end of array */
    }

    global_ep = (const char*)value;
    return NULL; /* malformed. */
}

/* Render an array to text */
static char *print_array(const cJSON *item, int depth, cJSON_bool fmt, printbuffer *p)
{
    char **entries;
    char *out = NULL, *ptr = NULL, *ret = NULL;
    size_t len = 5;
    cJSON *child = item->child;
    int numentries = 0, i = 0, fail = 0;
    size_t tmplen = 0;

    /* How many entries in the array? */
    while (child)
    {
        numentries++;
        child = child->next;
    }
    /* Explicitly handle numentries==0 */
    if (!numentries)
    {
        if (p)
        {
            out = (char*)ensure(p, 3);
        }
        else
        {
            out = (char*)cJSON_malloc(3);
        }
        if (out)
        {
            strcpy(out, "[]");
        }
        return out;
    }

    if (p)
    {
        /* Compose the output array. */
        i = p->offset;
        ptr = (char*)ensure(p, 1);
        if (!ptr)
        {
            return NULL;
        }
        *ptr = '[';
        p->offset++;

        child = item->child;
        while (child && !fail)
        {
            print_value(child, depth + 1, fmt, p);
            p->offset = (size_t)((unsigned char*)p->buffer - (unsigned char*)p->buffer);
            if (child->next)
            {
                len = fmt ? 2 : 1;
                ptr = (char*)ensure(p, len + 1);
                if (!ptr)
                {
                    return NULL;
                }
                *ptr++ = ',';
                if(fmt)
                {
                    *ptr++ = ' ';
                }
                *ptr = '\0';
                p->offset += len;
            }
            child = child->next;
        }
        ptr = (char*)ensure(p, 2);
        if (!ptr)
        {
            return NULL;
        }
        *ptr++ = ']';
        *ptr = '\0';
        out = (char*)p->buffer;
    }
    else
    {
        /* Allocate an array to hold the pointers to all printed values */
        entries = (char**)cJSON_malloc(numentries * sizeof(char*));
        if (!entries)
        {
            return NULL;
        }
        memset(entries, '\0', numentries * sizeof(char*));
        /* Retrieve all the results: */
        child = item->child;
        while (child && !fail)
        {
            ret = print_value(child, depth + 1, fmt, 0);
            entries[i++] = ret;
            if (ret)
            {
                len += strlen(ret) + 2 + (fmt ? 1 : 0);
            }
            else
            {
                fail = 1;
            }
            child = child->next;
        }

        /* If we didn't fail, try to malloc the output string */
        if (!fail)
        {
            out = (char*)cJSON_malloc(len);
        }
        /* If that fails, we fail. */
        if (!out)
        {
            fail = 1;
        }

        /* Handle failure. */
        if (fail)
        {
            for (i = 0; i < numentries; i++)
            {
                if (entries[i])
                {
                    cJSON_free(entries[i]);
                }
            }
            cJSON_free(entries);
            return NULL;
        }

        /* Compose the output array. */
        *out = '[';
        ptr = out + 1;
        *ptr = '\0';
        for (i = 0; i < numentries; i++)
        {
            tmplen = strlen(entries[i]);
            memcpy(ptr, entries[i], tmplen);
            ptr += tmplen;
            if (i != numentries - 1)
            {
                *ptr++ = ',';
                if(fmt)
                {
                    *ptr++ = ' ';
                }
                *ptr = '\0';
            }
            cJSON_free(entries[i]);
        }
        cJSON_free(entries);
        *ptr++ = ']';
        *ptr++ = '\0';
    }

    return out;
}

/* Build an object from the text. */
static const unsigned char *parse_object(cJSON *item, const unsigned char *value, const unsigned char *input_end)
{
    cJSON *child = NULL;
    if (*value != '{')
    {
        global_ep = (const char*)value;
        return NULL;
    }

    item->type = cJSON_Object;
    value = skip(value + 1, input_end);
    if (*value == '}')
    {
        return value + 1;
    }

    item->child = child = cJSON_New_Item();
    if (!item->child)
    {
        return NULL;
    }
    value = skip(parse_string(child, skip(value, input_end), input_end), input_end);
    if (!value)
    {
        return NULL;
    }
    child->string = child->valuestring;
    child->valuestring = NULL;
    if (*value != ':')
    {
        global_ep = (const char*)value;
        return NULL;
    }
    value = skip(parse_value(child, skip(value + 1, input_end), input_end), input_end);
    if (!value)
    {
        return NULL;
    }

    while (*value == ',')
    {
        cJSON *new_item;
        value = skip(value + 1, input_end);
        if (*value == '}') /* allow trailing comma */
        {
            break;
        }

        new_item = cJSON_New_Item();
        if (!new_item)
        {
            return NULL;
        }
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_string(child, skip(value, input_end), input_end), input_end);
        if (!value)
        {
            return NULL;
        }
        child->string = child->valuestring;
        child->valuestring = NULL;
        if (*value != ':')
        {
            global_ep = (const char*)value;
            return NULL;
        }
        value = skip(parse_value(child, skip(value + 1, input_end), input_end), input_end);
        if (!value)
        {
            return NULL;
        }
    }

    if (*value == '}')
    {
        return value + 1;
    }

    global_ep = (const char*)value;
    return NULL;
}

/* Render an object to text. */
static char *print_object(const cJSON *item, int depth, cJSON_bool fmt, printbuffer *p)
{
    char **entries = NULL, **names = NULL;
    char *out = NULL, *ptr = NULL, *ret = NULL, *str = NULL;
    size_t len = 7, i = 0, j = 0;
    cJSON *child = item->child;
    int numentries = 0, fail = 0;
    size_t tmplen = 0;

    /* Count the number of entries. */
    while (child)
    {
        numentries++;
        child = child->next;
    }
    /* Explicitly handle empty object case */
    if (!numentries)
    {
        if (p)
        {
            out = (char*)ensure(p, fmt ? depth + 4 : 3);
        }
        else
        {
            out = (char*)cJSON_malloc(fmt ? depth + 4 : 3);
        }
        if (!out)
        {
            return NULL;
        }
        ptr = out;
        *ptr++ = '{';
        if (fmt) {
            *ptr++ = '\n';
            for (i = 0; i < (size_t)depth; i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr++ = '\0';
        return out;
    }
    if (p)
    {
        /* Compose the output: */
        i = p->offset;
        len = fmt ? 2 : 1;
        ptr = (char*)ensure(p, len+1);
        if (!ptr)
        {
            return NULL;
        }
        *ptr++ = '{';
        if (fmt)
        {
            *ptr++ = '\n';
        }
        *ptr = '\0';
        p->offset += len;
        child = item->child;
        depth++;
        while (child)
        {
            if (fmt)
            {
                ptr = (char*)ensure(p, depth);
                if (!ptr)
                {
                    return NULL;
                }
                for (j = 0; j < (size_t)depth; j++)
                {
                    *ptr++ = '\t';
                }
                p->offset += depth;
            }
            print_string_ptr((unsigned char*)child->string, p);
            p->offset = (size_t)((unsigned char*)p->buffer - (unsigned char*)p->buffer);

            len = fmt ? 2 : 1;
            ptr = (char*)ensure(p, len);
            if (!ptr)
            {
                return NULL;
            }
            *ptr++ = ':';
            if (fmt)
            {
                *ptr++ = '\t';
            }
            p->offset += len;

            print_value(child, depth, fmt, p);
            p->offset = (size_t)((unsigned char*)p->buffer - (unsigned char*)p->buffer);

            len = (size_t)(fmt ? 1 : 0) + (child->next ? 1 : 0);
            ptr = (char*)ensure(p, len + 1);
            if (!ptr)
            {
                return NULL;
            }
            if (child->next)
            {
                *ptr++ = ',';
            }

            if (fmt)
            {
                *ptr++ = '\n';
            }
            *ptr = '\0';
            p->offset += len;
            child = child->next;
        }
        ptr = (char*)ensure(p, fmt ? (depth + 1) : 2);
        if (!ptr)
        {
            return NULL;
        }
        if (fmt)
        {
            for (i = 0; i < (size_t)(depth - 1); i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr = '\0';
        out = (char*)p->buffer;
    }
    else
    {
        /* Allocate space for the names and entries */
        entries = (char**)cJSON_malloc(numentries * sizeof(char*));
        if (!entries)
        {
            return NULL;
        }
        names = (char**)cJSON_malloc(numentries * sizeof(char*));
        if (!names)
        {
            cJSON_free(entries);
            return NULL;
        }
        memset(entries, '\0', sizeof(char*) * numentries);
        memset(names, '\0', sizeof(char*) * numentries);

        /* Collect all the results into our arrays: */
        child = item->child;
        depth++;
        if (fmt)
        {
            len += depth;
        }
        while (child && !fail)
        {
            names[i] = str = print_string_ptr((unsigned char*)child->string, 0);
            entries[i++] = ret = print_value(child, depth, fmt, 0);
            if (str && ret)
            {
                len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0);
            }
            else
            {
                fail = 1;
            }
            child = child->next;
        }

        /* Try to allocate the output string */
        if (!fail)
        {
            out = (char*)cJSON_malloc(len);
        }
        if (!out)
        {
            fail = 1;
        }

        /* Handle failure */
        if (fail)
        {
            for (i = 0; i < numentries; i++)
            {
                if (names[i])
                {
                    cJSON_free(names[i]);
                }
                if (entries[i])
                {
                    cJSON_free(entries[i]);
                }
            }
            cJSON_free(names);
            cJSON_free(entries);
            return NULL;
        }

        /* Compose the output: */
        *out = '{';
        ptr = out + 1;
        if (fmt)
        {
            *ptr++ = '\n';
        }
        *ptr = '\0';
        for (i = 0; i < numentries; i++)
        {
            if (fmt)
            {
                for (j = 0; j < (size_t)depth; j++)
                {
                    *ptr++ = '\t';
                }
            }
            tmplen = strlen(names[i]);
            memcpy(ptr, names[i], tmplen);
            ptr += tmplen;
            *ptr++ = ':';
            if (fmt)
            {
                *ptr++ = '\t';
            }
            strcpy(ptr, entries[i]);
            ptr += strlen(entries[i]);
            if (i != numentries - 1)
            {
                *ptr++ = ',';
            }
            if (fmt)
            {
                *ptr++ = '\n';
            }
            *ptr = '\0';
            cJSON_free(names[i]);
            cJSON_free(entries[i]);
        }

        cJSON_free(names);
        cJSON_free(entries);
        if (fmt)
        {
            for (i = 0; i < (size_t)(depth - 1); i++)
            {
                *ptr++ = '\t';
            }
        }
        *ptr++ = '}';
        *ptr++ = '\0';
    }

    return out;
}

/* Get Array size/item / object item. */
int cJSON_GetArraySize(const cJSON *array)
{
    cJSON *c = array->child;
    int i = 0;
    while (c)
    {
        i++;
        c = c->next;
    }
    return i;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int item)
{
    cJSON *c = array ? array->child : 0;
    while (c && item > 0)
    {
        item--;
        c = c->next;
    }
    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string)
{
    cJSON *c = object ? object->child : 0;
    while (c && cJSON_strcasecmp(c->string, string))
    {
        c = c->next;
    }
    return c;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string)
{
    cJSON *c = object ? object->child : 0;
    while (c && strcmp(c->string, string))
    {
        c = c->next;
    }
    return c;
}

cJSON_bool cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

/* Utility for array list handling. */
static void suffix_object(cJSON *prev, cJSON *item)
{
    prev->next = item;
    item->prev = prev;
}
/* Utility for handling references. */
static cJSON *create_reference(const cJSON *item)
{
    cJSON *ref = cJSON_New_Item();
    if (!ref)
    {
        return NULL;
    }
    memcpy(ref, item, sizeof(cJSON));
    ref->string = 0;
    ref->type |= cJSON_IsReference;
    ref->next = ref->prev = 0;
    return ref;
}

/* Add item to array/object. */
void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *c = array->child;
    if (!item)
    {
        return;
    }
    if (!c)
    {
        array->child = item;
    }
    else
    {
        while (c && c->next)
        {
            c = c->next;
        }
        suffix_object(c, item);
    }
}

void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    if (!item)
    {
        return;
    }
    if (item->string)
    {
        cJSON_free(item->string);
    }
    item->string = (char*)cJSON_strdup((const unsigned char*)string, strlen(string));
    cJSON_AddItemToArray(object, item);
}

void cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item)
{
    if (!item)
    {
        return;
    }
    if (!(item->type & cJSON_StringIsConst) && item->string)
    {
        cJSON_free(item->string);
    }
    item->string = (char*)string;
    item->type |= cJSON_StringIsConst;
    cJSON_AddItemToArray(object, item);
}

void cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item)
{
    cJSON_AddItemToArray(array, create_reference(item));
}

void cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item)
{
    cJSON_AddItemToObject(object, string, create_reference(item));
}

cJSON *cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item)
{
    if (!parent || !item)
    {
        return NULL;
    }

    if (item->prev)
    {
        item->prev->next = item->next;
    }
    if (item->next)
    {
        item->next->prev = item->prev;
    }
    if (item == parent->child)
    {
        parent->child = item->next;
    }
    item->prev = item->next = NULL;

    return item;
}

cJSON *cJSON_DetachItemFromArray(cJSON *array, int which)
{
    cJSON *c = array->child;
    while (c && which > 0)
    {
        c = c->next;
        which--;
    }
    if (!c)
    {
        return NULL;
    }

    return cJSON_DetachItemViaPointer(array, c);
}

void cJSON_DeleteItemFromArray(cJSON *array, int which)
{
    cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}

/* **FIX:** Removed const from first parameter to match declaration in cJSON.h */
cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *string)
{
    int i = 0;
    cJSON *c = object->child;
    while (c && cJSON_strcasecmp(c->string, string))
    {
        i++;
        c = c->next;
    }
    if (c)
    {
        return cJSON_DetachItemFromArray(object, i);
    }

    return NULL;
}

/* **FIX:** Removed const from first parameter to match declaration in cJSON.h */
cJSON *cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string)
{
    int i = 0;
    cJSON *c = object->child;
    while (c && strcmp(c->string, string))
    {
        i++;
        c = c->next;
    }
    if (c)
    {
        return cJSON_DetachItemFromArray(object, i);
    }

    return NULL;
}

void cJSON_DeleteItemFromObject(cJSON *object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObject(object, string));
}

/* Replace item in array/object. */
void cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem)
{
    cJSON *c = array->child;
    while (c && which > 0)
    {
        c = c->next;
        which--;
    }
    if (!c)
    {
        cJSON_AddItemToArray(array, newitem);
        return;
    }
    newitem->next = c;
    newitem->prev = c->prev;
    c->prev = newitem;
    if (c == array->child)
    {
        array->child = newitem;
    }
    else
    {
        newitem->prev->next = newitem;
    }
}

cJSON_bool cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON * replacement)
{
    if (!parent || !item || !replacement)
    {
        return false;
    }

    if (item->next)
    {
        item->next->prev = replacement;
    }
    if (item->prev)
    {
        item->prev->next = replacement;
    }
    replacement->next = item->next;
    replacement->prev = item->prev;

    if (item == parent->child)
    {
        parent->child = replacement;
    }

    item->next = NULL;
    item->prev = NULL;
    cJSON_Delete(item);

    return true;
}

void cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem)
{
    cJSON *c = array->child;
    while (c && which > 0)
    {
        c = c->next;
        which--;
    }
    if (!c)
    {
        return;
    }
    newitem->next = c->next;
    newitem->prev = c->prev;
    if (newitem->next)
    {
        newitem->next->prev = newitem;
    }
    if (c == array->child)
    {
        array->child = newitem;
    }
    else
    {
        newitem->prev->next = newitem;
    }
    c->next = c->prev = NULL;
    cJSON_Delete(c);
}

void cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem)
{
    int i = 0;
    cJSON *c = object->child;
    while (c && cJSON_strcasecmp(c->string, string))
    {
        i++;
        c = c->next;
    }
    if (c)
    {
        newitem->string = (char*)cJSON_strdup((const unsigned char*)string, strlen(string));
        cJSON_ReplaceItemInArray(object, i, newitem);
    }
}

void cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object, const char *string, cJSON *newitem)
{
    int i = 0;
    cJSON *c = object->child;
    while (c && strcmp(c->string, string))
    {
        i++;
        c = c->next;
    }
    if (c)
    {
        newitem->string = (char*)cJSON_strdup((const unsigned char*)string, strlen(string));
        cJSON_ReplaceItemInArray(object, i, newitem);
    }
}

/* Create basic types: */
cJSON *cJSON_CreateNull(void)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_NULL;
    }
    return item;
}

cJSON *cJSON_CreateTrue(void)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_True;
    }
    return item;
}

cJSON *cJSON_CreateFalse(void)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_False;
    }
    return item;
}

cJSON *cJSON_CreateBool(cJSON_bool b)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = b ? cJSON_True : cJSON_False;
    }
    return item;
}

cJSON *cJSON_CreateNumber(double num)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}

cJSON *cJSON_CreateString(const char *string)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_String;
        item->valuestring = (char*)cJSON_strdup((const unsigned char*)string, strlen(string));
        if (!item->valuestring)
        {
            cJSON_Delete(item);
            return NULL;
        }
    }
    return item;
}

cJSON *cJSON_CreateRaw(const char *raw)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_Raw;
        item->valuestring = (char*)cJSON_strdup((const unsigned char*)raw, strlen(raw));
        if (!item->valuestring)
        {
            cJSON_Delete(item);
            return NULL;
        }
    }
    return item;
}

cJSON *cJSON_CreateArray(void)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_Array;
    }
    return item;
}

cJSON *cJSON_CreateObject(void)
{
    cJSON *item = cJSON_New_Item();
    if (item)
    {
        item->type = cJSON_Object;
    }
    return item;
}

/* Create Arrays: */
cJSON *cJSON_CreateIntArray(const int *numbers, int count)
{
    int i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();
    for (i = 0; a && (i < count); i++)
    {
        n = cJSON_CreateNumber(numbers[i]);
        if (!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if (!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

cJSON *cJSON_CreateFloatArray(const float *numbers, int count)
{
    int i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for (i = 0; a && (i < count); i++)
    {
        n = cJSON_CreateNumber(numbers[i]);
        if (!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if (!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

cJSON *cJSON_CreateDoubleArray(const double *numbers, int count)
{
    int i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for (i = 0; a && (i < count); i++)
    {
        n = cJSON_CreateNumber(numbers[i]);
        if (!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if (!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

cJSON *cJSON_CreateStringArray(const char *const *strings, int count)
{
    int i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (strings == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for (i = 0; a && (i < count); i++)
    {
        n = cJSON_CreateString(strings[i]);
        if (!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if (!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

/* Duplication */
cJSON *cJSON_Duplicate(const cJSON *item, cJSON_bool recurse)
{
    cJSON *newitem = NULL, *cptr = NULL, *nptr = NULL, *newchild = NULL;
    /* Bail out if item is NULL */
    if (!item)
    {
        return NULL;
    }
    /* Create new item */
    newitem = cJSON_New_Item();
    if (!newitem)
    {
        return NULL;
    }
    /* Copy over all vars */
    newitem->type = item->type & (~cJSON_IsReference);
    newitem->valueint = item->valueint;
    newitem->valuedouble = item->valuedouble;
    if (item->valuestring)
    {
        newitem->valuestring = (char*)cJSON_strdup((unsigned char*)item->valuestring, strlen(item->valuestring));
        if (!newitem->valuestring)
        {
            cJSON_Delete(newitem);
            return NULL;
        }
    }
    if (item->string)
    {
        newitem->string = (item->type & cJSON_StringIsConst) ? item->string : (char*)cJSON_strdup((unsigned char*)item->string, strlen(item->string));
        if (!newitem->string)
        {
            cJSON_Delete(newitem);
            return NULL;
        }
    }
    /* If non-recursive, then we're done! */
    if (!recurse)
    {
        return newitem;
    }
    /* Walk the ->next chain for the child. */
    cptr = item->child;
    while (cptr)
    {
        newchild = cJSON_Duplicate(cptr, true); /* Duplicate (with recurse) each item in the ->next chain */
        if (!newchild)
        {
            cJSON_Delete(newitem);
            return NULL;
        }
        if (nptr)
        {
            /* If newitem->child already set, then crosswire ->prev and ->next and move on */
            nptr->next = newchild;
            newchild->prev = nptr;
            nptr = newchild;
        }
        else
        {
            /* Set newitem->child and move to it */
            newitem->child = newchild;
            nptr = newchild;
        }
        cptr = cptr->next;
    }

    return newitem;
}

void cJSON_Minify(char *json)
{
    unsigned char *into = (unsigned char*)json;
    while (*json)
    {
        if (*json == ' ')
        {
            json++;
        }
        else if (*json == '\t')
        {
            json++;
        }
        else if (*json == '\r')
        {
            json++;
        }
        else if (*json == '\n')
        {
            json++;
        }
        else if (*json == '/' && json[1] == '/')
        {
            while (*json && *json != '\n')
            {
                json++;
            }
        }
        else if (*json == '/' && json[1] == '*')
        {
            while (*json && !(*json == '*' && json[1] == '/'))
            {
                json++;
            }
            json += 2;
        }
        else if (*json == '\"')
        {
            *into++ = (unsigned char)*json++;
            while (*json && *json != '\"')
            {
                if (*json == '\\')
                {
                    *into++ = (unsigned char)*json++;
                }
                *into++ = (unsigned char)*json++;
            }
            *into++ = (unsigned char)*json++;
        }
        else
        {
            *into++ = (unsigned char)*json++;
        }
    }
    *into = '\0';
}

cJSON_bool cJSON_IsInvalid(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_Invalid;
}

cJSON_bool cJSON_IsFalse(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_False;
}

cJSON_bool cJSON_IsTrue(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_True;
}

cJSON_bool cJSON_IsBool(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & (cJSON_True | cJSON_False)) != 0;
}
cJSON_bool cJSON_IsNull(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_NULL;
}

cJSON_bool cJSON_IsNumber(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_Number;
}

cJSON_bool cJSON_IsString(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_String;
}

cJSON_bool cJSON_IsArray(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_Array;
}

cJSON_bool cJSON_IsObject(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_Object;
}

cJSON_bool cJSON_IsRaw(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }
    return (item->type & 255) == cJSON_Raw;
}

double cJSON_SetNumberHelper(cJSON *object, double number)
{
  if (object)
  {
    object->valuedouble = number;
    object->valueint = (int)number;
  }
  return number;
}

int cJSON_strcasecmp(const char *s1, const char *s2)
{
	if (!s1)
    {
        if (!s2)
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }
    if (!s2)
    {
        return -1;
    }

	for(; tolower((unsigned char)*s1) == tolower((unsigned char)*s2); ++s1, ++s2)
    {
		if(*s1 == 0)
        {
			return 0;
        }
    }

	return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}
