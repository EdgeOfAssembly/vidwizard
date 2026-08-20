/**
 * @file parse_time.c
 * @brief Implementation of timestamp, range, crop, and speed parsers.
 */

#include "vidwizard/parse_time.h"

#include <string.h>

enum
{
    VW_EINVAL = -22,
    VW_ENOSPC = -28
};

static int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const char *skip_spaces(const char *p)
{
    while (p != NULL && is_space_char(*p))
    {
        p++;
    }
    return p;
}

static int parse_nonneg_double(const char **pp, double *out)
{
    const char *p = *pp;
    double value = 0.0;
    int digits = 0;

    if (p == NULL || *p < '0' || *p > '9')
    {
        return VW_EINVAL;
    }

    while (*p >= '0' && *p <= '9')
    {
        value = value * 10.0 + (double)(*p - '0');
        if (value > 100000000.0)
        {
            return VW_EINVAL;
        }
        p++;
        digits++;
    }

    if (*p == '.')
    {
        double place = 0.1;
        int frac_digits = 0;
        p++;
        if (*p < '0' || *p > '9')
        {
            return VW_EINVAL;
        }
        while (*p >= '0' && *p <= '9')
        {
            value += (double)(*p - '0') * place;
            place *= 0.1;
            p++;
            frac_digits++;
            if (frac_digits > 9)
            {
                return VW_EINVAL;
            }
        }
        digits += frac_digits;
    }

    if (digits < 1)
    {
        return VW_EINVAL;
    }

    *out = value;
    *pp = p;
    return 0;
}

static int parse_nonneg_int(const char **pp, int *out)
{
    const char *p = *pp;
    long value = 0;

    if (p == NULL || *p < '0' || *p > '9')
    {
        return VW_EINVAL;
    }

    while (*p >= '0' && *p <= '9')
    {
        value = value * 10 + (long)(*p - '0');
        if (value > 2147483647L)
        {
            return VW_EINVAL;
        }
        p++;
    }

    *out = (int)value;
    *pp = p;
    return 0;
}

int vw_parse_timestamp(const char *s, double *out_seconds)
{
    const char *p = NULL;
    const char *end = NULL;
    double fields[3] = {0.0, 0.0, 0.0};
    int nfields = 0;
    double seconds = 0.0;

    if (s == NULL || out_seconds == NULL)
    {
        return VW_EINVAL;
    }

    p = skip_spaces(s);
    if (*p == '\0')
    {
        return VW_EINVAL;
    }

    while (nfields < 3)
    {
        if (parse_nonneg_double(&p, &fields[nfields]) != 0)
        {
            return VW_EINVAL;
        }
        nfields++;
        if (*p != ':')
        {
            break;
        }
        if (nfields == 3)
        {
            return VW_EINVAL;
        }
        p++;
    }

    end = skip_spaces(p);
    if (*end != '\0')
    {
        return VW_EINVAL;
    }

    if (nfields == 1)
    {
        seconds = fields[0];
    }
    else if (nfields == 2)
    {
        if (fields[1] >= 60.0)
        {
            return VW_EINVAL;
        }
        seconds = fields[0] * 60.0 + fields[1];
    }
    else
    {
        if (fields[1] >= 60.0 || fields[2] >= 60.0)
        {
            return VW_EINVAL;
        }
        seconds = fields[0] * 3600.0 + fields[1] * 60.0 + fields[2];
    }

    if (seconds < 0.0)
    {
        return VW_EINVAL;
    }

    *out_seconds = seconds;
    return 0;
}

int vw_parse_ranges(const char *s, vw_range *buf, size_t cap, size_t *out_n)
{
    const char *p = NULL;
    size_t n = 0;
    size_t limit = 0;

    if (s == NULL || buf == NULL || out_n == NULL)
    {
        return VW_EINVAL;
    }

    p = skip_spaces(s);
    if (*p == '\0')
    {
        return VW_EINVAL;
    }

    limit = cap;
    if (limit > (size_t)VW_MAX_RANGES)
    {
        limit = (size_t)VW_MAX_RANGES;
    }

    while (*p != '\0')
    {
        char start_tok[64];
        char end_tok[64];
        size_t slen = 0;
        size_t elen = 0;
        vw_range range = {0.0, 0.0};
        const char *hyphen = NULL;
        const char *q = NULL;

        p = skip_spaces(p);
        if (*p == '\0')
        {
            break;
        }

        q = p;
        hyphen = NULL;
        if (*p == '-')
        {
            /* `-20` → from 0 to 20. */
            hyphen = p;
            q = p + 1;
            while (*q != '\0' && *q != ',')
            {
                q++;
            }
        }
        else
        {
            while (*q != '\0' && *q != ',')
            {
                if (*q == '-' && hyphen == NULL)
                {
                    hyphen = q;
                }
                q++;
            }
        }

        if (hyphen == NULL)
        {
            return VW_EINVAL;
        }

        slen = (size_t)(hyphen - p);
        elen = (size_t)(q - hyphen - 1);
        if (slen >= sizeof(start_tok) || elen >= sizeof(end_tok))
        {
            return VW_EINVAL;
        }
        if (slen == 0 && elen == 0)
        {
            return VW_EINVAL;
        }

        if (slen == 0)
        {
            range.start_s = 0.0;
        }
        else
        {
            memcpy(start_tok, p, slen);
            start_tok[slen] = '\0';
            if (vw_parse_timestamp(start_tok, &range.start_s) != 0)
            {
                return VW_EINVAL;
            }
        }

        if (elen == 0)
        {
            range.end_s = VW_RANGE_UNTIL_EOF;
        }
        else
        {
            memcpy(end_tok, hyphen + 1, elen);
            end_tok[elen] = '\0';
            if (vw_parse_timestamp(end_tok, &range.end_s) != 0)
            {
                return VW_EINVAL;
            }
            if (range.start_s >= range.end_s)
            {
                return VW_EINVAL;
            }
        }

        if (n >= limit)
        {
            return VW_ENOSPC;
        }
        buf[n] = range;
        n++;

        p = q;
        if (*p == ',')
        {
            p++;
            if (*p == '\0')
            {
                return VW_EINVAL;
            }
        }
    }

    if (n == 0)
    {
        return VW_EINVAL;
    }

    *out_n = n;
    return 0;
}

void vw_resolve_ranges(vw_range *buf, size_t n, double duration_s)
{
    size_t i = 0;
    double cap = 1000000000000.0;

    if (buf == NULL)
    {
        return;
    }
    if (duration_s > 0.0)
    {
        cap = duration_s;
    }
    for (i = 0; i < n; i++)
    {
        if (buf[i].start_s < 0.0)
        {
            buf[i].start_s = 0.0;
        }
        if (buf[i].end_s < 0.0 || (duration_s > 0.0 && buf[i].end_s > duration_s))
        {
            buf[i].end_s = cap;
        }
    }
}

int vw_looks_like_ranges(const char *s)
{
    vw_range buf[VW_MAX_RANGES];
    size_t n = 0;

    if (s == NULL || s[0] == '\0')
    {
        return 0;
    }
    if (vw_parse_ranges(s, buf, VW_MAX_RANGES, &n) != 0)
    {
        return 0;
    }
    return 1;
}

static int copy_ranges_tail(const char *tail, char *ranges, size_t ranges_cap)
{
    size_t len = 0;

    if (tail == NULL || *tail == '\0')
    {
        if (ranges != NULL && ranges_cap > 0)
        {
            ranges[0] = '\0';
        }
        return 0;
    }

    if (!vw_looks_like_ranges(tail))
    {
        return VW_EINVAL;
    }

    if (ranges == NULL)
    {
        return 0;
    }

    len = strlen(tail);
    if (len + 1 > ranges_cap)
    {
        return VW_ENOSPC;
    }
    memcpy(ranges, tail, len + 1);
    return 0;
}

int vw_parse_crop_spec(const char *s, vw_crop *crop, char *ranges, size_t ranges_cap)
{
    const char *p = NULL;
    vw_crop out = {0, 0, 0, 0, 0};
    int saw_x = 0;

    if (s == NULL || crop == NULL)
    {
        return VW_EINVAL;
    }

    p = skip_spaces(s);
    if (parse_nonneg_int(&p, &out.width) != 0 || out.width < 1)
    {
        return VW_EINVAL;
    }

    if (*p == 'x' || *p == 'X')
    {
        saw_x = 1;
        p++;
    }
    else if (*p == ':')
    {
        p++;
    }
    else
    {
        return VW_EINVAL;
    }

    if (parse_nonneg_int(&p, &out.height) != 0 || out.height < 1)
    {
        return VW_EINVAL;
    }

    if (*p == '\0' || *p == ':')
    {
        if (saw_x)
        {
            /* WxH or WxH:RANGES */
            out.centered = 1;
            out.x = 0;
            out.y = 0;
            if (*p == ':')
            {
                int rc = copy_ranges_tail(p + 1, ranges, ranges_cap);
                if (rc != 0)
                {
                    return rc;
                }
            }
            else if (ranges != NULL && ranges_cap > 0)
            {
                ranges[0] = '\0';
            }
            *crop = out;
            return 0;
        }

        /* W:H or W:H:X:Y… */
        if (*p == '\0')
        {
            out.centered = 1;
            if (ranges != NULL && ranges_cap > 0)
            {
                ranges[0] = '\0';
            }
            *crop = out;
            return 0;
        }

        /* W:H:X:Y[:RANGES] */
        p++;
        if (parse_nonneg_int(&p, &out.x) != 0)
        {
            return VW_EINVAL;
        }
        if (*p != ':')
        {
            return VW_EINVAL;
        }
        p++;
        if (parse_nonneg_int(&p, &out.y) != 0)
        {
            return VW_EINVAL;
        }
        out.centered = 0;
        if (*p == ':')
        {
            int rc = copy_ranges_tail(p + 1, ranges, ranges_cap);
            if (rc != 0)
            {
                return rc;
            }
        }
        else if (*p == '\0')
        {
            if (ranges != NULL && ranges_cap > 0)
            {
                ranges[0] = '\0';
            }
        }
        else
        {
            return VW_EINVAL;
        }
        *crop = out;
        return 0;
    }

    if (saw_x && *p == '+')
    {
        p++;
        if (parse_nonneg_int(&p, &out.x) != 0)
        {
            return VW_EINVAL;
        }
        if (*p != '+')
        {
            return VW_EINVAL;
        }
        p++;
        if (parse_nonneg_int(&p, &out.y) != 0)
        {
            return VW_EINVAL;
        }
        out.centered = 0;
        if (*p == ':')
        {
            int rc = copy_ranges_tail(p + 1, ranges, ranges_cap);
            if (rc != 0)
            {
                return rc;
            }
        }
        else if (*p == '\0')
        {
            if (ranges != NULL && ranges_cap > 0)
            {
                ranges[0] = '\0';
            }
        }
        else
        {
            return VW_EINVAL;
        }
        *crop = out;
        return 0;
    }

    return VW_EINVAL;
}

int vw_parse_speed_spec(const char *s, double *factor, char *ranges, size_t ranges_cap)
{
    const char *p = NULL;
    double value = 0.0;

    if (s == NULL || factor == NULL)
    {
        return VW_EINVAL;
    }

    p = skip_spaces(s);
    if (parse_nonneg_double(&p, &value) != 0)
    {
        return VW_EINVAL;
    }
    if (value <= 0.0)
    {
        return VW_EINVAL;
    }

    p = skip_spaces(p);
    if (*p == ':')
    {
        int rc = copy_ranges_tail(p + 1, ranges, ranges_cap);
        if (rc != 0)
        {
            return rc;
        }
    }
    else if (*p == '\0')
    {
        if (ranges != NULL && ranges_cap > 0)
        {
            ranges[0] = '\0';
        }
    }
    else
    {
        return VW_EINVAL;
    }

    *factor = value;
    return 0;
}

static int parse_one_zoom_seg(const char *s, vw_zoom_seg *out)
{
    char head[256];
    const char *range_at = NULL;
    const char *p = NULL;
    char *at = NULL;
    size_t hlen = 0;
    vw_zoom_seg z;
    vw_range rng[VW_MAX_RANGES];
    size_t nr = 0;

    if (s == NULL || out == NULL || *s == '\0')
    {
        return VW_EINVAL;
    }

    z.z0 = 1.0;
    z.z1 = 1.0;
    z.cx = 0.5;
    z.cy = 0.5;
    z.range.start_s = 0.0;
    z.range.end_s = VW_RANGE_UNTIL_EOF;

    /* Last ':' whose tail is a range list is the time window. */
    p = s;
    while (*p != '\0')
    {
        if (*p == ':' && vw_looks_like_ranges(p + 1))
        {
            range_at = p;
        }
        p++;
    }

    if (range_at != NULL)
    {
        hlen = (size_t)(range_at - s);
        if (hlen == 0 || hlen >= sizeof(head))
        {
            return VW_EINVAL;
        }
        memcpy(head, s, hlen);
        head[hlen] = '\0';
        if (vw_parse_ranges(range_at + 1, rng, VW_MAX_RANGES, &nr) != 0 || nr < 1)
        {
            return VW_EINVAL;
        }
        z.range = rng[0];
    }
    else
    {
        hlen = strlen(s);
        if (hlen >= sizeof(head))
        {
            return VW_EINVAL;
        }
        memcpy(head, s, hlen + 1);
    }

    at = strchr(head, '@');
    if (at != NULL)
    {
        const char *c = at + 1;
        if (parse_nonneg_double(&c, &z.cx) != 0)
        {
            return VW_EINVAL;
        }
        if (*c != ',')
        {
            return VW_EINVAL;
        }
        c++;
        if (parse_nonneg_double(&c, &z.cy) != 0)
        {
            return VW_EINVAL;
        }
        if (*c != '\0')
        {
            return VW_EINVAL;
        }
        if (z.cx > 1.0 || z.cy > 1.0)
        {
            return VW_EINVAL;
        }
        *at = '\0';
    }

    p = head;
    if (parse_nonneg_double(&p, &z.z0) != 0)
    {
        return VW_EINVAL;
    }
    if (*p == '-')
    {
        p++;
        if (parse_nonneg_double(&p, &z.z1) != 0)
        {
            return VW_EINVAL;
        }
    }
    else
    {
        z.z1 = z.z0;
    }
    if (*p != '\0')
    {
        return VW_EINVAL;
    }
    if (z.z0 < 1.0 || z.z1 < 1.0 || z.z0 > 8.0 || z.z1 > 8.0)
    {
        return VW_EINVAL;
    }

    *out = z;
    return 0;
}

int vw_parse_zoom_spec(const char *s, vw_zoom_seg *buf, size_t cap, size_t *out_n)
{
    const char *p = NULL;
    size_t n = 0;
    size_t limit = 0;

    if (s == NULL || buf == NULL || out_n == NULL)
    {
        return VW_EINVAL;
    }

    p = skip_spaces(s);
    if (*p == '\0')
    {
        return VW_EINVAL;
    }

    limit = cap;
    if (limit > (size_t)VW_MAX_ZOOMS)
    {
        limit = (size_t)VW_MAX_ZOOMS;
    }

    while (*p != '\0')
    {
        char tok[256];
        size_t tlen = 0;
        const char *q = p;
        vw_zoom_seg seg;

        while (*q != '\0' && *q != ';')
        {
            q++;
        }
        tlen = (size_t)(q - p);
        if (tlen == 0 || tlen >= sizeof(tok))
        {
            return VW_EINVAL;
        }
        memcpy(tok, p, tlen);
        tok[tlen] = '\0';
        if (parse_one_zoom_seg(tok, &seg) != 0)
        {
            return VW_EINVAL;
        }
        if (n >= limit)
        {
            return VW_ENOSPC;
        }
        buf[n] = seg;
        n++;
        p = q;
        if (*p == ';')
        {
            p++;
            if (*p == '\0')
            {
                return VW_EINVAL;
            }
        }
    }

    if (n == 0)
    {
        return VW_EINVAL;
    }
    *out_n = n;
    return 0;
}

static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int vw_parse_hex_color(const char *s, char *out, size_t cap)
{
    const char *p = NULL;
    size_t n = 0;
    size_t i = 0;

    if (s == NULL || out == NULL)
    {
        return VW_EINVAL;
    }
    p = skip_spaces(s);
    if (*p == '#')
    {
        p++;
    }
    while (is_hex_digit(p[n]))
    {
        n++;
    }
    if ((n != 6 && n != 8) || p[n] != '\0')
    {
        return VW_EINVAL;
    }
    if (cap < n + 2)
    {
        return VW_ENOSPC;
    }
    out[0] = '#';
    for (i = 0; i < n; i++)
    {
        char c = p[i];
        if (c >= 'A' && c <= 'F')
        {
            c = (char)(c - 'A' + 'a');
        }
        out[i + 1] = c;
    }
    out[n + 1] = '\0';
    return 0;
}

int vw_parse_text_spec(const char *s, vw_text *out)
{
    const char *end = NULL;
    const char *pos_at = NULL;
    const char *range_colon = NULL;
    const char *body_end = NULL;
    size_t tlen = 0;
    vw_text t;

    if (s == NULL || out == NULL)
    {
        return VW_EINVAL;
    }

    memset(&t, 0, sizeof(t));
    t.x = 0;
    t.y = 0;
    t.has_range = 0;
    t.range.start_s = 0.0;
    t.range.end_s = VW_RANGE_UNTIL_EOF;

    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
    {
        end--;
    }
    if (end == s)
    {
        return VW_EINVAL;
    }

    /* Optional +X+Y at the end. */
    {
        const char *q = end;
        const char *plus2 = NULL;
        const char *plus1 = NULL;
        while (q > s && q[-1] >= '0' && q[-1] <= '9')
        {
            q--;
        }
        if (q > s && q[-1] == '+')
        {
            plus2 = q - 1;
            q--;
            while (q > s && q[-1] >= '0' && q[-1] <= '9')
            {
                q--;
            }
            if (q > s && q[-1] == '+')
            {
                plus1 = q - 1;
            }
        }
        if (plus1 != NULL && plus2 != NULL && plus2 > plus1 + 1)
        {
            int x = 0;
            int y = 0;
            const char *px = plus1 + 1;
            const char *py = plus2 + 1;
            if (parse_nonneg_int(&px, &x) == 0 && px == plus2 && parse_nonneg_int(&py, &y) == 0 &&
                py == end)
            {
                t.x = x;
                t.y = y;
                pos_at = plus1;
            }
        }
    }

    body_end = pos_at != NULL ? pos_at : end;

    /* Optional :RANGE immediately before position (or end). */
    {
        const char *colon = NULL;
        const char *q = body_end;
        while (q > s)
        {
            q--;
            if (*q == ':')
            {
                colon = q;
                break;
            }
        }
        if (colon != NULL && colon + 1 < body_end)
        {
            char rbuf[VW_MAX_RANGES_CHARS];
            size_t rlen = (size_t)(body_end - colon - 1);
            vw_range rng[VW_MAX_RANGES];
            size_t nr = 0;
            if (rlen > 0 && rlen < sizeof(rbuf))
            {
                memcpy(rbuf, colon + 1, rlen);
                rbuf[rlen] = '\0';
                if (vw_parse_ranges(rbuf, rng, VW_MAX_RANGES, &nr) == 0 && nr >= 1)
                {
                    range_colon = colon;
                    t.has_range = 1;
                    t.range = rng[0];
                }
            }
        }
    }

    {
        const char *text_end = range_colon != NULL ? range_colon : body_end;
        tlen = (size_t)(text_end - s);
        if (tlen == 0 || tlen > (size_t)VW_MAX_TEXT)
        {
            return VW_EINVAL;
        }
        memcpy(t.text, s, tlen);
        t.text[tlen] = '\0';
    }

    *out = t;
    return 0;
}

int vw_frame_index_width(uint64_t frame_count)
{
    int width = 0;

    if (frame_count < 10ULL)
    {
        return 1;
    }

    while (frame_count > 0ULL)
    {
        width++;
        frame_count /= 10ULL;
    }
    return width;
}

int vw_ranges_cover(const vw_range *ranges, size_t n, double t_s)
{
    size_t i = 0;

    if (n == 0)
    {
        return 1;
    }
    if (ranges == NULL)
    {
        return 0;
    }

    for (i = 0; i < n; i++)
    {
        const double end = ranges[i].end_s < 0.0 ? 1000000000000.0 : ranges[i].end_s;
        if (t_s >= ranges[i].start_s && t_s < end)
        {
            return 1;
        }
    }
    return 0;
}

int vw_format_frame_name(char *dst, size_t cap, const char *prefix, int width,
                         uint64_t index)
{
    size_t plen = 0;
    char digits[32];
    int nd = 0;
    int pad = 0;
    uint64_t x = 0;
    size_t need = 0;
    size_t pos = 0;
    int i = 0;

    if (dst == NULL || cap < 1 || prefix == NULL || width < 1 || index < 1ULL)
    {
        return VW_EINVAL;
    }

    plen = strlen(prefix);
    x = index;
    nd = 0;
    while (x > 0ULL && nd < 32)
    {
        digits[nd] = (char)('0' + (int)(x % 10ULL));
        nd++;
        x /= 10ULL;
    }
    pad = width - nd;
    if (pad < 0)
    {
        pad = 0;
    }

    /* prefix + '_' + zero-pad + digits + ".png" + NUL */
    need = plen + 1U + (size_t)pad + (size_t)nd + 4U + 1U;
    if (need > cap)
    {
        return VW_ENOSPC;
    }

    memcpy(dst, prefix, plen);
    pos = plen;
    dst[pos] = '_';
    pos++;
    for (i = 0; i < pad; i++)
    {
        dst[pos] = '0';
        pos++;
    }
    for (i = nd - 1; i >= 0; i--)
    {
        dst[pos] = digits[i];
        pos++;
    }
    dst[pos] = '.';
    pos++;
    dst[pos] = 'p';
    pos++;
    dst[pos] = 'n';
    pos++;
    dst[pos] = 'g';
    pos++;
    dst[pos] = '\0';
    return 0;
}
