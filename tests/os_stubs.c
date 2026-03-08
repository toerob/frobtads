/*
 * os_stubs.c - Stub implementations of OS functions for unit tests.
 * These are used by print_file_handler tests to simulate interactive
 * os_gets() input, and also satisfy linker references from production headers.
 */
#include <stddef.h>
#include <string.h>

#define MAX_STUB_INPUTS 64

static const char *g_stub_inputs[MAX_STUB_INPUTS];
static size_t g_stub_input_count = 0;
static size_t g_stub_input_index = 0;

void os_stubs_reset_inputs(void)
{
    g_stub_input_count = 0;
    g_stub_input_index = 0;
}

int os_stubs_add_input(const char *line)
{
    if (g_stub_input_count >= MAX_STUB_INPUTS)
        return -1;

    g_stub_inputs[g_stub_input_count++] = (line != 0 ? line : "");
    return 0;
}

unsigned char *os_gets(unsigned char *buf, size_t buflen)
{
    if (buf == 0 || buflen == 0)
        return 0;

    if (g_stub_input_index >= g_stub_input_count)
        return 0;

    {
        const char *src = g_stub_inputs[g_stub_input_index++];
        size_t max_copy = buflen - 1;
        size_t len = strlen(src);
        if (len > max_copy)
            len = max_copy;

        memcpy(buf, src, len);
        buf[len] = '\0';
    }

    return buf;
}