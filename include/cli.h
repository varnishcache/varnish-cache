/*
 * $Id$
 */

#define CLI_URL_QUERY							\
	"url.query",							\
	"url.query <url>",						\
	"\tQuery the cache status of a specific URL.\n"			\
	    "\tReturns the TTL, size and checksum of the object." 

#define CLI_URL_PURGE							\
	"url.purge",							\
	"url.purge <regexp>",						\
	"\tAll urls matching regexp will consider currently cached\n"	\
	    "\tobjects obsolete"

#define CLI_URL_STATUS							\
	"url.status",							\
	"url.status <url>",						\
	"\tReturns all metadata for the specified URL"

#define CLI_CONFIG_LOAD							\
	"config.load",							\
	"config.load <configname> <filename>",				\
	"\tCompile and load the VCL file under the name provided."

#define CLI_CONFIG_INLINE						\
	"config.inline",						\
	"config.inline <configname> <quoted_VCLstring>",		\
	"\tCompile and load the VCL data under the name provided." 

#define CLI_CONFIG_UNLOAD						\
	"config.unload",						\
	"config.unload <configname>",					\
	"\tUnload the named configuration (when possible)."

#define CLI_CONFIG_LIST							\
	"config.list",							\
	"config.list",							\
	"\tList all loaded configuration."

#define CLI_CONFIG_USE							\
	"config.use",							\
	"config.use <configname>",					\
	"\tSwitch to the named configuration immediately."

#define CLI_SERVER_FREEZE						\
	"server.freeze",						\
	"server.freeze",						\
	"\tStop the clock, freeze object store."

#define CLI_SERVER_THAW							\
	"thaw",								\
	"thaw",								\
	"\tRestart the clock, unfreeze object store."

#define CLI_SERVER_SUSPEND						\
	"suspend",							\
	"suspend",							\
	"\tStop accepting requests."

#define CLI_SERVER_RESUME						\
	"resume",							\
	"resume",							\
	"\tAccept requests."

#define CLI_SERVER_STOP							\
	"stop",								\
	"stop",								\
	"\tStop the Varnish cache process"

#define CLI_SERVER_START						\
	"start",							\
	"start",							\
	"\tStart the Varnish cache process."

#define CLI_SERVER_RESTART						\
	"restart",							\
	"restart",							\
	"\tRestart the Varnish cache process."

#define CLI_PING							\
	"ping",								\
	"ping [timestamp]",						\
	"\tKeep connection alive"

#define CLI_STATS							\
	"stats",							\
	"stats",							\
	"\tShow summary statistics"

#define CLI_ZERO							\
	"zero",								\
	"zero",								\
	"\tZero summary statistics"

#define CLI_HELP							\
	"help",								\
	"help [command]",						\
	"\tShow command/protocol help"

#define CLI_VERBOSE							\
	"verbose",							\
	"verbose",							\
	"\tEnable/Disable verbosity"

#define CLI_EXIT							\
	"exit",								\
	"exit",								\
	"\tClose connection"

#define CLI_QUIT							\
	"quit",								\
	"quit",								\
	"\tClose connection"

#define CLI_BYE								\
	"bye",								\
	"bye",								\
	"\tClose connection"

enum cli_status_e {
	CLIS_SYNTAX	= 100,
	CLIS_UNKNOWN	= 101,
	CLIS_UNIMPL	= 102,
	CLIS_TOOFEW	= 104,
	CLIS_TOOMANY	= 105,
	CLIS_PARAM	= 106,
	CLIS_OK		= 200
};
