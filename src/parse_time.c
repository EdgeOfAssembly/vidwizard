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
        while (*q != '\0' && *q != ',')
        {
            if (*q == '-' && hyphen == NULL && q != p)
            {
                hyphen = q;
            }
            q++;
        }

        if (hyphen == NULL)
        {
            return VW_EINVAL;
        }

        slen = (size_t)(hyphen - p);
        elen = (size_t)(q - hyphen - 1);
        if (slen == 0 || elen == 0 || slen >= sizeof(start_tok) || elen >= sizeof(end_tok))
        {
            return VW_EINVAL;
        }

        memcpy(start_tok, p, slen);
        start_tok[slen] = '\0';
        memcpy(end_tok, hyphen + 1, elen);
        end_tok[elen] = '\0';

        if (vw_parse_timestamp(start_tok, &range.start_s) != 0)
        {
            return VW_EINVAL;
        }
        if (vw_parse_timestamp(end_tok, &range.end_s) != 0)
        {
            return VW_EINVAL;
        }
        if (range.start_s >= range.end_s)
        {
            return VW_EINVAL;
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
        if (t_s >= ranges[i].start_s && t_s < ranges[i].end_s)
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
