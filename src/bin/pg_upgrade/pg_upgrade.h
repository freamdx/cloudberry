#ifndef PG_UPGRADE_H
#define PG_UPGRADE_H
/*
 *	pg_upgrade.h
 *
 *	Portions Copyright (c) 2016-Present, VMware, Inc. or its affiliates
 *	Copyright (c) 2010-2021, PostgreSQL Global Development Group
 *	src/bin/pg_upgrade/pg_upgrade.h
 */

#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "postgres.h"
#include "libpq-fe.h"
#include "pqexpbuffer.h"
#include "common/kmgr_utils.h"

/* For now, pg_upgrade does not use common/logging.c; use our own pg_fatal */
#undef pg_fatal

/* Use port in the private/dynamic port number range */
#define DEF_PGUPORT			50432

#define MAX_STRING			1024
#define QUERY_ALLOC			8192

#define MESSAGE_WIDTH		60

#define GET_MAJOR_VERSION(v)	((v) / 100)

/* contains both global db information and CREATE DATABASE commands */
#define GLOBALS_DUMP_FILE	"pg_upgrade_dump_globals.sql"
#define DB_DUMP_FILE_MASK	"pg_upgrade_dump_%u.custom"

/*
 * Base directories that include all the files generated internally, from the
 * root path of the new cluster.  The paths are dynamically built as of
 * BASE_OUTPUTDIR/$timestamp/{LOG_OUTPUTDIR,DUMP_OUTPUTDIR} to ensure their
 * uniqueness in each run.
 */
#define BASE_OUTPUTDIR		"pg_upgrade_output.d"
#define LOG_OUTPUTDIR		 "log"
#define DUMP_OUTPUTDIR		 "dump"

#define DB_DUMP_LOG_FILE_MASK	"pg_upgrade_dump_%u.log"
#define SERVER_LOG_FILE		"pg_upgrade_server.log"
#define UTILITY_LOG_FILE	"pg_upgrade_utility.log"
#define INTERNAL_LOG_FILE	"pg_upgrade_internal.log"

extern char *output_files[];

/*
 * WIN32 files do not accept writes from multiple processes
 *
 * On Win32, we can't send both pg_upgrade output and command output to the
 * same file because we get the error: "The process cannot access the file
 * because it is being used by another process." so send the pg_ctl
 * command-line output to a new file, rather than into the server log file.
 * Ideally we could use UTILITY_LOG_FILE for this, but some Windows platforms
 * keep the pg_ctl output file open by the running postmaster, even after
 * pg_ctl exits.
 *
 * We could use the Windows pgwin32_open() flags to allow shared file
 * writes but is unclear how all other tools would use those flags, so
 * we just avoid it and log a little differently on Windows;  we adjust
 * the error message appropriately.
 */
#ifndef WIN32
#define SERVER_START_LOG_FILE	SERVER_LOG_FILE
#define SERVER_STOP_LOG_FILE	SERVER_LOG_FILE
#else
#define SERVER_START_LOG_FILE	"pg_upgrade_server_start.log"
/*
 *	"pg_ctl start" keeps SERVER_START_LOG_FILE and SERVER_LOG_FILE open
 *	while the server is running, so we use UTILITY_LOG_FILE for "pg_ctl
 *	stop".
 */
#define SERVER_STOP_LOG_FILE	UTILITY_LOG_FILE
#endif


#ifndef WIN32
#define pg_mv_file			rename
#define PATH_SEPARATOR		'/'
#define PATH_QUOTE	'\''
#define RM_CMD				"rm -f"
#define RMDIR_CMD			"rm -rf"
#define SCRIPT_PREFIX		"./"
#define SCRIPT_EXT			"sh"
#define ECHO_QUOTE	"'"
#define ECHO_BLANK	""
#else
#define pg_mv_file			pgrename
#define PATH_SEPARATOR		'\\'
#define PATH_QUOTE	'"'
#define RM_CMD				"DEL /q"
#define RMDIR_CMD			"RMDIR /s/q"
#define SCRIPT_PREFIX		""
#define SCRIPT_EXT			"bat"
#define EXE_EXT				".exe"
#define ECHO_QUOTE	""
#define ECHO_BLANK	"."
#endif


#define atooid(x)  ((Oid) strtoul((x), NULL, 10))

/*
 * The format of visibility map is changed with this 9.6 commit,
 */
#define VISIBILITY_MAP_FROZEN_BIT_CAT_VER 201603011

/*
 * pg_multixact format changed in 9.3 commit 0ac5ad5134f2769ccbaefec73844f85,
 * ("Improve concurrency of foreign key locking") which also updated catalog
 * version to this value.  pg_upgrade behavior depends on whether old and new
 * server versions are both newer than this, or only the new one is.
 *
 * In GPDB: that upstream change was merged into GPDB in the big 9.3 merge
 * commit.
 */
#define MULTIXACT_FORMATCHANGE_CAT_VER 301809211

/*
 * Extra information stored for each Append-only table.
 * This is used to transfer the information from the auxiliary
 * AO table to the new cluster.
 */

/* To hold contents of pg_visimap_<oid> */
typedef struct
{
	int			segno;
	int64		first_row_no;
	char	   *visimap;		/* text representation of the "bit varying" field */
} AOVisiMapInfo;

typedef struct
{
	int			segno;
	int			columngroup_no;
	int64		first_row_no;
	char	   *minipage;		/* text representation of the "bit varying" field */
} AOBlkDir;

/* To hold contents of pg_aoseg_<oid> */
typedef struct
{
	int			segno;
	int64		eof;
	int64		tupcount;
	int64		varblockcount;
	int64		eofuncompressed;
	int64		modcount;
	int16		version;
	int16		state;
} AOSegInfo;

/* To hold contents of pf_aocsseg_<oid> */
typedef struct
{
	int         segno;
	int64		tupcount;
	int64		varblockcount;
	char       *vpinfo;
	int64		modcount;
	int16		state;
	int16		version;
} AOCSSegInfo;

typedef struct
{
	int16		attlen;
	char		attalign;
	bool		is_numeric;
} AttInfo;

typedef enum
{
	HEAP,
	AO,
	AOCS,
	FSM
} RelType;


/*
 * large object chunk size added to pg_controldata,
 * commit 5f93c37805e7485488480916b4585e098d3cc883
 */
#define LARGE_OBJECT_SIZE_PG_CONTROL_VER 942

/*
 * change in JSONB format during 9.4 beta
 */
#define JSONB_FORMAT_CHANGE_CAT_VER 201409291


/*
 * Each relation is represented by a relinfo structure.
 */
typedef struct
{
	/* Can't use NAMEDATALEN; not guaranteed to be same on client */
	char	   *nspname;		/* namespace name */
	char	   *relname;		/* relation name */
	Oid			reloid;			/* relation OID */
	char		relstorage;
	Oid 		relfilenode;	/* relation file node */
	Oid			indtable;		/* if index, OID of its table, else 0 */
	Oid			toastheap;		/* if toast table, OID of base table, else 0 */
	char	   *tablespace;		/* tablespace path; "" for cluster default */
	bool		nsp_alloc;		/* should nspname be freed? */
	bool		tblsp_alloc;	/* should tablespace be freed? */

	RelType		reltype;

	/* Extra information for append-only tables */
	AOSegInfo  *aosegments;
	AOCSSegInfo *aocssegments;
	int			naosegments;
	AOVisiMapInfo *aovisimaps;
	int			naovisimaps;
	AOBlkDir   *aoblkdirs;
	int			naoblkdirs;

	/* Extra information for heap tables */
	bool		gpdb4_heap_conversion_needed;
	bool		has_numerics;
	AttInfo	   *atts;
	int			natts;
} RelInfo;

typedef struct
{
	RelInfo    *rels;
	int			nrels;
} RelInfoArr;

/*
 * The following structure represents a relation mapping.
 */
typedef struct
{
	const char *old_tablespace;
	const char *new_tablespace;
	const char *old_tablespace_suffix;
	const char *new_tablespace_suffix;
	Oid			old_db_oid;
	Oid			new_db_oid;

	/*
	 * old/new relfilenodes might differ for pg_largeobject(_metadata) indexes
	 * due to VACUUM FULL or REINDEX.  Other relfilenodes are preserved.
	 */
	Oid 		old_relfilenode;
	Oid 		new_relfilenode;
	/* the rest are used only for logging and error reporting */
	char	   *nspname;		/* namespaces */
	char	   *relname;

	bool		missing_seg0_ok;

	RelType		type;			/* Type of relation */

	/* Extra information for heap tables */
	bool		gpdb4_heap_conversion_needed;
	bool		has_numerics;
	AttInfo	   *atts;
	int			natts;
} FileNameMap;

/*
 * Structure to store database information
 */
typedef struct
{
	Oid			db_oid;			/* oid of the database */
	char	   *db_name;		/* database name */
	char		db_tablespace[MAXPGPATH];	/* database default tablespace
											 * path */
	char	   *db_collate;
	char	   *db_ctype;
	int			db_encoding;
	RelInfoArr	rel_arr;		/* array of all user relinfos */
} DbInfo;

typedef struct
{
	DbInfo	   *dbs;			/* array of db infos */
	int			ndbs;			/* number of db infos */
} DbInfoArr;

/*
 * The following structure is used to hold pg_control information.
 * Rather than using the backend's control structure we use our own
 * structure to avoid pg_control version issues between releases.
 */
typedef struct
{
	uint32		ctrl_ver;
	uint32		cat_ver;
	char		nextxlogfile[25];
	uint32		chkpnt_nxtxid;
	uint32		chkpnt_nxtepoch;
	uint64		chkpnt_nxtgxid;
	uint32		chkpnt_nxtoid;
	uint32		chkpnt_nxtmulti;
	uint32		chkpnt_nxtmxoff;
	uint32		chkpnt_oldstMulti;
	uint32		chkpnt_oldstxid;
	uint32		align;
	uint32		blocksz;
	uint32		largesz;
	uint32		walsz;
	uint32		walseg;
	uint32		ident;
	uint32		index;
	uint32		toast;
	uint32		large_object;
	bool		date_is_int;
	bool		float8_pass_by_value;
	uint32		data_checksum_version;
	int			file_encryption_method;
} ControlData;

/*
 * Enumeration to denote transfer modes
 */
typedef enum
{
	TRANSFER_MODE_CLONE,
	TRANSFER_MODE_COPY,
	TRANSFER_MODE_LINK
} transferMode;

/*
 * Enumeration to denote pg_log modes
 */
typedef enum
{
	PG_VERBOSE,
	PG_STATUS,
	PG_REPORT,
	PG_WARNING,
	PG_FATAL
} eLogType;

typedef long pgpid_t;


/*
 * cluster
 *
 *	information about each cluster
 */
typedef struct
{
	ControlData controldata;	/* pg_control information */
	DbInfoArr	dbarr;			/* dbinfos array */
	char	   *pgdata;			/* pathname for cluster's $PGDATA directory */
	char	   *pgconfig;		/* pathname for cluster's config file
								 * directory */
	char	   *bindir;			/* pathname for cluster's executable directory */
	char	   *pgopts;			/* options to pass to the server, like pg_ctl
								 * -o */
	char	   *sockdir;		/* directory for Unix Domain socket, if any */
	unsigned short port;		/* port number where postmaster is waiting */
	uint32		major_version;	/* PG_VERSION of cluster */
	char		major_version_str[64];	/* string PG_VERSION of cluster */
	uint32		bin_version;	/* version returned from pg_ctl */
	const char *tablespace_suffix;	/* directory specification */
} ClusterInfo;


/*
 *	LogOpts
*/
typedef struct
{
	FILE	   *internal;		/* internal log FILE */
	bool		verbose;		/* true -> be verbose in messages */
	bool		retain;			/* retain log files on success */
	/* Set of internal directories for output files */
	char	   *rootdir;		/* Root directory, aka pg_upgrade_output.d */
	char	   *basedir;		/* Base output directory, with timestamp */
	char	   *dumpdir;		/* Dumps */
	char	   *logdir;			/* Log files */
	bool		isatty;			/* is stdout a tty */
} LogOpts;


/*
 *	UserOpts
*/
typedef struct
{
	bool		check;			/* true -> ask user for permission to make
								 * changes */
	transferMode transfer_mode; /* copy files or link them? */
	int			jobs;			/* number of processes/threads to use */
	char	   *socketdir;		/* directory to use for Unix sockets */
	bool		ind_coll_unknown;	/* mark unknown index collation versions */
	bool		pass_terminal_fd; /* pass -R to pg_ctl? */
} UserOpts;

typedef struct
{
	char	   *name;
	int			dbnum;
} LibraryInfo;

/*
 * OSInfo
 */
typedef struct
{
	const char *progname;		/* complete pathname for this program */
	char	   *user;			/* username for clusters */
	bool		user_specified; /* user specified on command-line */
	char	  **old_tablespaces;	/* tablespaces */
	int			num_old_tablespaces;
	LibraryInfo *libraries;		/* loadable libraries */
	int			num_libraries;
	ClusterInfo *running_cluster;
} OSInfo;


/*
 * Global variables
 */
extern LogOpts log_opts;
extern UserOpts user_opts;
extern ClusterInfo old_cluster,
			new_cluster;
extern OSInfo os_info;

/* check.c */

void		output_check_banner(bool live_check);
void		check_and_dump_old_cluster(bool live_check, char **sequence_script_file_name);
void		check_new_cluster(void);
void		report_clusters_compatible(void);

void		issue_warnings_and_set_wal_level(char *sequence_script_file_name);
void		output_completion_banner(char *deletion_script_file_name);
void		check_cluster_versions(void);
void		check_cluster_compatibility(bool live_check);
void		create_script_for_old_cluster_deletion(char **deletion_script_file_name);
void		create_script_for_cluster_analyze(char **analyze_script_file_name);


/* controldata.c */

void		get_control_data(ClusterInfo *cluster, bool live_check);
void		check_control_data(ControlData *oldctrl, ControlData *newctrl);
void		disable_old_cluster(void);


/* dump.c */

void		generate_old_dump(void);


/* exec.c */

#define EXEC_PSQL_ARGS "--echo-queries --set ON_ERROR_STOP=on --no-psqlrc --dbname=template1"

bool		exec_prog(const char *log_file, const char *opt_log_file,
					  bool report_error, bool exit_on_error, const char *fmt,...) pg_attribute_printf(5, 6);
void		verify_directories(void);
bool		pid_lock_file_exists(const char *datadir);


/* file.c */

void		cloneFile(const char *src, const char *dst,
					  const char *schemaName, const char *relName);
void		copyFile(const char *src, const char *dst,
					 const char *schemaName, const char *relName);
void		linkFile(const char *src, const char *dst,
					 const char *schemaName, const char *relName);
void		rewriteVisibilityMap(const char *fromfile, const char *tofile,
								 const char *schemaName, const char *relName);
void		check_file_clone(void);
void		check_hard_link(void);

/* fopen_priv() is no longer different from fopen() */
#define fopen_priv(path, mode)	fopen(path, mode)

/* function.c */

void		get_loadable_libraries(void);
void		check_loadable_libraries(void);

/* info.c */

FileNameMap *gen_db_file_maps(DbInfo *old_db,
							  DbInfo *new_db, int *nmaps, const char *old_pgdata,
							  const char *new_pgdata);
void		get_db_and_rel_infos(ClusterInfo *cluster);
void		print_maps(FileNameMap *maps, int n,
					   const char *db_name);

/* option.c */

void		parseCommandLine(int argc, char *argv[]);
void		adjust_data_dir(ClusterInfo *cluster);
void		get_sock_dir(ClusterInfo *cluster, bool live_check);

/* relfilenode.c */

void		transfer_all_new_tablespaces(DbInfoArr *old_db_arr,
										 DbInfoArr *new_db_arr, char *old_pgdata, char *new_pgdata);
void		transfer_all_new_dbs(DbInfoArr *old_db_arr,
								 DbInfoArr *new_db_arr, char *old_pgdata, char *new_pgdata,
								 char *old_tablespace);

/* tablespace.c */

void		init_tablespaces(void);


/* server.c */

PGconn	   *connectToServer(ClusterInfo *cluster, const char *db_name);
PGresult   *executeQueryOrDie(PGconn *conn, const char *fmt,...) pg_attribute_printf(2, 3);

char	   *cluster_conn_opts(ClusterInfo *cluster);

bool		start_postmaster(ClusterInfo *cluster, bool report_and_exit_on_error);
void		stop_postmaster(bool in_atexit);
uint32		get_major_server_version(ClusterInfo *cluster);
void		check_pghost_envvar(void);


/* util.c */

char	   *quote_identifier(const char *s);
extern void appendShellString(PQExpBuffer buf, const char *str);
extern void appendConnStrVal(PQExpBuffer buf, const char *str);
extern void appendPsqlMetaConnect(PQExpBuffer buf, const char *dbname);
int			get_user_info(char **user_name_p);
void		check_ok(void);
void		report_status(eLogType type, const char *fmt,...) pg_attribute_printf(2, 3);
void		pg_log(eLogType type, const char *fmt,...) pg_attribute_printf(2, 3);
void		pg_fatal(const char *fmt,...) pg_attribute_printf(1, 2) pg_attribute_noreturn();
void		end_progress_output(void);
void		cleanup_output_dirs(void);
void		prep_status(const char *fmt,...) pg_attribute_printf(1, 2);
void		prep_status_progress(const char *fmt,...) pg_attribute_printf(1, 2);
unsigned int str2uint(const char *str);
uint64		str2uint64(const char *str);
void		pg_putenv(const char *var, const char *val);
void 		gp_fatal_log(const char *fmt,...) pg_attribute_printf(1, 2);


/* version.c */

bool		check_for_data_types_usage(ClusterInfo *cluster,
									   const char *base_query,
									   const char *output_path);
bool		check_for_data_type_usage(ClusterInfo *cluster,
									  const char *type_name,
									  const char *output_path);
void		old_9_3_check_for_line_data_type_usage(ClusterInfo *cluster);
void		old_9_6_check_for_unknown_data_type_usage(ClusterInfo *cluster);
void		old_9_6_invalidate_hash_indexes(ClusterInfo *cluster,
											bool check_mode);

/* version_old_8_3.c */

void		old_8_3_check_for_name_data_type_usage(ClusterInfo *cluster);
void		old_8_3_check_for_tsquery_usage(ClusterInfo *cluster);
void		old_8_3_check_ltree_usage(ClusterInfo *cluster);
void		old_8_3_rebuild_tsvector_tables(ClusterInfo *cluster, bool check_mode);
void		old_8_3_invalidate_hash_gin_indexes(ClusterInfo *cluster, bool check_mode);
void old_8_3_invalidate_bpchar_pattern_ops_indexes(ClusterInfo *cluster,
											  bool check_mode);
char	   *old_8_3_create_sequence_script(ClusterInfo *cluster);
void		old_11_check_for_sql_identifier_data_type_usage(ClusterInfo *cluster);
void		report_extension_updates(ClusterInfo *cluster);

/* parallel.c */
void		parallel_exec_prog(const char *log_file, const char *opt_log_file,
							   const char *fmt,...) pg_attribute_printf(3, 4);
void		parallel_transfer_all_new_dbs(DbInfoArr *old_db_arr, DbInfoArr *new_db_arr,
										  char *old_pgdata, char *new_pgdata,
										  char *old_tablespace);
bool		reap_child(bool wait_for_child);

/*
 * Hack to make backend macros that check for assertions to work.
 */
#ifdef AssertMacro
#undef AssertMacro
#endif
#define AssertMacro(condition) ((void) true)
#ifdef Assert
#undef Assert
#endif
#define Assert(condition) ((void) (true || (condition)))

#endif /* PG_UPGRADE_H */
