/*
 * =====================================================================
 *        Version:  1.0
 *        Created:  01.09.2012 12:58:02
 *         Author:  Miroslav Bendík
 *        Company:  LinuxOS.sk
 * =====================================================================
 */

#include "build_config.h"

#if MSDOS || __OS2__ || __NT__ || _WIN32
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

#define BLOCK_SIZE		16
#define MAPBLOCK_MIN		(-2048)
#define MAPBLOCK_MAX		2047
#define CHUNK_SIZE_DEFAULT	5

// Max number of node name -> color mappings stored in a mapblock
#define MAPBLOCK_MAXCOLORS	65536

// List of possible database names (for usage message)
#if USE_SQLITE3
#define USAGE_NAME_SQLITE "/sqlite3"
#else
#define USAGE_NAME_SQLITE
#endif

#if USE_POSTGRESQL
#define USAGE_NAME_POSTGRESQL "/postgresql"
#else
#define USAGE_NAME_POSTGRESQL
#endif

#if USE_LEVELDB
#define USAGE_NAME_LEVELDB "/leveldb"
#else
#define USAGE_NAME_LEVELDB
#endif

#if USE_REDIS
#define USAGE_NAME_REDIS "/redis"
#else
#define USAGE_NAME_REDIS
#endif

#define USAGE_DATABASES "auto" USAGE_NAME_SQLITE USAGE_NAME_POSTGRESQL USAGE_NAME_LEVELDB USAGE_NAME_REDIS

#if !USE_SQLITE3 && !USE_POSTGRESQL && !USE_LEVELDB && !USE_REDIS
#error No database backends configured !
#endif

// default database to use
#if USE_SQLITE3 && !USE_POSTGRESQL && !USE_LEVELDB && !USE_REDIS
#define DEFAULT_BACKEND "sqlite3"
#elif !USE_SQLITE3 && USE_POSTGRESQL && !USE_LEVELDB && !USE_REDIS
#define DEFAULT_BACKEND "postgresql"
#elif !USE_SQLITE3 && !USE_POSTGRESQL && USE_LEVELDB && !USE_REDIS
#define DEFAULT_BACKEND "leveldb"
#elif !USE_SQLITE3 && !USE_POSTGRESQL && !USE_LEVELDB && USE_REDIS
#define DEFAULT_BACKEND "redis"
#else
#define DEFAULT_BACKEND "auto"
#endif

