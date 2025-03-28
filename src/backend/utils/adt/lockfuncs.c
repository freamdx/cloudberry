/*-------------------------------------------------------------------------
 *
 * lockfuncs.c
 *		Functions for SQL access to various lock-manager capabilities.
 *
 * Copyright (c) 2002-2021, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		src/backend/utils/adt/lockfuncs.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/predicate_internals.h"
#include "utils/array.h"
#include "utils/builtins.h"

#include "libpq-fe.h"
#include "cdb/cdbdisp_query.h"
#include "cdb/cdbdispatchresult.h"
#include "cdb/cdbvars.h"

/*
 * This must match enum LockTagType!  Also, be sure to document any changes
 * in the docs for the pg_locks view and for wait event types.
 */
const char *const LockTagTypeNames[] = {
	"relation",
	"extend",
	"frozenid",
	"page",
	"tuple",
	"transactionid",
	"virtualxid",
	"spectoken",
	"object",
	"resource queue",
	"distributed xid",
	"userlock",
	"advisory",
	"warehouse"
};

StaticAssertDecl(lengthof(LockTagTypeNames) == (LOCKTAG_WAREHOUSE + 1),
				 "array length mismatch");

/* This must match enum PredicateLockTargetType (predicate_internals.h) */
static const char *const PredicateLockTagTypeNames[] = {
	"relation",
	"page",
	"tuple"
};

StaticAssertDecl(lengthof(PredicateLockTagTypeNames) == (PREDLOCKTAG_TUPLE + 1),
				 "array length mismatch");

/* Working status for pg_lock_status */
typedef struct
{
	LockData   *lockData;		/* state data from lmgr */
	int			currIdx;		/* current PROCLOCK index */
	PredicateLockData *predLockData;	/* state data for pred locks */
	int			predLockIdx;	/* current index for pred lock */

	int			numSegLocks;	/* Total number of locks being reported back to client */
	int			numsegresults;	/* If we dispatch to segDBs, the number of segresults */
	int			nextResultset;	/* which result set is being processed */
	int			nextRow;		/* which row in current result set will be processed next */
	struct pg_result **segresults;	/* pg_result for each segDB */
} PG_Lock_Status;

/* Number of columns in pg_locks output */
#define NUM_LOCK_STATUS_COLUMNS		19

/*
 * VXIDGetDatum - Construct a text representation of a VXID
 *
 * This is currently only used in pg_lock_status, so we put it here.
 */
static Datum
VXIDGetDatum(BackendId bid, LocalTransactionId lxid)
{
	/*
	 * The representation is "<bid>/<lxid>", decimal and unsigned decimal
	 * respectively.  Note that elog.c also knows how to format a vxid.
	 */
	char		vxidstr[32];

	snprintf(vxidstr, sizeof(vxidstr), "%d/%u", bid, lxid);

	return CStringGetTextDatum(vxidstr);
}


/*
 * pg_lock_status - produce a view with one row per held or awaited lock mode
 */
Datum
pg_lock_status(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	PG_Lock_Status *mystatus;
	LockData   *lockData;
	PredicateLockData *predLockData;

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	tupdesc;
		MemoryContext oldcontext;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* build tupdesc for result tuples */
		/* this had better match function's declaration in pg_proc.h */
		tupdesc = CreateTemplateTupleDesc(NUM_LOCK_STATUS_COLUMNS);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "locktype",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "database",
						   OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "relation",
						   OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "page",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "tuple",
						   INT2OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "virtualxid",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7, "transactionid",
						   XIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 8, "classid",
						   OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 9, "objid",
						   OIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 10, "objsubid",
						   INT2OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 11, "virtualtransaction",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 12, "pid",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 13, "mode",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 14, "granted",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 15, "fastpath",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 16, "waitstart",
						   TIMESTAMPTZOID, -1, 0);

		/*
		 * These next columns are specific to GPDB
		 */
		TupleDescInitEntry(tupdesc, (AttrNumber) 17, "mppSessionId",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 18, "mppIsWriter",
						   BOOLOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 19, "gp_segment_id",
						   INT4OID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		/*
		 * Collect all the locking information that we will format and send
		 * out as a result set.
		 */
		mystatus = (PG_Lock_Status *) palloc(sizeof(PG_Lock_Status));
		funcctx->user_fctx = (void *) mystatus;

		mystatus->lockData = GetLockStatusData();
		mystatus->currIdx = 0;
		mystatus->predLockData = GetPredicateLockStatusData();
		mystatus->predLockIdx = 0;

		mystatus->numSegLocks = 0;
		mystatus->numsegresults = 0;
		mystatus->nextResultset = 0;
		mystatus->nextRow = 0;
		mystatus->segresults = NULL;

		/*
		 * Seeing the locks just from the masterDB isn't enough to know what is locked,
		 * or if there is a deadlock.  That's because the segDBs also take locks.
		 * Some locks show up only on the master, some only on the segDBs, and some on both.
		 *
		 * So, let's collect the lock information from all the segDBs.  Sure, this means
		 * there are a lot more rows coming back from pg_locks than before, since most locks
		 * on the segDBs happen across all the segDBs at the same time.  But not always,
		 * so let's play it safe and get them all.
		 */

		if (Gp_role == GP_ROLE_DISPATCH)
		{
			CdbPgResults cdb_pgresults = {NULL, 0};
			StringInfoData buffer;
			int i;
			initStringInfo(&buffer);

			/*
			 * Why dispatch something here, rather than do a UNION ALL in pg_locks view, and
			 * a join to gp_dist_random('gp_id')?  There are several important reasons.
			 *
			 * The union all method is much slower, and requires taking locks on gp_id.
			 * More importantly, applications such as pgAdmin do queries of this view that
			 * involve a correlated subqueries joining to other catalog tables,
			 * which works if we do it this way, but fails
			 * if the view includes the union all.  That completely breaks the server status
			 * display in pgAdmin.
			 *
			 * Why dispatch this way, rather than via SPI?  There are several advantages.
			 * First, it's easy to get "writer gang is busy" errors if we use SPI.
			 *
			 * Second, this should be much faster, as it doesn't require setting up
			 * the interconnect, and doesn't need to touch any actual data tables to be
			 * able to get the gp_segment_id.
			 *
			 * The downside is we get n result sets, where n == number of segDBs.
			 *
			 * It would be better yet if we sent a plan tree rather than a text string,
			 * so the segDBs don't need to parse it.  That would also avoid taking any relation locks
			 * on the segDB to get this info (normally need to get an accessShareLock on pg_locks on the segDB
			 * to make sure it doesn't go away during parsing).  But the only safe way I know to do this
			 * is to hand-build the plan tree, and I'm to lazy to do it right now. It's just a matter of
			 * building a function scan node, and filling it in with our result set info (from the tupledesc).
			 *
			 * One thing to note:  it's OK to join pg_locks with any catalog table or master-only table,
			 * but joining to a distributed table will result in "writer gang busy: possible attempt to
			 * execute volatile function in unsupported context" errors, because
			 * the scan of the distributed table might already be running on the writer gang
			 * when we want to dispatch this.
			 *
			 * This could be fixed by allocating a reader gang and dispatching to that, but the cost
			 * of setting up a new gang is high, and I've never seen anyone need to join this to a
			 * distributed table.
			 *
			 * GPDB_84_MERGE_FIXME: Should we rewrite this in a different way now that we have
			 * ON SEGMENT/ ON MASTER attributes on functions?
			 */
			CdbDispatchCommand("SELECT * FROM pg_catalog.pg_lock_status()", DF_WITH_SNAPSHOT, &cdb_pgresults);

			if (cdb_pgresults.numResults == 0)
				elog(ERROR, "pg_locks didn't get back any data from the segDBs");

			for (i = 0; i < cdb_pgresults.numResults; i++)
			{
				/*
				 * Any error here should have propagated into errbuf, so we shouldn't
				 * ever see anything other that tuples_ok here.  But, check to be
				 * sure.
				 */
				if (PQresultStatus(cdb_pgresults.pg_results[i]) != PGRES_TUPLES_OK)
				{
					cdbdisp_clearCdbPgResults(&cdb_pgresults);
					elog(ERROR,"pg_locks: resultStatus not tuples_Ok");
				}

				/*
				 * numSegLocks needs to be the total size we are returning to
				 * the application. At the start of this loop, it has the count
				 * for the masterDB locks.  Add each of the segDB lock counts.
				 */
				mystatus->numSegLocks += PQntuples(cdb_pgresults.pg_results[i]);

				/*
				 * This query better match the tupledesc we just made above.
				 */
				if (PQnfields(cdb_pgresults.pg_results[i]) != tupdesc->natts)
					elog(ERROR, "unexpected number of columns returned from pg_lock_status() on segment (%d, expected %d)",
						 PQnfields(cdb_pgresults.pg_results[i]), tupdesc->natts);
			}

			mystatus->numsegresults = cdb_pgresults.numResults;
			/*
			 * cdbdisp_dispatchRMCommand copies the result sets into our memory, which
			 * will still exist on the subsequent calls.
			 */
			mystatus->segresults = cdb_pgresults.pg_results;
		}

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	mystatus = (PG_Lock_Status *) funcctx->user_fctx;
	lockData = mystatus->lockData;

	/*
	 * This loop returns all the local lock data from the segment we are running on.
	 */

	while (mystatus->currIdx < lockData->nelements)
	{
		bool		granted;
		LOCKMODE	mode = 0;
		const char *locktypename;
		char		tnbuf[32];
		Datum		values[NUM_LOCK_STATUS_COLUMNS];
		bool		nulls[NUM_LOCK_STATUS_COLUMNS];
		HeapTuple	tuple;
		Datum		result;
		LockInstanceData *instance;

		instance = &(lockData->locks[mystatus->currIdx]);

		/*
		 * Look to see if there are any held lock modes in this PROCLOCK. If
		 * so, report, and destructively modify lockData so we don't report
		 * again.
		 */
		granted = false;
		if (instance->holdMask)
		{
			for (mode = 0; mode < MAX_LOCKMODES; mode++)
			{
				if (instance->holdMask & LOCKBIT_ON(mode))
				{
					granted = true;
					instance->holdMask &= LOCKBIT_OFF(mode);
					break;
				}
			}
		}

		/*
		 * If no (more) held modes to report, see if PROC is waiting for a
		 * lock on this lock.
		 */
		if (!granted)
		{
			if (instance->waitLockMode != NoLock)
			{
				/* Yes, so report it with proper mode */
				mode = instance->waitLockMode;

				/*
				 * We are now done with this PROCLOCK, so advance pointer to
				 * continue with next one on next call.
				 */
				mystatus->currIdx++;
			}
			else
			{
				/*
				 * Okay, we've displayed all the locks associated with this
				 * PROCLOCK, proceed to the next one.
				 */
				mystatus->currIdx++;
				continue;
			}
		}

		/*
		 * Form tuple with appropriate data.
		 */
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, false, sizeof(nulls));

		if (instance->locktag.locktag_type <= LOCKTAG_LAST_TYPE)
			locktypename = LockTagTypeNames[instance->locktag.locktag_type];
		else
		{
			snprintf(tnbuf, sizeof(tnbuf), "unknown %d",
					 (int) instance->locktag.locktag_type);
			locktypename = tnbuf;
		}
		values[0] = CStringGetTextDatum(locktypename);

		switch ((LockTagType) instance->locktag.locktag_type)
		{
			case LOCKTAG_RELATION:
			case LOCKTAG_RELATION_EXTEND:
				values[1] = ObjectIdGetDatum(instance->locktag.locktag_field1);
				values[2] = ObjectIdGetDatum(instance->locktag.locktag_field2);
				nulls[3] = true;
				nulls[4] = true;
				nulls[5] = true;
				nulls[6] = true;
				nulls[7] = true;
				nulls[8] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_DATABASE_FROZEN_IDS:
				values[1] = ObjectIdGetDatum(instance->locktag.locktag_field1);
				nulls[2] = true;
				nulls[3] = true;
				nulls[4] = true;
				nulls[5] = true;
				nulls[6] = true;
				nulls[7] = true;
				nulls[8] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_PAGE:
				values[1] = ObjectIdGetDatum(instance->locktag.locktag_field1);
				values[2] = ObjectIdGetDatum(instance->locktag.locktag_field2);
				values[3] = UInt32GetDatum(instance->locktag.locktag_field3);
				nulls[4] = true;
				nulls[5] = true;
				nulls[6] = true;
				nulls[7] = true;
				nulls[8] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_TUPLE:
				values[1] = ObjectIdGetDatum(instance->locktag.locktag_field1);
				values[2] = ObjectIdGetDatum(instance->locktag.locktag_field2);
				values[3] = UInt32GetDatum(instance->locktag.locktag_field3);
				values[4] = UInt16GetDatum(instance->locktag.locktag_field4);
				nulls[5] = true;
				nulls[6] = true;
				nulls[7] = true;
				nulls[8] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_TRANSACTION:
				values[6] =
					TransactionIdGetDatum(instance->locktag.locktag_field1);
				nulls[1] = true;
				nulls[2] = true;
				nulls[3] = true;
				nulls[4] = true;
				nulls[5] = true;
				nulls[7] = true;
				nulls[8] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_DISTRIB_TRANSACTION:
				values[6] =
					DistributedTransactionIdGetDatum(LOCKTAG_DISTRIB_TRANSACTION_ID(instance->locktag));
				nulls[1] = true;
				nulls[2] = true;
				nulls[3] = true;
				nulls[4] = true;
				nulls[5] = true;
				nulls[7] = true;
				nulls[8] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_VIRTUALTRANSACTION:
				values[5] = VXIDGetDatum(instance->locktag.locktag_field1,
										 instance->locktag.locktag_field2);
				nulls[1] = true;
				nulls[2] = true;
				nulls[3] = true;
				nulls[4] = true;
				nulls[6] = true;
				nulls[7] = true;
				nulls[8] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_RESOURCE_QUEUE:
			case LOCKTAG_WAREHOUSE:
#if 0
				values[1] = ObjectIdGetDatum(proc->databaseId);
#endif
				nulls[1] = true;
				values[8] = ObjectIdGetDatum(instance->locktag.locktag_field1);
				nulls[2] = true;
				nulls[3] = true;
				nulls[4] = true;
				nulls[5] = true;
				nulls[6] = true;
				nulls[7] = true;
				nulls[9] = true;
				break;
			case LOCKTAG_OBJECT:
			case LOCKTAG_USERLOCK:
			case LOCKTAG_ADVISORY:
			default:			/* treat unknown locktags like OBJECT */
				values[1] = ObjectIdGetDatum(instance->locktag.locktag_field1);
				values[7] = ObjectIdGetDatum(instance->locktag.locktag_field2);
				values[8] = ObjectIdGetDatum(instance->locktag.locktag_field3);
				values[9] = Int16GetDatum(instance->locktag.locktag_field4);
				nulls[2] = true;
				nulls[3] = true;
				nulls[4] = true;
				nulls[5] = true;
				nulls[6] = true;
				break;
		}

		values[10] = VXIDGetDatum(instance->backend, instance->lxid);
		if (instance->pid != 0)
			values[11] = Int32GetDatum(instance->pid);
		else
			nulls[11] = true;
		values[12] = CStringGetTextDatum(GetLockmodeName(instance->locktag.locktag_lockmethodid, mode));
		values[13] = BoolGetDatum(granted);
		values[14] = BoolGetDatum(instance->fastpath);
		if (!granted && instance->waitStart != 0)
			values[15] = TimestampTzGetDatum(instance->waitStart);
		else
			nulls[15] = true;
		
		values[16] = Int32GetDatum(instance->mppSessionId);

		values[17] = BoolGetDatum(instance->mppIsWriter);

		values[18] = Int32GetDatum(GpIdentity.segindex);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);
		SRF_RETURN_NEXT(funcctx, result);
	}

	/*
	 * This loop only executes on the masterDB and only in dispatch mode,
	 * because that is the only time we dispatched to the segDBs.
	 */
	while (mystatus->currIdx >= lockData->nelements && mystatus->currIdx < lockData->nelements + mystatus->numSegLocks)
	{
		HeapTuple	tuple;
		Datum		result;
		Datum		values[NUM_LOCK_STATUS_COLUMNS];
		bool		nulls[NUM_LOCK_STATUS_COLUMNS];
		int			i;
		int			whichresultset;
		int			whichrow;

		Assert(Gp_role == GP_ROLE_DISPATCH);

		/*
		 * Because we have one result set per segDB (rather than one big result
		 * set with everything), we use mystatus->nextResultset and
		 * mystatus->nextRow to track which result set we are on, and which row
		 * within that result set we are returning, respectively.
		 */
		whichresultset = mystatus->nextResultset;
		whichrow = mystatus->nextRow;
		Assert(whichresultset < mystatus->numsegresults);
		Assert(whichrow < PQntuples(mystatus->segresults[whichresultset]));

		/*
		 * Advance to next row. If we're out of rows in this result set,
		 * advance to next one.
		 */
		if (mystatus->nextRow + 1 < PQntuples(mystatus->segresults[mystatus->nextResultset]))
		{
			mystatus->nextRow++;
		}
		else
		{
			mystatus->nextResultset++;
			mystatus->nextRow = 0;
		}
		mystatus->currIdx++;

		/*
		 * Form tuple with appropriate data we got from the segDBs
		 */
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, false, sizeof(nulls));

		/*
		 * For each column, extract out the value (which comes out in text).
		 * Convert it to the appropriate datatype to match our tupledesc,
		 * and put that in values.
		 * The columns look like this (from select statement earlier):
		 *
		 * "   (locktype text, database oid, relation oid, page int4, tuple int2,"
		 *	"   transactionid xid, classid oid, objid oid, objsubid int2,"
		 *	"    transaction xid, pid int4, mode text, granted boolean, "
		 *	"    mppSessionId int4, mppIsWriter boolean, gp_segment_id int4) ,"
		 */

		values[0] = CStringGetTextDatum(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 0));
		values[1] = ObjectIdGetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 1)));
		values[2] = ObjectIdGetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 2)));
		values[3] = UInt32GetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 3)));
		values[4] = UInt16GetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 4)));

		values[5] = CStringGetTextDatum(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 5));
		values[6] = TransactionIdGetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 6)));
		values[7] = ObjectIdGetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 7)));
		values[8] = ObjectIdGetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 8)));
		values[9] = UInt16GetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 9)));

		values[10] = CStringGetTextDatum(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 10));
		values[11] = UInt32GetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 11)));
		values[12] = CStringGetTextDatum(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 12));
		values[13] = BoolGetDatum(strncmp(PQgetvalue(mystatus->segresults[whichresultset], whichrow,13),"t",1)==0);
		values[14] = BoolGetDatum(strncmp(PQgetvalue(mystatus->segresults[whichresultset], whichrow,14),"t",1)==0);
		values[15] = TimestampTzGetDatum(atoll(PQgetvalue(mystatus->segresults[whichresultset], whichrow, 15)));
		/*
		 * These next fields are specific to GPDB
		 */
		values[16] = Int32GetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow,16)));
		values[17] = BoolGetDatum(strncmp(PQgetvalue(mystatus->segresults[whichresultset], whichrow,17),"t",1)==0);
		values[18] = Int32GetDatum(atoi(PQgetvalue(mystatus->segresults[whichresultset], whichrow,18)));
		/*
		 * Copy the null info over.  It should all match properly.
		 */
		for (i = 0; i < NUM_LOCK_STATUS_COLUMNS; i++)
		{
			nulls[i] = PQgetisnull(mystatus->segresults[whichresultset], whichrow, i);
		}

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);
		SRF_RETURN_NEXT(funcctx, result);
	}

	/*
	 * Have returned all regular locks. Now start on the SIREAD predicate
	 * locks.
	 */
	predLockData = mystatus->predLockData;
	if (mystatus->predLockIdx < predLockData->nelements)
	{
		PredicateLockTargetType lockType;

		PREDICATELOCKTARGETTAG *predTag = &(predLockData->locktags[mystatus->predLockIdx]);
		SERIALIZABLEXACT *xact = &(predLockData->xacts[mystatus->predLockIdx]);
		Datum		values[NUM_LOCK_STATUS_COLUMNS];
		bool		nulls[NUM_LOCK_STATUS_COLUMNS];
		HeapTuple	tuple;
		Datum		result;

		mystatus->predLockIdx++;

		/*
		 * Form tuple with appropriate data.
		 */
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, false, sizeof(nulls));

		/* lock type */
		lockType = GET_PREDICATELOCKTARGETTAG_TYPE(*predTag);

		values[0] = CStringGetTextDatum(PredicateLockTagTypeNames[lockType]);

		/* lock target */
		values[1] = GET_PREDICATELOCKTARGETTAG_DB(*predTag);
		values[2] = GET_PREDICATELOCKTARGETTAG_RELATION(*predTag);
		if (lockType == PREDLOCKTAG_TUPLE)
			values[4] = GET_PREDICATELOCKTARGETTAG_OFFSET(*predTag);
		else
			nulls[4] = true;
		if ((lockType == PREDLOCKTAG_TUPLE) ||
			(lockType == PREDLOCKTAG_PAGE))
			values[3] = GET_PREDICATELOCKTARGETTAG_PAGE(*predTag);
		else
			nulls[3] = true;

		/* these fields are targets for other types of locks */
		nulls[5] = true;		/* virtualxid */
		nulls[6] = true;		/* transactionid */
		nulls[7] = true;		/* classid */
		nulls[8] = true;		/* objid */
		nulls[9] = true;		/* objsubid */

		/* lock holder */
		values[10] = VXIDGetDatum(xact->vxid.backendId,
								  xact->vxid.localTransactionId);
		if (xact->pid != 0)
			values[11] = Int32GetDatum(xact->pid);
		else
			nulls[11] = true;

		/*
		 * Lock mode. Currently all predicate locks are SIReadLocks, which are
		 * always held (never waiting) and have no fast path
		 */
		values[12] = CStringGetTextDatum("SIReadLock");
		values[13] = BoolGetDatum(true);
		values[14] = BoolGetDatum(false);
		nulls[15] = true;

		/*
		 * GPDB_91_MERGE_FIXME: what to set these GPDB-specific fields to?
		 * These commented-out values are copy-pasted from the code above
		 * for normal locks.
		 */
		//values[14] = Int32GetDatum(proc->mppSessionId);
		//values[15] = BoolGetDatum(proc->mppIsWriter);
		//values[16] = Int32GetDatum(Gp_segment);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);
		SRF_RETURN_NEXT(funcctx, result);
	}

	/*
	 * if we dispatched to the segDBs, free up the memory holding the result sets.
	 * Otherwise we might leak this memory each time we got called (does it automatically
	 * get freed by the pool being deleted?  Probably, but this is safer).
	 */
	if (mystatus->segresults != NULL)
	{
		int i;
		for (i = 0; i < mystatus->numsegresults; i++)
			PQclear(mystatus->segresults[i]);

		pfree(mystatus->segresults);
	}

	SRF_RETURN_DONE(funcctx);
}


/*
 * pg_blocking_pids - produce an array of the PIDs blocking given PID
 *
 * The reported PIDs are those that hold a lock conflicting with blocked_pid's
 * current request (hard block), or are requesting such a lock and are ahead
 * of blocked_pid in the lock's wait queue (soft block).
 *
 * In parallel-query cases, we report all PIDs blocking any member of the
 * given PID's lock group, and the reported PIDs are those of the blocking
 * PIDs' lock group leaders.  This allows callers to compare the result to
 * lists of clients' pg_backend_pid() results even during a parallel query.
 *
 * Parallel query makes it possible for there to be duplicate PIDs in the
 * result (either because multiple waiters are blocked by same PID, or
 * because multiple blockers have same group leader PID).  We do not bother
 * to eliminate such duplicates from the result.
 *
 * We need not consider predicate locks here, since those don't block anything.
 */
Datum
pg_blocking_pids(PG_FUNCTION_ARGS)
{
	int			blocked_pid = PG_GETARG_INT32(0);
	Datum	   *arrayelems;
	int			narrayelems;
	BlockedProcsData *lockData; /* state data from lmgr */
	int			i,
				j;

	/* Collect a snapshot of lock manager state */
	lockData = GetBlockerStatusData(blocked_pid);

	/* We can't need more output entries than there are reported PROCLOCKs */
	arrayelems = (Datum *) palloc(lockData->nlocks * sizeof(Datum));
	narrayelems = 0;

	/* For each blocked proc in the lock group ... */
	for (i = 0; i < lockData->nprocs; i++)
	{
		BlockedProcData *bproc = &lockData->procs[i];
		LockInstanceData *instances = &lockData->locks[bproc->first_lock];
		int		   *preceding_waiters = &lockData->waiter_pids[bproc->first_waiter];
		LockInstanceData *blocked_instance;
		LockMethod	lockMethodTable;
		int			conflictMask;

		/*
		 * Locate the blocked proc's own entry in the LockInstanceData array.
		 * There should be exactly one matching entry.
		 */
		blocked_instance = NULL;
		for (j = 0; j < bproc->num_locks; j++)
		{
			LockInstanceData *instance = &(instances[j]);

			if (instance->pid == bproc->pid)
			{
				Assert(blocked_instance == NULL);
				blocked_instance = instance;
			}
		}
		Assert(blocked_instance != NULL);

		lockMethodTable = GetLockTagsMethodTable(&(blocked_instance->locktag));
		conflictMask = lockMethodTable->conflictTab[blocked_instance->waitLockMode];

		/* Now scan the PROCLOCK data for conflicting procs */
		for (j = 0; j < bproc->num_locks; j++)
		{
			LockInstanceData *instance = &(instances[j]);

			/* A proc never blocks itself, so ignore that entry */
			if (instance == blocked_instance)
				continue;
			/* Members of same lock group never block each other, either */
			if (instance->leaderPid == blocked_instance->leaderPid)
				continue;

			if (conflictMask & instance->holdMask)
			{
				/* hard block: blocked by lock already held by this entry */
			}
			else if (instance->waitLockMode != NoLock &&
					 (conflictMask & LOCKBIT_ON(instance->waitLockMode)))
			{
				/* conflict in lock requests; who's in front in wait queue? */
				bool		ahead = false;
				int			k;

				for (k = 0; k < bproc->num_waiters; k++)
				{
					if (preceding_waiters[k] == instance->pid)
					{
						/* soft block: this entry is ahead of blocked proc */
						ahead = true;
						break;
					}
				}
				if (!ahead)
					continue;	/* not blocked by this entry */
			}
			else
			{
				/* not blocked by this entry */
				continue;
			}

			/* blocked by this entry, so emit a record */
			arrayelems[narrayelems++] = Int32GetDatum(instance->leaderPid);
		}
	}

	/* Assert we didn't overrun arrayelems[] */
	Assert(narrayelems <= lockData->nlocks);

	/* Construct array, using hardwired knowledge about int4 type */
	PG_RETURN_ARRAYTYPE_P(construct_array(arrayelems, narrayelems,
										  INT4OID,
										  sizeof(int32), true, TYPALIGN_INT));
}


/*
 * pg_safe_snapshot_blocking_pids - produce an array of the PIDs blocking
 * given PID from getting a safe snapshot
 *
 * XXX this does not consider parallel-query cases; not clear how big a
 * problem that is in practice
 */
Datum
pg_safe_snapshot_blocking_pids(PG_FUNCTION_ARGS)
{
	int			blocked_pid = PG_GETARG_INT32(0);
	int		   *blockers;
	int			num_blockers;
	Datum	   *blocker_datums;

	/* A buffer big enough for any possible blocker list without truncation */
	blockers = (int *) palloc(MaxBackends * sizeof(int));

	/* Collect a snapshot of processes waited for by GetSafeSnapshot */
	num_blockers =
		GetSafeSnapshotBlockingPids(blocked_pid, blockers, MaxBackends);

	/* Convert int array to Datum array */
	if (num_blockers > 0)
	{
		int			i;

		blocker_datums = (Datum *) palloc(num_blockers * sizeof(Datum));
		for (i = 0; i < num_blockers; ++i)
			blocker_datums[i] = Int32GetDatum(blockers[i]);
	}
	else
		blocker_datums = NULL;

	/* Construct array, using hardwired knowledge about int4 type */
	PG_RETURN_ARRAYTYPE_P(construct_array(blocker_datums, num_blockers,
										  INT4OID,
										  sizeof(int32), true, TYPALIGN_INT));
}


/*
 * pg_isolation_test_session_is_blocked - support function for isolationtester
 *
 * Check if specified PID is blocked by any of the PIDs listed in the second
 * argument.  Currently, this looks for blocking caused by waiting for
 * heavyweight locks or safe snapshots.  We ignore blockage caused by PIDs
 * not directly under the isolationtester's control, eg autovacuum.
 *
 * This is an undocumented function intended for use by the isolation tester,
 * and may change in future releases as required for testing purposes.
 */
Datum
pg_isolation_test_session_is_blocked(PG_FUNCTION_ARGS)
{
	int			blocked_pid = PG_GETARG_INT32(0);
	ArrayType  *interesting_pids_a = PG_GETARG_ARRAYTYPE_P(1);
	ArrayType  *blocking_pids_a;
	int32	   *interesting_pids;
	int32	   *blocking_pids;
	int			num_interesting_pids;
	int			num_blocking_pids;
	int			dummy;
	int			i,
				j;

	/* Validate the passed-in array */
	Assert(ARR_ELEMTYPE(interesting_pids_a) == INT4OID);
	if (array_contains_nulls(interesting_pids_a))
		elog(ERROR, "array must not contain nulls");
	interesting_pids = (int32 *) ARR_DATA_PTR(interesting_pids_a);
	num_interesting_pids = ArrayGetNItems(ARR_NDIM(interesting_pids_a),
										  ARR_DIMS(interesting_pids_a));

	/*
	 * Get the PIDs of all sessions blocking the given session's attempt to
	 * acquire heavyweight locks.
	 */
	blocking_pids_a =
		DatumGetArrayTypeP(DirectFunctionCall1(pg_blocking_pids, blocked_pid));

	Assert(ARR_ELEMTYPE(blocking_pids_a) == INT4OID);
	Assert(!array_contains_nulls(blocking_pids_a));
	blocking_pids = (int32 *) ARR_DATA_PTR(blocking_pids_a);
	num_blocking_pids = ArrayGetNItems(ARR_NDIM(blocking_pids_a),
									   ARR_DIMS(blocking_pids_a));

	/*
	 * Check if any of these are in the list of interesting PIDs, that being
	 * the sessions that the isolation tester is running.  We don't use
	 * "arrayoverlaps" here, because it would lead to cache lookups and one of
	 * our goals is to run quickly with debug_discard_caches > 0.  We expect
	 * blocking_pids to be usually empty and otherwise a very small number in
	 * isolation tester cases, so make that the outer loop of a naive search
	 * for a match.
	 */
	for (i = 0; i < num_blocking_pids; i++)
		for (j = 0; j < num_interesting_pids; j++)
		{
			if (blocking_pids[i] == interesting_pids[j])
				PG_RETURN_BOOL(true);
		}

	/*
	 * Check if blocked_pid is waiting for a safe snapshot.  We could in
	 * theory check the resulting array of blocker PIDs against the
	 * interesting PIDs list, but since there is no danger of autovacuum
	 * blocking GetSafeSnapshot there seems to be no point in expending cycles
	 * on allocating a buffer and searching for overlap; so it's presently
	 * sufficient for the isolation tester's purposes to use a single element
	 * buffer and check if the number of safe snapshot blockers is non-zero.
	 */
	if (GetSafeSnapshotBlockingPids(blocked_pid, &dummy, 1) > 0)
		PG_RETURN_BOOL(true);

	PG_RETURN_BOOL(false);
}


/*
 * Functions for manipulating advisory locks
 *
 * We make use of the locktag fields as follows:
 *
 *	field1: MyDatabaseId ... ensures locks are local to each database
 *	field2: first of 2 int4 keys, or high-order half of an int8 key
 *	field3: second of 2 int4 keys, or low-order half of an int8 key
 *	field4: 1 if using an int8 key, 2 if using 2 int4 keys
 */
#define SET_LOCKTAG_INT64(tag, key64) \
	SET_LOCKTAG_ADVISORY(tag, \
						 MyDatabaseId, \
						 (uint32) ((key64) >> 32), \
						 (uint32) (key64), \
						 1)
#define SET_LOCKTAG_INT32(tag, key1, key2) \
	SET_LOCKTAG_ADVISORY(tag, MyDatabaseId, key1, key2, 2)

/*
 * pg_advisory_lock(int8) - acquire exclusive lock on an int8 key
 */
Datum
pg_advisory_lock_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;

	SET_LOCKTAG_INT64(tag, key);

	(void) LockAcquire(&tag, ExclusiveLock, true, false);

	PG_RETURN_VOID();
}

/*
 * pg_advisory_xact_lock(int8) - acquire xact scoped
 * exclusive lock on an int8 key
 */
Datum
pg_advisory_xact_lock_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;

	SET_LOCKTAG_INT64(tag, key);

	(void) LockAcquire(&tag, ExclusiveLock, false, false);

	PG_RETURN_VOID();
}

/*
 * pg_advisory_lock_shared(int8) - acquire share lock on an int8 key
 */
Datum
pg_advisory_lock_shared_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;

	SET_LOCKTAG_INT64(tag, key);

	(void) LockAcquire(&tag, ShareLock, true, false);

	PG_RETURN_VOID();
}

/*
 * pg_advisory_xact_lock_shared(int8) - acquire xact scoped
 * share lock on an int8 key
 */
Datum
pg_advisory_xact_lock_shared_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;

	SET_LOCKTAG_INT64(tag, key);

	(void) LockAcquire(&tag, ShareLock, false, false);

	PG_RETURN_VOID();
}

/*
 * pg_try_advisory_lock(int8) - acquire exclusive lock on an int8 key, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_lock_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT64(tag, key);

	res = LockAcquire(&tag, ExclusiveLock, true, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_try_advisory_xact_lock(int8) - acquire xact scoped
 * exclusive lock on an int8 key, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_xact_lock_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT64(tag, key);

	res = LockAcquire(&tag, ExclusiveLock, false, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_try_advisory_lock_shared(int8) - acquire share lock on an int8 key, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_lock_shared_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT64(tag, key);

	res = LockAcquire(&tag, ShareLock, true, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_try_advisory_xact_lock_shared(int8) - acquire xact scoped
 * share lock on an int8 key, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_xact_lock_shared_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT64(tag, key);

	res = LockAcquire(&tag, ShareLock, false, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_advisory_unlock(int8) - release exclusive lock on an int8 key
 *
 * Returns true if successful, false if lock was not held
*/
Datum
pg_advisory_unlock_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;
	bool		res;

	SET_LOCKTAG_INT64(tag, key);

	res = LockRelease(&tag, ExclusiveLock, true);

	PG_RETURN_BOOL(res);
}

/*
 * pg_advisory_unlock_shared(int8) - release share lock on an int8 key
 *
 * Returns true if successful, false if lock was not held
 */
Datum
pg_advisory_unlock_shared_int8(PG_FUNCTION_ARGS)
{
	int64		key = PG_GETARG_INT64(0);
	LOCKTAG		tag;
	bool		res;

	SET_LOCKTAG_INT64(tag, key);

	res = LockRelease(&tag, ShareLock, true);

	PG_RETURN_BOOL(res);
}

/*
 * pg_advisory_lock(int4, int4) - acquire exclusive lock on 2 int4 keys
 */
Datum
pg_advisory_lock_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;

	SET_LOCKTAG_INT32(tag, key1, key2);

	(void) LockAcquire(&tag, ExclusiveLock, true, false);

	PG_RETURN_VOID();
}

/*
 * pg_advisory_xact_lock(int4, int4) - acquire xact scoped
 * exclusive lock on 2 int4 keys
 */
Datum
pg_advisory_xact_lock_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;

	SET_LOCKTAG_INT32(tag, key1, key2);

	(void) LockAcquire(&tag, ExclusiveLock, false, false);

	PG_RETURN_VOID();
}

/*
 * pg_advisory_lock_shared(int4, int4) - acquire share lock on 2 int4 keys
 */
Datum
pg_advisory_lock_shared_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;

	SET_LOCKTAG_INT32(tag, key1, key2);

	(void) LockAcquire(&tag, ShareLock, true, false);

	PG_RETURN_VOID();
}

/*
 * pg_advisory_xact_lock_shared(int4, int4) - acquire xact scoped
 * share lock on 2 int4 keys
 */
Datum
pg_advisory_xact_lock_shared_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;

	SET_LOCKTAG_INT32(tag, key1, key2);

	(void) LockAcquire(&tag, ShareLock, false, false);

	PG_RETURN_VOID();
}

/*
 * pg_try_advisory_lock(int4, int4) - acquire exclusive lock on 2 int4 keys, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_lock_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT32(tag, key1, key2);

	res = LockAcquire(&tag, ExclusiveLock, true, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_try_advisory_xact_lock(int4, int4) - acquire xact scoped
 * exclusive lock on 2 int4 keys, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_xact_lock_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT32(tag, key1, key2);

	res = LockAcquire(&tag, ExclusiveLock, false, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_try_advisory_lock_shared(int4, int4) - acquire share lock on 2 int4 keys, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_lock_shared_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT32(tag, key1, key2);

	res = LockAcquire(&tag, ShareLock, true, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_try_advisory_xact_lock_shared(int4, int4) - acquire xact scoped
 * share lock on 2 int4 keys, no wait
 *
 * Returns true if successful, false if lock not available
 */
Datum
pg_try_advisory_xact_lock_shared_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;
	LockAcquireResult res;

	SET_LOCKTAG_INT32(tag, key1, key2);

	res = LockAcquire(&tag, ShareLock, false, true);

	PG_RETURN_BOOL(res != LOCKACQUIRE_NOT_AVAIL);
}

/*
 * pg_advisory_unlock(int4, int4) - release exclusive lock on 2 int4 keys
 *
 * Returns true if successful, false if lock was not held
*/
Datum
pg_advisory_unlock_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;
	bool		res;

	SET_LOCKTAG_INT32(tag, key1, key2);

	res = LockRelease(&tag, ExclusiveLock, true);

	PG_RETURN_BOOL(res);
}

/*
 * pg_advisory_unlock_shared(int4, int4) - release share lock on 2 int4 keys
 *
 * Returns true if successful, false if lock was not held
 */
Datum
pg_advisory_unlock_shared_int4(PG_FUNCTION_ARGS)
{
	int32		key1 = PG_GETARG_INT32(0);
	int32		key2 = PG_GETARG_INT32(1);
	LOCKTAG		tag;
	bool		res;

	SET_LOCKTAG_INT32(tag, key1, key2);

	res = LockRelease(&tag, ShareLock, true);

	PG_RETURN_BOOL(res);
}

/*
 * pg_advisory_unlock_all() - release all advisory locks
 */
Datum
pg_advisory_unlock_all(PG_FUNCTION_ARGS)
{
	LockReleaseSession(USER_LOCKMETHOD);

	PG_RETURN_VOID();
}
