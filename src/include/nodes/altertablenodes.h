/*-------------------------------------------------------------------------
 *
 * altertablenodes.h
 *	  definitions for ALTER TABLE subcommands
 *
 * In PostgreSQL, these are in tablecmds.c, but in GPDB, they need to be
 * exposed to out/readfunc.c, because we need support for serializing
 * these. Also, the structs are Nodes in GPDB for the same reason.
 *
 * NOTE: If new fields are added to these structs, remember to update
 * outfuncs.c/readfuncs.c
 *
 * Portions Copyright (c) 2006-2009, Greenplum inc
 * Portions Copyright (c) 2012-Present VMware, Inc. or its affiliates.
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/nodes/altertablenodes.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef ALTERTABLENODES_H
#define ALTERTABLENODES_H

/*
 * GPDB: Moved here from tablecmds.c
 *
 * State information for ALTER TABLE
 *
 * The pending-work queue for an ALTER TABLE is a List of AlteredTableInfo
 * structs, one for each table modified by the operation (the named table
 * plus any child tables that are affected).  We save lists of subcommands
 * to apply to this table (possibly modified by parse transformation steps);
 * these lists will be executed in Phase 2.  If a Phase 3 step is needed,
 * necessary information is stored in the constraints and newvals lists.
 *
 * Phase 2 is divided into multiple passes; subcommands are executed in
 * a pass determined by subcommand type.
 */

#define AT_PASS_UNSET			-1	/* UNSET will cause ERROR */
#define AT_PASS_DROP			0	/* DROP (all flavors) */
#define AT_PASS_ALTER_TYPE		1	/* ALTER COLUMN TYPE */
#define AT_PASS_OLD_INDEX		2	/* re-add existing indexes */
#define AT_PASS_OLD_CONSTR		3	/* re-add existing constraints */
/* We could support a RENAME COLUMN pass here, but not currently used */
#define AT_PASS_ADD_COL			4	/* ADD COLUMN */
#define AT_PASS_ADD_CONSTR		5	/* ADD constraints (initial examination) */
#define AT_PASS_COL_ATTRS		6	/* set other column attributes */
#define AT_PASS_ADD_INDEXCONSTR	7	/* ADD index-based constraints */
#define AT_PASS_ADD_INDEX		8	/* ADD indexes */
#define AT_PASS_ADD_OTHERCONSTR	9	/* ADD other constraints, defaults */
#define AT_PASS_MISC			10	/* other stuff */
#define AT_NUM_PASSES			11

typedef struct AlteredTableInfo
{
	NodeTag		type;

	/* Information saved before any work commences: */
	Oid			relid;			/* Relation to work on */
	char		relkind;		/* Its relkind */
	TupleDesc	oldDesc;		/* Pre-modification tuple descriptor */

	/*
	 * Transiently set during Phase 2, normally set to NULL.
	 *
	 * ATRewriteCatalogs sets this when it starts, and closes when ATExecCmd
	 * returns control.  This can be exploited by ATExecCmd subroutines to
	 * close/reopen across transaction boundaries.
	 */
	Relation	rel;

	/* Information saved by Phase 1 for Phase 2: */
	List	   *subcmds[AT_NUM_PASSES]; /* Lists of AlterTableCmd */
	/* Information saved by Phases 1/2 for Phase 3: */
	List	   *constraints;	/* List of NewConstraint */
	List	   *newvals;		/* List of NewColumnValue */
	List	   *afterStmts;		/* List of utility command parsetrees */
	bool		verify_new_notnull; /* T if we should recheck NOT NULL */
	int			rewrite;		/* Reason for forced rewrite, if any */
	Oid 		newAccessMethod; /* new access method; 0 means no change */
	bool		dist_opfamily_changed; /* T if changing datatype of distribution key column and new opclass is in different opfamily than old one */
	Oid			new_opclass;		/* new opclass, if changing a distribution key column */
	Oid			newTableSpace;	/* new tablespace; 0 means no change */
	bool		chgPersistence; /* T if SET LOGGED/UNLOGGED is used */
	char		newrelpersistence;	/* if above is true */
	Expr	   *partition_constraint;	/* for attach partition validation */
	/* true, if validating default due to some other attach/detach */
	bool		validate_default;
	/* Objects to rebuild after completing ALTER TYPE operations */
	List	   *changedConstraintOids;	/* OIDs of constraints to rebuild */
	List	   *changedConstraintDefs;	/* string definitions of same */
	List	   *changedIndexOids;	/* OIDs of indexes to rebuild */
	List	   *changedIndexDefs;	/* string definitions of same */
	char	   *replicaIdentityIndex;	/* index to reset as REPLICA IDENTITY */
	char	   *clusterOnIndex;	/* index to use for CLUSTER */
	List	   *changedStatisticsOids;	/* OIDs of statistics to rebuild */
	List	   *changedStatisticsDefs;	/* string definitions of same */

	/*
	 * Saved before/constraint list(list of list) in transformAlterTableStmt.
	 * `transformAlterTableStmt()` will not execute on QEs, so QD must
	 * dispatch these results to the QEs. The 2 fields are lists of list,
	 * whenever `transformAlterTableStmt()` is called, the QD saves the lists
	 * to the current `AlteredTableInfo` and the QE consumes the list item
	 * from `AlteredTableInfo` dispatched from QD.
	 *
	 * beforeStmtLists: transformAlterTableStmt() generates some utility
	 * 			commands to run before the current action. See the usage of
	 * 			ProcessUtilityForAlterTable.  These utility commands should be
	 * 			generated in QD, because it needs to choose some names, like
	 * 			relation/index/sequence name. The QEs are not allowed to
	 * 			choose a name that is different to that in QD side.  The
	 * 			BEFORE commands don't run all at one point like `afterStmts`,
	 * 			so we must exactly pick up the list of commands that was
	 * 			generated by QD at the same running sequence.
	 * constraintLists: transformAlterTableStmt() normally sets `atstmt.cmds`
	 * 			to the same cmd-list that passes in, but it will generate some
	 * 			new cmd-list driven by the constraint.
	 */
	List	   *beforeStmtLists;
	List	   *constraintLists;
} AlteredTableInfo;

typedef struct ExprState ExprState;

/* Struct describing one new constraint to check in Phase 3 scan */
/* Note: new NOT NULL constraints are handled elsewhere */
typedef struct NewConstraint
{
	NodeTag		type;

	char	   *name;			/* Constraint name, or NULL if none */
	ConstrType	contype;		/* CHECK or FOREIGN */
	Oid			refrelid;		/* PK rel, if FOREIGN */
	Oid			refindid;		/* OID of PK's index, if FOREIGN */
	Oid			conid;			/* OID of pg_constraint entry, if FOREIGN */
	Node	   *qual;			/* Check expr or CONSTR_FOREIGN Constraint */
	ExprState  *qualstate;		/* Execution state for CHECK expr */
} NewConstraint;

/*
 * Struct describing one new column value that needs to be computed during
 * Phase 3 copy (this could be either a new column with a non-null default, or
 * a column that we're changing the type of).  Columns without such an entry
 * are just copied from the old table during ATRewriteTable.  Note that the
 * expr is an expression over *old* table values, except when is_generated
 * is true; then it is an expression over columns of the *new* tuple.
 */
typedef struct NewColumnValue
{
	NodeTag		type;

	AttrNumber	attnum;			/* which column */
	Expr	   *expr;			/* expression to compute */
	ExprState  *exprstate;		/* execution state */
	bool		is_generated;	/* is it a GENERATED expression? */
} NewColumnValue;

#endif							/* ALTERTABLENODES_H */
