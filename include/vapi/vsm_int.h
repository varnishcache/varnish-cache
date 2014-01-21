/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Define the layout of the shared memory log segment.
 *
 * NB: THIS IS NOT A PUBLIC API TO VARNISH!
 *
 * There is a lot of diplomacy and protocol involved with the VSM segment
 * since there is no way to (and no desire to!) lock between the readers
 * and the writer.
 *
 * In particular we want the readers to seamlessly jump from one VSM instance
 * to another when the child restarts.
 *
 * The VSM segment life-cycle is:
 *
 *	Manager creates VSM file under temp name
 *
 *	Temp VSM file is initialized such that VSM_head is consistent
 *	with a non-zero alloc_seq
 *
 *	Manager renames Temp VSM file to correct filename as atomic
 *	operation.
 *
 *	When manager abandons VSM file, alloc_seq is set to zero, which
 *	never happens in any other circumstances.
 *
 *	If a manager is started and finds and old abandoned VSM segment
 *	it will zero the alloc_seq in it, before replacing the file.
 *
 * Subscribers will have to monitor three things to make sure they look at
 * the right thing: The alloc_seq field, the age counter and the dev+inode
 * of the path-name.  The former check is by far the cheaper, the second
 * can be used to check that Varnishd is still alive and the last check
 * should only be employed when lack of activity in the VSM segment raises
 * suspicion that something has happened.
 *
 * The allocations ("chunks") in the VSM forms a linked list, starting with
 * VSM_head->first, with the first/next fields being byte offsets relative
 * to the start of the VSM segment.
 *
 * The last chunk on the list, has next == 0.
 *
 * New chunks are appended to the list, no matter where in the VSM
 * they happen to be allocated.
 *
 * Chunk allocation sequence is:
 *	Find free space
 *	Zero payload
 *	Init Chunk header
 *	Write memory barrier
 *	update hdr->first or $last->next pointer
 *	hdr->alloc_seq changes
 *	Write memory barrier
 *
 * Chunk contents should be designed so that zero bytes are not mistaken
 * for valid contents.
 *
 * Chunk deallocation sequence is:
 *	update hdr->first or $prev->next pointer
 *	Write memory barrier
 *	this->len = 0
 *	hdr->alloc_seq changes
 *	Write memory barrier
 *
 * The space occupied by the chunk is put on a cooling list and is not
 * recycled for at least a minute.
 *
 */

#ifndef VSM_INT_H_INCLUDED
#define VSM_INT_H_INCLUDED

#define VSM_FILENAME		"_.vsm"
#define VSM_MARKER_LEN	8
#define VSM_IDENT_LEN	128

struct VSM_chunk {
#define VSM_CHUNK_MARKER	"VSMCHUNK"
	char			marker[VSM_MARKER_LEN];
	ssize_t			len;		/* Incl VSM_chunk */
	ssize_t			next;		/* Offset in shmem */
	char			class[VSM_MARKER_LEN];
	char			type[VSM_MARKER_LEN];
	char			ident[VSM_IDENT_LEN];
};

struct VSM_head {
#define VSM_HEAD_MARKER		"VSMHEAD0"	/* Incr. as version# */
	char			marker[VSM_MARKER_LEN];
	ssize_t			hdrsize;
	ssize_t			shm_size;
	ssize_t			first;		/* Offset, first chunk */
	unsigned		alloc_seq;
	uint64_t		age;
};

#endif /* VSM_INT_H_INCLUDED */
