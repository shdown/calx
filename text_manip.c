// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "text_manip.h"
#include "probes.generated.h"

const char *text_nth_line(const char *text, size_t ntext, size_t lineno, size_t *out_len)
{
    for (; lineno && ntext; --lineno) {
        const char *cr = memchr(text, '\n', ntext);
        if (!cr)
            break;
        ++cr;
        ntext -= cr - text;
        text = cr;
    }

    const char *cr = ntext ? memchr(text, '\n', ntext) : NULL;
    *out_len = cr ? ((size_t) (cr - text)) : ntext;
    return text;
}

void text_putnc(FILE *out, int c, size_t n)
{
    enum { NBUF = 128 };
    char buf[NBUF];

    memset(buf, c, NBUF);

    for (; n > NBUF; n -= NBUF)
        fwrite(buf, 1, NBUF, out);

    fwrite(buf, 1, n, out);
}

static size_t decode_wide(const char *s, size_t ns, size_t *out_width, const char **out_replacement)
{
    mbstate_t mbs = {0};
    wchar_t c;
    size_t r = mbrtowc(&c, s, ns, &mbs);
    if (r == 0 || r == ((size_t) -1) || r == ((size_t) -2)) {
        // Illegal sequence or NUL
        goto illegal;
    }

    if (c == L'\t') {
        // Tab
        *out_replacement = "    ";
        *out_width = 4;
        return 0;
    } else if (c == L'\r') {
        // <CR> (Windows-style newline is <CR><LF>)
        *out_replacement = " ";
        *out_width = 1;
        return 0;
    }

#if CALX_HAVE_WCWIDTH
    int width = wcwidth(c);
    if (width < 0 || c == L'\0') {
        // Non-printable
        goto illegal;
    }
    *out_width = width;
#else
    if ((c & 127) == c && c < 32) {
        // Non-printable
        goto illegal;
    } else {
        *out_width = 1;
    }
#endif

    return r;

illegal:
    *out_replacement = "?";
    *out_width = 1;
    return 0;
}

static size_t print_counting(FILE *out, const char **ps, size_t *pns, size_t limit)
{
    const char *s = *ps;
    size_t ns = *pns;

    size_t boundary = limit < ns ? limit : ns;

    size_t prev = 0;
    size_t total_width = 0;
    size_t offset = 0;
    while (offset < boundary) {
        size_t width;
        const char *replacement;
        size_t n = decode_wide(s + offset, ns - offset, &width, &replacement);
        if (n) {
            offset += n;
        } else {
            fwrite(s + prev, 1, offset - prev, out);
            fputs(replacement, out);
            ++offset;
            prev = offset;
        }
        total_width += width;
    }
    if (prev != offset)
        fwrite(s + prev, 1, offset - prev, out);

    *ps += offset;
    *pns -= offset;
    return total_width;
}

void text_show_line(FILE *out, const char *text, size_t ntext, size_t lineno)
{
    size_t nline;
    const char *line = text_nth_line(text, ntext, lineno, &nline);

    (void) print_counting(out, &line, &nline, SIZE_MAX);

    fputc('\n', out);
}

void text_show_line_segment(
    FILE *out, const char *text, size_t ntext, size_t lineno,
    size_t seg_offset, size_t seg_len)
{
    size_t nline;
    const char *line = text_nth_line(text, ntext, lineno, &nline);

    size_t width_before = print_counting(out, &line, &nline, seg_offset);
    size_t width_segment = print_counting(out, &line, &nline, seg_len);
    (void) print_counting(out, &line, &nline, SIZE_MAX);

    fputc('\n', out);
    text_putnc(out, ' ', width_before);
    fputc('^', out);
    if (width_segment)
        text_putnc(out, '~', width_segment - 1);
}
