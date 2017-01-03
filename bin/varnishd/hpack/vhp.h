/*-
 * Copyright (c) 2016 Varnish Software
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <stdint.h>

/* VHT - Varnish HPACK Table */

#define VHT_ENTRY_SIZE 32U

struct vht_entry {
	unsigned		magic;
#define	VHT_ENTRY_MAGIC		0xc06dd892
	unsigned		offset;
	unsigned		namelen;
	unsigned		valuelen;
};

struct vht_table {
	unsigned		magic;
#define VHT_TABLE_MAGIC		0x6bbdc683
	unsigned		n;
	unsigned		size;
	unsigned		maxsize; /* n * 32 + size <= maxsize */
	unsigned		protomax;
	unsigned		bufsize;
	char			*buf;
};

void VHT_NewEntry(struct vht_table *);
int VHT_NewEntry_Indexed(struct vht_table *, unsigned);
void VHT_AppendName(struct vht_table *, const char *, ssize_t);
void VHT_AppendValue(struct vht_table *, const char *, ssize_t);
int VHT_SetMaxTableSize(struct vht_table *, size_t);
int VHT_SetProtoMax(struct vht_table *, size_t);
const char *VHT_LookupName(const struct vht_table *, unsigned, size_t *);
const char *VHT_LookupValue(const struct vht_table *, unsigned, size_t *);
int VHT_Init(struct vht_table *, size_t);
void VHT_Fini(struct vht_table *);

/* VHD - Varnish HPACK Decoder */

enum vhd_ret_e {
#define VHD_RET(NAME, VAL, DESC)		\
	VHD_##NAME = VAL,
#include "tbl/vhd_return.h"
};

struct vhd_int {
	uint8_t			magic;
#define VHD_INT_MAGIC		0x05

	uint8_t			pfx;
	uint8_t			m;
	unsigned		v;
};

struct vhd_raw {
	uint8_t			magic;
#define VHD_RAW_MAGIC		0xa0

	unsigned		l;
};

struct vhd_huffman {
	uint8_t			magic;
#define VHD_HUFFMAN_MAGIC	0x56

	uint8_t			blen;
	uint16_t		bits;
	uint16_t		pos;
	unsigned		len;
};

struct vhd_lookup {
	uint8_t			magic;
#define VHD_LOOKUP_MAGIC	0x65

	unsigned		l;
};

struct vhd_decode {
	unsigned		magic;
#define VHD_DECODE_MAGIC	0x9cbc72b2

	unsigned		index;
	uint16_t		state;
	int8_t			error;
	uint8_t			first;

	union {
		struct vhd_int		integer[1];
		struct vhd_lookup	lookup[1];
		struct vhd_raw		raw[1];
		struct vhd_huffman	huffman[1];
	};
};

void VHD_Init(struct vhd_decode *);
enum vhd_ret_e  VHD_Decode(struct vhd_decode *, struct vht_table *,
    const uint8_t *in, size_t inlen, size_t *p_inused,
    char *out, size_t outlen, size_t *p_outused);
const char *VHD_Error(enum vhd_ret_e);
