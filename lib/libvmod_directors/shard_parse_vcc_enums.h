/*
 * for the time being, this code is auto-generated outside the varnishd source
 * tree, see
 * https://code.uplex.de/uplex-varnish/libvmod-vslp/blob/shard/src/gen_enum_parse.pl
 *
 * TODO: integrate in vmodtool.py or replace with something else
 */

enum alg_e {
	_ALG_E_INVALID = 0,
	CRC32,
	SHA256,
	RS,
	_ALG_E_MAX
};


enum alg_e parse_alg_e (const char *);

enum by_e {
	_BY_E_INVALID = 0,
	HASH,
	URL,
	KEY,
	BLOB,
	_BY_E_MAX
};


enum by_e parse_by_e (const char *);

enum healthy_e {
	_HEALTHY_E_INVALID = 0,
	CHOSEN,
	IGNORE,
	ALL,
	_HEALTHY_E_MAX
};


enum healthy_e parse_healthy_e (const char *);

