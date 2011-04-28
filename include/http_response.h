/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 */

HTTP_RESP(101, "Switching Protocols")
HTTP_RESP(200, "OK")
HTTP_RESP(201, "Created")
HTTP_RESP(202, "Accepted")
HTTP_RESP(203, "Non-Authoritative Information")
HTTP_RESP(204, "No Content")
HTTP_RESP(205, "Reset Content")
HTTP_RESP(206, "Partial Content")
HTTP_RESP(300, "Multiple Choices")
HTTP_RESP(301, "Moved Permanently")
HTTP_RESP(302, "Found")
HTTP_RESP(303, "See Other")
HTTP_RESP(304, "Not Modified")
HTTP_RESP(305, "Use Proxy")
HTTP_RESP(306, "(Unused)")
HTTP_RESP(307, "Temporary Redirect")
HTTP_RESP(400, "Bad Request")
HTTP_RESP(401, "Unauthorized")
HTTP_RESP(402, "Payment Required")
HTTP_RESP(403, "Forbidden")
HTTP_RESP(404, "Not Found")
HTTP_RESP(405, "Method Not Allowed")
HTTP_RESP(406, "Not Acceptable")
HTTP_RESP(407, "Proxy Authentication Required")
HTTP_RESP(408, "Request Timeout")
HTTP_RESP(409, "Conflict")
HTTP_RESP(410, "Gone")
HTTP_RESP(411, "Length Required")
HTTP_RESP(412, "Precondition Failed")
HTTP_RESP(413, "Request Entity Too Large")
HTTP_RESP(414, "Request-URI Too Long")
HTTP_RESP(415, "Unsupported Media Type")
HTTP_RESP(416, "Requested Range Not Satisfiable")
HTTP_RESP(417, "Expectation Failed")
HTTP_RESP(500, "Internal Server Error")
HTTP_RESP(501, "Not Implemented")
HTTP_RESP(502, "Bad Gateway")
HTTP_RESP(503, "Service Unavailable")
HTTP_RESP(504, "Gateway Timeout")
HTTP_RESP(505, "HTTP Version Not Supported")
