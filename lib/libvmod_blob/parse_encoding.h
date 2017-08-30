/*
 * for the time being, this code is auto-generated outside the varnishd source
 * tree, see
 * https://code.uplex.de/uplex-varnish/libvmod-blobcode/blob/master/src/gen_enum_parse.pl
 *
 * TODO: integrate in vmodtool.py or replace with something else
 * cf. the same TODO for the shard director in libvmod_directors
 */

enum encoding {
	_INVALID = 0,
	IDENTITY,
	BASE64,
	BASE64URL,
	BASE64URLNOPAD,
	HEX,
	HEXUC,
	HEXLC,
	URL,
	URLLC,
	URLUC,
	__MAX_ENCODING
};

enum encoding parse_encoding (const char *);

