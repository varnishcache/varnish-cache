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

/*lint -save -e525 -e539 */

/* Decode integer with prefix */
#define VHD_FSM_INTEGER(STATE, pfx)				\
	VHD_FSM(STATE, INTEGER, pfx, 0)
/* Goto */
#define VHD_FSM_GOTO(STATE, TO)					\
	VHD_FSM(STATE, GOTO, VHD_S_##TO, 0)
/* Label */
#define VHD_FSM_LABEL(STATE)					\
	VHD_FSM(STATE, SKIP, 0, 0)
/* Set max table size */
#define VHD_FSM_SET_MAX(STATE)					\
	VHD_FSM(STATE, SET_MAX, 0, 0)
/* Set index */
#define VHD_FSM_SET_IDX(STATE)					\
	VHD_FSM(STATE, SET_IDX, 0, 0)
/* Table lookup from index */
#define VHD_FSM_LOOKUP(STATE, type)				\
	VHD_FSM(STATE, LOOKUP, type, 0)
/* New table entry */
#define VHD_FSM_NEW(STATE)					\
	VHD_FSM(STATE, NEW, 0, 0)
/* New table entry, name from index */
#define VHD_FSM_NEW_IDX(STATE)					\
	VHD_FSM(STATE, NEW_IDX, 0, 0)
/* Branch if index is zero */
#define VHD_FSM_BRANCH_ZIDX(STATE, BRANCH)			\
	VHD_FSM(STATE, BRANCH_ZIDX, VHD_S_##BRANCH, 0)
/* Branch if bit 0 */
#define VHD_FSM_BRANCH_BIT0(STATE, BRANCH)			\
	VHD_FSM(STATE, BRANCH_BIT0, VHD_S_##BRANCH, 0)

/* Decode a literal:
 *   0   1   2   3   4   5   6   7
 * +---+---+-----------------------+
 * | H |        Length (7+)        |
 * +---+---------------------------+
 * |     String (Length octets)    |
 * +-------------------------------+
 */
#define VHD_FSM_LITERAL(STATE, type, flags)			\
	VHD_FSM_BRANCH_BIT0(STATE, STATE##_HUFFMAN_LEN)		\
	VHD_FSM_INTEGER(STATE##_RAW_LEN, 7)			\
	VHD_FSM(STATE##_RAW, RAW, type, flags)			\
	VHD_FSM_GOTO(STATE##_RAW_FINISH, STATE##_EXIT)		\
	VHD_FSM_INTEGER(STATE##_HUFFMAN_LEN, 7)			\
	VHD_FSM(STATE##_HUFFMAN, HUFFMAN, type, flags)		\
	VHD_FSM_LABEL(STATE##_EXIT)

/* The idle state */
VHD_FSM(IDLE, IDLE, 0, 0)

/* Indexed header field
 * (RFC 7541 section 6.1)
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 1 | Index(7+)                 |
 * +---+---------------------------+
 */
VHD_FSM_LABEL(HP61_START)
VHD_FSM_INTEGER(HP61_IDX, 7)
VHD_FSM_SET_IDX(HP61_SET_IDX)
VHD_FSM_LOOKUP(HP61_NAME, VHD_NAME)
VHD_FSM_LOOKUP(HP61_VAL, VHD_VALUE)
VHD_FSM_GOTO(HP61_FINISH, IDLE)

/* Literal header field with incremental indexing
 * (RFC 7541 section 6.2.1)
 *
 * HP621_IN - Indexed name
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 0 | 1 |      Index (6+)       |
 * +---+---+-----------------------+
 * | H |     Value Length (7+)     |
 * +---+---------------------------+
 * | Value String (Length octets)  |
 * +-------------------------------+
 *
 * HP621_NN - New name
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 0 | 1 |           0           |
 * +---+---+-----------------------+
 * | H |     Name Length (7+)      |
 * +---+---------------------------+
 * |  Name String (Length octets)  |
 * +---+---------------------------+
 * | H |     Value Length (7+)     |
 * +---+---------------------------+
 * | Value String (Length octets)  |
 * +-------------------------------+
 */
VHD_FSM_LABEL(HP621_START)
VHD_FSM_INTEGER(HP621_IDX, 6)
VHD_FSM_SET_IDX(HP621_SET_IDX)
VHD_FSM_BRANCH_ZIDX(HP621_BRANCH_ZIDX, HP621_NN_NEW)
/* HP621_IN - Indexed name */
VHD_FSM_LOOKUP(HP621_IN_NAME, VHD_NAME)
VHD_FSM_NEW_IDX(HP621_IN_NEW)
VHD_FSM_LITERAL(HP621_IN_VAL, VHD_VALUE, VHD_INCREMENTAL)
VHD_FSM_GOTO(HP621_IN_FINISH, IDLE)
/* HP621_NN - New name */
VHD_FSM_NEW(HP621_NN_NEW)
VHD_FSM_LITERAL(HP621_NN_NAME, VHD_NAME, VHD_INCREMENTAL)
VHD_FSM_LITERAL(HP621_NN_VAL, VHD_VALUE, VHD_INCREMENTAL)
VHD_FSM_GOTO(HP621_NN_FINISH, IDLE)

/* Literal header field without indexing
 * (RFC 7541 section 622)
 *
 * HP622_IN - Indexed name
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 0 | 0 | 0 | 0 |  Index (4+)   |
 * +---+---+-----------------------+
 * | H |     Value Length (7+)     |
 * +---+---------------------------+
 * | Value String (Length octets)  |
 * +-------------------------------+
 *
 * HP622_NN - New name
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 0 | 0 | 0 | 0 |       0       |
 * +---+---+-----------------------+
 * | H |     Name Length (7+)      |
 * +---+---------------------------+
 * |  Name String (Length octets)  |
 * +---+---------------------------+
 * | H |     Value Length (7+)     |
 * +---+---------------------------+
 * | Value String (Length octets)  |
 * +-------------------------------+
 */
VHD_FSM_LABEL(HP622_START)
VHD_FSM_INTEGER(HP622_IDX, 4)
VHD_FSM_SET_IDX(HP622_SET_IDX)
VHD_FSM_BRANCH_ZIDX(HP622_BR_ZIDX, HP622_NN_NAME)
/* HP622_IN - Indexed name */
VHD_FSM_LOOKUP(HP622_IN_NAME, VHD_NAME)
VHD_FSM_LITERAL(HP622_IN_VAL, VHD_VALUE, 0)
VHD_FSM_GOTO(HP622_IN_FINISH, IDLE)
/* HP622_NN - New name */
VHD_FSM_LITERAL(HP622_NN_NAME, VHD_NAME, 0)
VHD_FSM_LITERAL(HP622_NN_VAL, VHD_VALUE, 0)
VHD_FSM_GOTO(HP622_NN_FINISH, IDLE)

/* Literal header field never indexed
 * (RFC 7541 section 6.2.3)
 *
 * HP623_IN - Indexed name
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 0 | 0 | 0 | 1 |  Index (4+)   |
 * +---+---+-----------------------+
 * | H |     Value Length (7+)     |
 * +---+---------------------------+
 * | Value String (Length octets)  |
 * +-------------------------------+
 *
 * HP623_NN - New name
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 0 | 0 | 0 | 1 |       0       |
 * +---+---+-----------------------+
 * | H |     Name Length (7+)      |
 * +---+---------------------------+
 * |  Name String (Length octets)  |
 * +---+---------------------------+
 * | H |     Value Length (7+)     |
 * +---+---------------------------+
 * | Value String (Length octets)  |
 * +-------------------------------+
 */
VHD_FSM_LABEL(HP623_START)
VHD_FSM_INTEGER(HP623_IDX, 4)
VHD_FSM_SET_IDX(HP623_SET_IDX)
VHD_FSM_BRANCH_ZIDX(HP623_BR_ZIDX, HP623_NN_NAME)
/* HP623_IN - Indexed name */
VHD_FSM_LOOKUP(HP623_IN_NAME, VHD_NAME_SEC)
VHD_FSM_LITERAL(HP623_IN_VAL, VHD_VALUE_SEC, 0)
VHD_FSM_GOTO(HP623_IN_FINISH, IDLE)
/* HP623_NN - New name */
VHD_FSM_LITERAL(HP623_NN_NAME, VHD_NAME_SEC, 0)
VHD_FSM_LITERAL(HP623_NN_VAL, VHD_VALUE_SEC, 0)
VHD_FSM_GOTO(HP623_NN_FINISH, IDLE)

/* Dynamic table size update
 * (RFC 7541 section 6.3)
 *
 *   0   1   2   3   4   5   6   7
 * +---+---+---+---+---+---+---+---+
 * | 0 | 0 | 1 |   Max size (5+)   |
 * +---+---------------------------+
 */
VHD_FSM_LABEL(HP63_START)
VHD_FSM_INTEGER(HP63_SIZE, 5)
VHD_FSM_SET_MAX(HP63_SET_MAX)
VHD_FSM_GOTO(HP63_FINISH, IDLE)

/*---------------------------------------------------------------------*/
/* States used for unit testing */

#ifdef DECODE_TEST_DRIVER

/* Test integer prefix 5 */
VHD_FSM_LABEL(TEST_INT5)
VHD_FSM_INTEGER(TEST_INT5_INT, 5)
VHD_FSM_GOTO(TEST_INT5_FINISH, IDLE)

/* Test literal decoding */
VHD_FSM_LABEL(TEST_LITERAL)
VHD_FSM_LITERAL(TEST_LITERAL_NAME, VHD_NAME, 0)
VHD_FSM_LITERAL(TEST_LITERAL_VALUE, VHD_VALUE, 0)
VHD_FSM_GOTO(TEST_LITERAL_FINISH, IDLE)

#endif	/* DECODE_TEST_DRIVER */

/*---------------------------------------------------------------------*/
/* Clean up macro namespace */
#undef VHD_FSM_INTEGER
#undef VHD_FSM_GOTO
#undef VHD_FSM_LABEL
#undef VHD_FSM_SET_MAX
#undef VHD_FSM_SET_IDX
#undef VHD_FSM_LOOKUP
#undef VHD_FSM_NEW
#undef VHD_FSM_NEW_IDX
#undef VHD_FSM_BRANCH_ZIDX
#undef VHD_FSM_BRANCH_BIT0
#undef VHD_FSM_LITERAL

#undef VHD_FSM

/*lint -restore */
