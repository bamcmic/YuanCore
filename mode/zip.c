/* zip.c - 仅 stored 的 zip 只读解析器(实现见 zip.h) */
#include "zip.h"

#include "fs.h"
#include "heap.h"

#define SIG_EOCD 0x06054b50u
#define SIG_CD   0x02014b50u
#define SIG_LFH  0x04034b50u

#define MAX_ENTRIES 256
#define MAX_SIZE    (4 * 1024 * 1024)

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

int zip_load(const char *path, zip_file *zf)
{
    int size, i, found;
    unsigned n, cd_off, cd_size, pos;
    unsigned char *d;

    zf->data = 0;
    zf->entries = 0;
    zf->count = 0;
    zf->size = 0;

    if (!fs_ready() || !fs_exists(path))
        return -1;
    size = fs_size(path);
    if (size < 22 || size > MAX_SIZE)
        return -3;

    d = kmalloc((unsigned)size);
    if (d == 0)
        return -1;
    if (fs_read(path, d, size) != size) {
        kfree(d);
        return -1;
    }

    /* 尾部扫 EOCD(注释最长 256 字节) */
    found = -1;
    for (i = size - 22; i >= 0 && i >= size - 22 - 256; i--) {
        if (rd32(d + i) == SIG_EOCD) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        kfree(d);
        return -2;
    }

    n       = rd16(d + found + 10);
    cd_size = rd32(d + found + 12);
    cd_off  = rd32(d + found + 16);

    if (n == 0 || n > MAX_ENTRIES ||
        cd_off > (unsigned)size || cd_size > (unsigned)size ||
        cd_off + cd_size > (unsigned)size) {
        kfree(d);
        return -2;
    }

    zf->data = d;
    zf->size = (unsigned)size;
    zf->count = (int)n;
    zf->entries = kmalloc(n * sizeof(zip_entry));
    if (zf->entries == 0) {
        zip_free(zf);
        return -1;
    }

    pos = cd_off;
    for (i = 0; i < (int)n; i++) {
        unsigned method, csize, usize, nlen, elen, clen, lho, nl2, el2;
        zip_entry *e = &zf->entries[i];

        if (pos + 46 > cd_off + cd_size || rd32(d + pos) != SIG_CD) {
            zip_free(zf);
            return -2;
        }
        method = rd16(d + pos + 10);
        csize  = rd32(d + pos + 20);
        usize  = rd32(d + pos + 24);
        nlen   = rd16(d + pos + 28);
        elen   = rd16(d + pos + 30);
        clen   = rd16(d + pos + 32);
        lho    = rd32(d + pos + 42);

        if (nlen >= sizeof(e->name)) {
            zip_free(zf);
            return -2;
        }

        e->method = method;
        e->size = usize;
        {
            unsigned k;

            for (k = 0; k < nlen; k++)
                e->name[k] = (char)d[pos + 46 + k];
            e->name[nlen] = '\0';
        }

        /* local header 实测数据偏移 */
        if (lho + 30 > (unsigned)size || rd32(d + lho) != SIG_LFH) {
            zip_free(zf);
            return -2;
        }
        nl2 = rd16(d + lho + 26);
        el2 = rd16(d + lho + 28);
        e->data_off = lho + 30u + nl2 + el2;

        if (e->data_off + csize > (unsigned)size || csize != usize) {
            zip_free(zf);
            return -2;
        }

        pos += 46 + nlen + elen + clen;
    }
    return 0;
}

void zip_free(zip_file *zf)
{
    if (zf->entries)
        kfree(zf->entries);
    if (zf->data)
        kfree(zf->data);
    zf->entries = 0;
    zf->data = 0;
    zf->count = 0;
    zf->size = 0;
}

int zip_find(const zip_file *zf, const char *name)
{
    int i;

    for (i = 0; i < zf->count; i++) {
        const char *a = zf->entries[i].name, *b = name;

        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0)
            return i;
    }
    return -1;
}

int zip_read(const zip_file *zf, int idx, unsigned char *buf, int max)
{
    const zip_entry *e;
    unsigned k;

    if (idx < 0 || idx >= zf->count)
        return -1;
    e = &zf->entries[idx];
    if (e->method != 0)
        return ZIP_E_METHOD;
    {
        int sz = (e->size > (unsigned)max) ? max : (int)e->size;

        for (k = 0; k < (unsigned)sz; k++)
            buf[k] = zf->data[e->data_off + k];
        return sz;
    }
}
