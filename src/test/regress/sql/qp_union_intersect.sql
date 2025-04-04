-- Create common test tables
--
-- All the test statements use these same test tables. To avoid having to
-- re-create them for every test, all the actual tests are wrapped in
-- begin-rollback blocks (except a few that throw an ERROR, and will
-- therefore roll back implicitly anyway). The purpose of these tests is to
-- exercise planner, so it doesn't matter that the changes are rolled back
-- afterwards.

-- start_matchsubs
-- m/DETAIL:  Failing row contains \(.*\)/
-- s/DETAIL:  Failing row contains \(.*\)/DETAIL:  Failing row contains (#####)/
-- end_matchsubs
begin;
CREATE TABLE dml_union_r (
        a int CONSTRAINT r_check_a CHECK(a <> -1),
        b int,
        c text,
        d numeric)
DISTRIBUTED BY (a);

CREATE TABLE dml_union_s (
        a int ,
        b int not NULL,
        c text ,
        d numeric default 10.00)
DISTRIBUTED BY (b)
PARTITION BY range(d);

CREATE TABLE dml_union_s_1_prt_2 PARTITION OF dml_union_s FOR VALUES FROM (1) TO (1001);
CREATE TABLE dml_union_s_1_prt_3 PARTITION OF dml_union_s FOR VALUES FROM (1001) TO (1101);
CREATE TABLE dml_union_s_1_prt_4 PARTITION OF dml_union_s FOR VALUES FROM (1101) TO (1201);
CREATE TABLE dml_union_s_1_prt_5 PARTITION OF dml_union_s FOR VALUES FROM (1201) TO (1301);
CREATE TABLE dml_union_s_1_prt_6 PARTITION OF dml_union_s FOR VALUES FROM (1301) TO (1401);
CREATE TABLE dml_union_s_1_prt_7 PARTITION OF dml_union_s FOR VALUES FROM (1401) TO (1501);
CREATE TABLE dml_union_s_1_prt_8 PARTITION OF dml_union_s FOR VALUES FROM (1501) TO (1601);
CREATE TABLE dml_union_s_1_prt_9 PARTITION OF dml_union_s FOR VALUES FROM (1601) TO (1701);
CREATE TABLE dml_union_s_1_prt_10 PARTITION OF dml_union_s FOR VALUES FROM (1701) TO (1801);
CREATE TABLE dml_union_s_1_prt_11 PARTITION OF dml_union_s FOR VALUES FROM (1801) TO (1901);
CREATE TABLE dml_union_s_1_prt_def PARTITION OF dml_union_s DEFAULT;

INSERT INTO dml_union_r SELECT generate_series(1,100), generate_series(1,100) * 3,'r', generate_series(1,100) % 6;
INSERT INTO dml_union_r VALUES(NULL,NULL,'text',NULL),(NULL,NULL,'text',NULL),(NULL,NULL,'text',NULL),(NULL,NULL,'text',NULL),(NULL,NULL,'text',NULL);
INSERT INTO dml_union_r VALUES(1,1,'text',1),(2,2,'text',2),(3,3,'text',3),(4,4,'text',4),(5,5,'text',5);
INSERT INTO dml_union_r VALUES(1,1,'text',1),(2,2,'text',2),(3,3,'text',3),(4,4,'text',4),(5,5,'text',5);
INSERT INTO dml_union_r VALUES(1,2,'text',3),(2,3,'text',4),(3,4,'text',5),(4,5,'text',6),(5,6,'text',7);

INSERT INTO dml_union_s SELECT generate_series(1,100), generate_series(1,100) * 3,'s', generate_series(1,100) ;
INSERT INTO dml_union_s VALUES(1,1,'text',1),(2,2,'text',2),(3,3,'text',3),(4,4,'text',4),(5,5,'text',5);
INSERT INTO dml_union_s VALUES(1,1,'text',1),(2,2,'text',2),(3,3,'text',3),(4,4,'text',4),(5,5,'text',5);
INSERT INTO dml_union_s VALUES(1,2,'text',3),(2,3,'text',4),(3,4,'text',5),(4,5,'text',6),(5,6,'text',7);
commit;


-- @description union_test1: INSERT and INTERSECT operation
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT dml_union_r.a, dml_union_r.b, dml_union_r.c, dml_union_r.d FROM dml_union_r INTERSECT SELECT dml_union_s.* FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT dml_union_r.a, dml_union_r.b, dml_union_r.c, dml_union_r.d FROM dml_union_r INTERSECT SELECT dml_union_s.* FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test2: INSERT and INTERSECT operation
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT dml_union_r.* FROM dml_union_r INTERSECT ALL SELECT dml_union_s.a, dml_union_s.b, dml_union_s.c, dml_union_s.d FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT dml_union_r.* FROM dml_union_r INTERSECT ALL SELECT dml_union_s.a, dml_union_s.b, dml_union_s.c, dml_union_s.d FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test3: INTERSECT with generate_series
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT generate_series(1,10) INTERSECT SELECT generate_series(1,100))foo;
INSERT INTO dml_union_r SELECT generate_series(1,10) INTERSECT SELECT generate_series(1,100);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test4: INTERSECT with generate_series
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT generate_series(1,10) INTERSECT ALL  SELECT generate_series(1,100))foo;
INSERT INTO dml_union_r SELECT generate_series(1,10) INTERSECT ALL  SELECT generate_series(1,100);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test5: INTERSECT with constants
begin;
SELECT COUNT(*) FROM dml_union_s;
SELECT COUNT(*) FROM (SELECT dml_union_r.a, dml_union_r.b,'A' as c, 0 as d FROM dml_union_r INTERSECT SELECT dml_union_s.a, dml_union_s.b,'A' as C,0 as d FROM dml_union_s)foo;
INSERT INTO dml_union_s (SELECT dml_union_r.a, dml_union_r.b,'A' as c, 0 as d FROM dml_union_r INTERSECT SELECT dml_union_s.a, dml_union_s.b,'A' as C,0 as d FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_s;
rollback;

-- @description union_test6: INTERSECT with constants
begin;
SELECT COUNT(*) FROM dml_union_s;
SELECT COUNT(*) FROM (SELECT dml_union_r.a, dml_union_r.b,'A' as c ,0 as d FROM dml_union_r INTERSECT ALL SELECT dml_union_s.a, dml_union_s.b,'A' as C,0 as d FROM dml_union_s)foo;
INSERT INTO dml_union_s (SELECT dml_union_r.a, dml_union_r.b,'A' as c ,0 as d FROM dml_union_r INTERSECT ALL SELECT dml_union_s.a, dml_union_s.b,'A' as C,0 as d FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_s;
rollback;

-- @description union_test7: INTERSECT with DISTINCT
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT distinct a,b,c,d FROM dml_union_r INTERSECT SELECT distinct a,b,c,d FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT distinct a,b,c,d FROM dml_union_r INTERSECT SELECT distinct a,b,c,d FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test8: INTERSECT with DISTINCT
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT distinct a,b,c,d FROM dml_union_r INTERSECT ALL SELECT distinct a,b,c,d FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT distinct a,b,c,d FROM dml_union_r INTERSECT ALL SELECT distinct a,b,c,d FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test9:  INSERT and EXCEPT operation
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT dml_union_r.a, dml_union_r.b, dml_union_r.c, dml_union_r.d FROM dml_union_r EXCEPT SELECT * FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT dml_union_r.a, dml_union_r.b, dml_union_r.c, dml_union_r.d FROM dml_union_r EXCEPT SELECT * FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test10:  INSERT and EXCEPT operation
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT * FROM dml_union_r EXCEPT ALL SELECT dml_union_s.* FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT * FROM dml_union_r EXCEPT ALL SELECT dml_union_s.* FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test12: EXCEPT with generate_series
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT generate_series(1,10) EXCEPT ALL SELECT generate_series(1,10))foo;
INSERT INTO dml_union_r SELECT generate_series(1,10) EXCEPT ALL SELECT generate_series(1,10);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test13: EXCEPT with predicate
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT * FROM (SELECT * FROM dml_union_r EXCEPT ALL SELECT * FROM dml_union_s) foo WHERE c='text')bar;
INSERT INTO dml_union_r SELECT * FROM (SELECT * FROM dml_union_r EXCEPT ALL SELECT * FROM dml_union_s) foo WHERE c='text';
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test14: EXCEPT with predicate (returns 0 rows)
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT * FROM (SELECT * FROM dml_union_r EXCEPT SELECT * FROM dml_union_s) foo WHERE c='s')bar;
INSERT INTO dml_union_r SELECT * FROM (SELECT * FROM dml_union_r EXCEPT SELECT * FROM dml_union_s) foo WHERE c='s';
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test15: EXCEPT with constants
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT dml_union_r.a, dml_union_r.b,'A' as c ,0 as d FROM dml_union_r EXCEPT ALL SELECT dml_union_s.a, dml_union_s.b,'A' as C,0 as d FROM dml_union_s)foo;
INSERT INTO dml_union_r (SELECT dml_union_r.a, dml_union_r.b,'A' as c ,0 as d FROM dml_union_r EXCEPT ALL SELECT dml_union_s.a, dml_union_s.b,'A' as C,0 as d FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test16: EXCEPT with distinct
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT distinct a,b,c,d FROM dml_union_r EXCEPT SELECT distinct a,b,c,d FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT distinct a,b,c,d FROM dml_union_r EXCEPT SELECT distinct a,b,c,d FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test17: EXCEPT with distinct
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT distinct a,b,c,d FROM dml_union_r EXCEPT ALL SELECT distinct a,b,c,d FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT distinct a,b,c,d FROM dml_union_r EXCEPT ALL SELECT distinct a,b,c,d FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test18: INSERT and UNION operation
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT dml_union_r.a, dml_union_r.b, dml_union_r.c, dml_union_r.d FROM dml_union_r UNION SELECT dml_union_s.* FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT dml_union_r.a, dml_union_r.b, dml_union_r.c, dml_union_r.d FROM dml_union_r UNION SELECT dml_union_s.* FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test19: INSERT and UNION operation
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT dml_union_r.* FROM dml_union_r UNION All SELECT * FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT dml_union_r.* FROM dml_union_r UNION All SELECT * FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test20: UNION with generate_series
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT generate_series(1,10) UNION SELECT generate_series(1,10))foo;
INSERT INTO dml_union_r SELECT generate_series(1,10) UNION SELECT generate_series(1,10);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test21: UNION with generate_series
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT generate_series(1,10) UNION ALL SELECT generate_series(1,10))foo;
INSERT INTO dml_union_r SELECT generate_series(1,10) UNION ALL SELECT generate_series(1,10);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test22: UNION with limit
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT * FROM dml_union_r UNION ALL SELECT * FROM dml_union_s ORDER BY 1,2,3,4) foo LIMIT 10;
SELECT COUNT(*) FROM dml_union_r;
INSERT INTO dml_union_r SELECT * FROM (SELECT * FROM dml_union_r UNION ALL SELECT * FROM dml_union_s ORDER BY 1,2,3,4) foo LIMIT 10;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test23: UNION with dml_union_sub-query in SELECT
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT NULL,(SELECT NULL f1 FROM dml_union_r UNION SELECT NULL f1 FROM dml_union_s)::int, 'nullval',NULL)foo;
INSERT INTO dml_union_r SELECT NULL,(SELECT NULL f1 FROM dml_union_r UNION SELECT NULL f1 FROM dml_union_s)::int, 'nullval',NULL;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test24: UNION with exists
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT AVG(a),10,'avg',10 FROM dml_union_r WHERE exists (SELECT a FROM dml_union_r UNION ALL SELECT b FROM dml_union_s))foo;
INSERT INTO dml_union_r SELECT AVG(a),10,'avg',10 FROM dml_union_r WHERE exists (SELECT a FROM dml_union_r UNION ALL SELECT b FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test25: UNION with DISTINCT
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT distinct a,b,c,d FROM dml_union_r UNION SELECT distinct a,b,c,d FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT distinct a,b,c,d FROM dml_union_r UNION SELECT distinct a,b,c,d FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test26: UNION with AGGREGATE
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT * FROM (SELECT AVG(a) as a FROM dml_union_r UNION SELECT AVG(b) as a FROM dml_union_s) foo)bar;
INSERT INTO dml_union_r SELECT * FROM (SELECT AVG(a) as a FROM dml_union_r UNION SELECT AVG(b) as a FROM dml_union_s) foo;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test27: Negative tests VIOLATES NULL VALUE CONSTRAINT
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM ( SELECT * FROM (SELECT * FROM dml_union_r EXCEPT SELECT * FROM dml_union_s ) foo WHERE c='text')bar;
INSERT INTO dml_union_s SELECT * FROM (SELECT * FROM dml_union_r EXCEPT SELECT * FROM dml_union_s) foo WHERE c='text';
--SELECT COUNT(*) FROM dml_union_r;

-- @description union_test28: Negative tests MORE THAN ONE ROW RETURNED
SELECT COUNT(*) FROM dml_union_r;
INSERT INTO dml_union_r SELECT (SELECT dml_union_r.d::int FROM dml_union_r INTERSECT SELECT dml_union_s.d FROM dml_union_s ORDER BY 1),1,'newval',1.000;
SELECT COUNT(*) FROM dml_union_r;

-- @description union_test29: INSERT NON ATOMICS with union/intersect/except
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT dml_union_r.* FROM dml_union_r INTERSECT (SELECT dml_union_r.* FROM dml_union_r UNION ALL SELECT dml_union_s.* FROM dml_union_s) EXCEPT SELECT dml_union_s.* FROM dml_union_s)foo;
INSERT INTO dml_union_r SELECT dml_union_r.* FROM dml_union_r INTERSECT (SELECT dml_union_r.* FROM dml_union_r UNION ALL SELECT dml_union_s.* FROM dml_union_s) EXCEPT SELECT dml_union_s.* FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test30: INSERT NON ATOMICS with union/intersect/except
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT c,c+1,case when c = 1 then 'r' else 's' end,c+1 FROM (    SELECT COUNT(distinct c) c FROM (SELECT f1, f2 , COUNT(*) c FROM (SELECT 10 f1, 'r' f2 FROM dml_union_r UNION SELECT 40 f1, 's' f2 FROM dml_union_r UNION SELECT a, c FROM dml_union_r INTERSECT SELECT a, c FROM dml_union_s ORDER BY 1) foo group by f1,f2) foo)foo)bar;
INSERT INTO dml_union_r SELECT c,c+1,case when c = 1 then 'r' else 's' end,c+1 FROM (SELECT COUNT(distinct c) c FROM (SELECT f1, f2 , COUNT(*) c FROM (SELECT 10 f1, 'r' f2 FROM dml_union_r UNION SELECT 40 f1, 's' f2 FROM dml_union_r UNION SELECT a, c FROM dml_union_r INTERSECT SELECT a, c FROM dml_union_s ORDER BY 1) foo group by f1,f2) foo)foo;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_test31: INSERT NON ATOMICS with union/intersect/except
begin;
SELECT COUNT(*) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT * FROM dml_union_r WHERE a in (SELECT dml_union_r.d::int FROM dml_union_r INTERSECT SELECT dml_union_s.d FROM dml_union_s ORDER BY 1) UNION SELECT * FROM dml_union_s)bar;
INSERT INTO dml_union_r SELECT * FROM dml_union_r WHERE a in (SELECT dml_union_r.d::int FROM dml_union_r INTERSECT SELECT dml_union_s.d FROM dml_union_s ORDER BY 1) UNION SELECT * FROM dml_union_s;
SELECT COUNT(*) FROM dml_union_r;
rollback;


-- @description union_delete_test1:  With UNION/INTERSECT/EXCEPT in dml_union_subquery
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a in (SELECT a FROM dml_union_r UNION SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test2:  With UNION/INTERSECT/EXCEPT in dml_union_subquery
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a in (SELECT a FROM dml_union_r UNION ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test3:  With UNION/INTERSECT/EXCEPT in dml_union_subquery
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a in (SELECT a FROM dml_union_r INTERSECT SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test4:  With UNION/INTERSECT/EXCEPT in dml_union_subquery
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a in (SELECT a FROM dml_union_r INTERSECT ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test5:  With UNION/INTERSECT/EXCEPT in dml_union_subquery
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a in (SELECT a FROM dml_union_r EXCEPT SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test6:  With UNION/INTERSECT/EXCEPT in dml_union_subquery
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a in (SELECT a FROM dml_union_r EXCEPT ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test7:  With UNION/INTERSECT/EXCEPT in the predicate condition ( 0 rows)
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a = (SELECT NULL UNION SELECT NULL)::int;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test8:  With UNION/INTERSECT/EXCEPT in the predicate condition ( 0 rows )
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a = (SELECT NULL INTERSECT SELECT NULL)::int;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test9:  With UNION/INTERSECT/EXCEPT in the predicate condition( 0 rows )
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a = (SELECT NULL EXCEPT SELECT NULL)::int;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test10:  With UNION/INTERSECT/EXCEPT in the predicate condition
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a = (SELECT a FROM dml_union_r UNION SELECT a FROM dml_union_s ORDER BY a LIMIT 1);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test11:  With UNION/INTERSECT/EXCEPT in the predicate condition
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a = (SELECT a FROM dml_union_r INTERSECT SELECT a FROM dml_union_s ORDER BY a LIMIT 1);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test12:  With UNION/INTERSECT/EXCEPT in the predicate condition
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r WHERE a = (SELECT a FROM dml_union_r EXCEPT SELECT a FROM dml_union_s ORDER BY a LIMIT 1);
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test13:  With UNION/INTERSECT/EXCEPT
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r USING (SELECT a FROM dml_union_r UNION SELECT a FROM dml_union_s) foo WHERE foo.a = dml_union_r.a;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test14:  With UNION/INTERSECT/EXCEPT
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r USING (SELECT a FROM dml_union_r INTERSECT SELECT a FROM dml_union_s) foo WHERE foo.a = dml_union_r.a;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test15:  With UNION/INTERSECT/EXCEPT
begin;
SELECT COUNT(*) FROM dml_union_r;
DELETE FROM dml_union_r USING (SELECT a FROM dml_union_r EXCEPT SELECT a FROM dml_union_s) foo WHERE foo.a = dml_union_r.a;
SELECT COUNT(*) FROM dml_union_r;
rollback;

-- @description union_delete_test16:  Using Partition table
begin;
SELECT COUNT(*) FROM dml_union_s;
DELETE FROM dml_union_s USING (SELECT a FROM dml_union_r UNION SELECT a FROM dml_union_s_1_prt_def) foo;
SELECT COUNT(*) FROM dml_union_s;
rollback;

-- @description union_delete_test17:  Using Partition table
begin;
SELECT COUNT(*) FROM dml_union_s;
DELETE FROM dml_union_s USING (SELECT * FROM dml_union_r UNION SELECT * FROM dml_union_s_1_prt_def) foo WHERE foo.d = dml_union_s.d;
SELECT COUNT(*) FROM dml_union_s;
rollback;

-- @description union_update_test1: Update distribution column with UNION
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT a FROM dml_union_r UNION SELECT a FROM dml_union_s ORDER BY 1 LIMIT 1;
UPDATE dml_union_r SET a = (SELECT a FROM dml_union_r UNION SELECT a FROM dml_union_s ORDER BY 1 LIMIT 1);
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
rollback;

-- @description union_update_test2: Update distribution column with UNION
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
UPDATE dml_union_r SET a = (SELECT a FROM dml_union_r UNION ALL SELECT a FROM dml_union_s ORDER BY 1 LIMIT 1);
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT DISTINCT(a) FROM dml_union_r;
rollback;

-- @description union_update_test3: Update distribution column with INTERSECT
begin;
SELECT COUNT(*) FROM dml_union_r WHERE a = 1;
SELECT COUNT(*) FROM (SELECT * FROM (SELECT a FROM dml_union_r order by a limit 1) foo INTERSECT SELECT a FROM dml_union_s)bar;
UPDATE dml_union_r SET a = ( SELECT * FROM (SELECT a FROM dml_union_r order by a limit 1) foo INTERSECT SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r WHERE a = 1;
rollback;

-- @description union_update_test4: Update distribution column with INTERSECT
begin;
SELECT COUNT(*) FROM dml_union_r WHERE a = 1;
SELECT COUNT(*) FROM (SELECT * FROM (SELECT a FROM dml_union_r ORDER BY 1 limit 1) foo INTERSECT ALL SELECT a FROM dml_union_s)bar; 
UPDATE dml_union_r SET a = ( SELECT * FROM (SELECT a FROM dml_union_r ORDER BY 1 limit 1) foo INTERSECT ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r WHERE a = 1;
rollback;

-- @description union_update_test5: Update distribution column with EXCEPT
begin;
SELECT SUM(a) FROM dml_union_r;
SELECT COUNT(*) FROM (SELECT * FROM (SELECT a FROM dml_union_r limit 1) foo EXCEPT SELECT a FROM dml_union_s)bar;
UPDATE dml_union_r SET a = ( SELECT * FROM (SELECT a FROM dml_union_r limit 1) foo EXCEPT SELECT a FROM dml_union_s);
SELECT SUM(a) FROM dml_union_r;
rollback;

-- @description union_update_test6: Update distribution column with EXCEPT
begin;
UPDATE dml_union_r SET a = ( SELECT * FROM (SELECT a FROM dml_union_r limit 1) foo EXCEPT ALL SELECT a FROM dml_union_s);
SELECT DISTINCT(a) FROM dml_union_r;
rollback;

-- @description union_update_test7: NULL values to distribution key
begin;
UPDATE dml_union_r SET a = (SELECT NULL UNION SELECT NULL)::int;
SELECT DISTINCT(a) FROM dml_union_r;
rollback;

-- @description union_update_test8: NULL values to distribution key
begin;
UPDATE dml_union_r SET a = (SELECT NULL INTERSECT SELECT NULL)::int;
SELECT DISTINCT(a) FROM dml_union_r;
rollback;

-- @description union_update_test9: NULL values to distribution key
begin;
UPDATE dml_union_r SET a = (SELECT NULL INTERSECT ALL SELECT NULL)::int;
SELECT DISTINCT(a) FROM dml_union_r;
rollback;

-- @description union_update_test10: NULL values to distribution key
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r; 
UPDATE dml_union_r SET a = (SELECT NULL EXCEPT SELECT NULL)::int;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r; 
rollback;

-- @description union_update_test11: NULL values to text
begin;
SELECT COUNT(DISTINCT(c)) FROM dml_union_r; 
UPDATE dml_union_r SET c = (SELECT NULL EXCEPT ALL SELECT NULL);
SELECT COUNT(DISTINCT(c)) FROM dml_union_r; 
rollback;

-- @description union_update_test12: Update partition key to NULL values when default partition present
begin;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
UPDATE dml_union_s SET d = (SELECT NULL UNION SELECT NULL)::numeric;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
rollback;

-- @description union_update_test13: Update partition key to NULL values when default partition present
begin;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
UPDATE dml_union_s SET d = (SELECT NULL INTERSECT SELECT NULL)::numeric;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
rollback;

-- @description union_update_test14: Update partition key to NULL values when default partition present
begin;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
UPDATE dml_union_s SET d = (SELECT NULL INTERSECT ALL SELECT NULL)::numeric;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
rollback;

-- @description union_update_test15: Update partition key to NULL values when default partition present
begin;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
UPDATE dml_union_s SET d = (SELECT NULL EXCEPT SELECT NULL)::numeric;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
rollback;

-- @description union_update_test16: Update partition key to NULL values when default partition present
begin;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
UPDATE dml_union_s SET d = (SELECT NULL EXCEPT ALL SELECT NULL)::numeric;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s; 
rollback;

-- @description union_update_test17: Update partition key to constant value with UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(*) FROM dml_union_r WHERE d = 20000; 
SELECT COUNT(*) FROM (SELECT a FROM dml_union_r UNION ALL SELECT a FROM dml_union_s)foo;
UPDATE dml_union_r SET d = 20000 WHERE a in (SELECT a FROM dml_union_r UNION ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r WHERE d = 20000; 
rollback;

-- @description union_update_test18: Update partition key to constant value with UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(*) FROM dml_union_r WHERE d = 20000; 
SELECT COUNT(*) FROM (SELECT a FROM dml_union_r INTERSECT ALL SELECT a FROM dml_union_s)foo;
UPDATE dml_union_r SET d = 20000 WHERE a in (SELECT a FROM dml_union_r INTERSECT ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r WHERE d = 20000; 
rollback;

-- @description union_update_test19: Update partition key to constant value with UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(*) FROM dml_union_r WHERE d = 20000; 
SELECT COUNT(*) FROM (SELECT a FROM dml_union_r EXCEPT ALL SELECT a FROM dml_union_s)foo;
UPDATE dml_union_r SET d = 20000 WHERE a in (SELECT a FROM dml_union_r EXCEPT ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r WHERE d = 20000; 
rollback;

-- @description union_update_test20:  UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
UPDATE dml_union_r SET a = dml_union_s.a FROM dml_union_s WHERE dml_union_r.b in (SELECT b FROM dml_union_r UNION SELECT b FROM dml_union_s);
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
rollback;

-- @description union_update_test21:  UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
UPDATE dml_union_r SET a = dml_union_r.a WHERE b in (SELECT b FROM dml_union_r INTERSECT SELECT b FROM dml_union_s);
SELECT DISTINCT(a) FROM dml_union_r;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
rollback;

-- @description union_update_test22:  UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
UPDATE dml_union_r SET a = dml_union_r.a WHERE b in (SELECT b FROM dml_union_r EXCEPT SELECT b FROM dml_union_s);
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT DISTINCT(a) FROM dml_union_r;
rollback;

-- @description union_update_test23: Update distribution column to constant value with UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT COUNT(*) FROM dml_union_r WHERE a = 0;
UPDATE dml_union_r SET a = 0 WHERE a in (SELECT a FROM dml_union_r UNION ALL SELECT a FROM dml_union_s);
SELECT COUNT(*) FROM dml_union_r WHERE a = 0;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
rollback;

-- @description union_update_test24: Update distribution column to constant value with UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT COUNT(*) FROM dml_union_r WHERE a = 0;
UPDATE dml_union_r SET a = 0 WHERE a in (SELECT a FROM dml_union_r INTERSECT ALL SELECT a FROM dml_union_s);
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT COUNT(*) FROM dml_union_r WHERE a = 0;
rollback;

-- @description union_update_test25: Update distribution column to constant value with UNION/INTERSECT/EXCEPT within dml_union_sub-query
begin;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT COUNT(*) FROM dml_union_r WHERE a = 0;
UPDATE dml_union_r SET a = 0 WHERE a in (SELECT a FROM dml_union_r EXCEPT ALL SELECT a FROM dml_union_s);
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
SELECT COUNT(*) FROM dml_union_r WHERE a = 0;
rollback;

-- @description union_update_test26: Negative Tests Update the partition key to an out of dml_union_range value with no default partition
begin;
DROP TABLE dml_union_s_1_prt_def;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s;
UPDATE dml_union_s SET d = (SELECT NULL UNION SELECT NULL)::numeric;
--SELECT DISTINCT(d) FROM dml_union_s;
--SELECT COUNT(DISTINCT(d)) FROM dml_union_s;
rollback;

-- @description union_update_test27: Negative Tests Update the partition key to an out of range value with no default partition
begin;
DROP TABLE dml_union_s_1_prt_def;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s;
UPDATE dml_union_s SET d = (SELECT NULL INTERSECT SELECT NULL)::numeric; 
--SELECT DISTINCT(d) FROM dml_union_s;
--SELECT COUNT(DISTINCT(d)) FROM dml_union_s;
rollback;

-- @description union_update_test28: Negative Tests Update the partition key to an out of dml_union_range value with no default partition
begin;
DROP TABLE dml_union_s_1_prt_def;
SELECT COUNT(DISTINCT(d)) FROM dml_union_s;
UPDATE dml_union_s SET d = (SELECT NULL EXCEPT SELECT NULL)::numeric; 
--SELECT DISTINCT(d) FROM dml_union_s;
--SELECT COUNT(DISTINCT(d)) FROM dml_union_s;
rollback;

-- @description union_update_test29: Negative Tests  UPDATE violates the CHECK constraint on the column
SELECT COUNT(DISTINCT(b)) FROM dml_union_s;
UPDATE dml_union_s SET b = (SELECT NULL UNION SELECT NULL)::numeric;
--SELECT COUNT(DISTINCT(b)) FROM dml_union_s;
--SELECT DISTINCT(b) FROM dml_union_s;

-- @description union_update_test30: Negative Tests  more than one row returned by a sub-query used as an expression
--
-- The access plan of this UPDATE is sensitive to optimizer_segments. With
-- ORCA, the error message varies depending accesss plan; you either get:
--
--   ERROR:  more than one row returned by a subquery used as an expression
--
-- like with the Postgres planner, or you get:
--
--   ERROR:  one or more assertions failed
--   DETAIL:  Expected no more than one row to be returned by expression
--
-- To make the output stable, arbitrarily fix optimizer_segments to 2, to get the latter.
set optimizer_segments=2;
ANALYZE dml_union_r, dml_union_s;
SELECT COUNT(DISTINCT(a)) FROM dml_union_r;
UPDATE dml_union_r SET a = ( SELECT a FROM dml_union_r UNION ALL SELECT a FROM dml_union_s);
reset optimizer_segments;
--SELECT COUNT(DISTINCT(a)) FROM dml_union_r;

-- @description union_update_test31: Negative Tests  more than one row returned by a sub-query used as an expression
UPDATE dml_union_r SET b = ( SELECT a FROM dml_union_r EXCEPT ALL SELECT a FROM dml_union_s);

--
-- Test mixing a set-returning function, which can be evaluated anywhere,
-- (it has General locus) and a diststributed table, in an Append.
--
explain (costs off)
select a from dml_union_r where a > 95
union all
select g from generate_series(1,2) g;

select a from dml_union_r where a > 95
union all
select g from generate_series(1,2) g;

explain (costs off)
select sum(a) from (
    select a from dml_union_r where a > 95
    union all
    select g from generate_series(1,2) g
) t;

select sum(a) from (
   select a from dml_union_r where a > 95
   union all
   select g from generate_series(1,2) g
) t;

--
-- Continue to test appending General to distributed table.
-- This time, the General is a dummy path, produced by pushing down condition.
-- (Only for planner, orca does not create dummy path here)
--
create table t_test_append_hash(a int, b int, c int) distributed by (a);
insert into t_test_append_hash select i, i+1, i+2 from generate_series(1, 5)i;

explain (costs off)
with t(a, b, s) as (
    select a, b, sum(c) from t_test_append_hash where a > b group by a, b
    union all
    select a, b, sum(c) from t_test_append_hash where a < b group by a, b
) select * from t where t.a < t.b;

with t(a, b, s) as (
    select a, b, sum(c) from t_test_append_hash where a > b group by a, b
    union all
    select a, b, sum(c) from t_test_append_hash where a < b group by a, b
) select * from t where t.a < t.b;

-- Test mixing a SegmentGeneral with distributed table.
create table t_test_append_rep(a int, b int, c int) distributed replicated;
insert into t_test_append_rep select i, i+1, i+2 from generate_series(5, 10)i;

explain (costs off)
select * from t_test_append_rep
union all
select * from t_test_append_hash;

select * from t_test_append_rep
union all
select * from t_test_append_hash;

-- Test value scan union all with a distributed table that direct dispatch
-- value scan's locus is general, so it will use Result plan node with
-- resconstantqual to be gp_execution_segment() = <some segid> to turn
-- general locus to partitioned locus to avoid gather partitioned locus
-- table to singleQE. When the subplan of partitioned table's scan can
-- use direct dispatch, previously, the result plan does not handle
-- direct dispatch correctly. This case cannot test plan, this is because
-- gp_execution_segment() = <some segid> the filter segid is randomly picked.
-- So the result plan's direct dispatch info is also random. We print the plan
-- and ignore it for better debugging info if error happens.
-- See github issue https://github.com/greenplum-db/gpdb/issues/9874 for details.

create table t_github_issue_9874 (a int) distributed by (a);
-- start_ignore
explain (costs off)
select 1
union all
select * from t_github_issue_9874 where a = 1;
-- end_ignore
select 1
union all
select * from t_github_issue_9874 where a = 1;

--
-- Test mixing a SegmentGeneral with distributed table
-- when gp_enable_direct_dispatch is off.
--
begin;
create table rt1(a int, b int) distributed replicated;
create table t1(a int, b int);
insert into t1 select i, i+1 from generate_series(6, 9) i;
insert into rt1 select i, i+1 from generate_series(1, 5) i;
set local gp_enable_direct_dispatch = on;
explain(costs off) select * from rt1 union all select * from t1;
select * from rt1 union all select * from t1;
set local gp_enable_direct_dispatch = off;
select * from rt1 union all select * from t1;
reset gp_enable_direct_dispatch;
abort;

-- Test mixing a SegmentGeneral with General locus scan.
explain (costs off)
select a from t_test_append_rep
union all
select * from generate_series(100, 105);

select a from t_test_append_rep
union all
select * from generate_series(100, 105);

-- test INTERSECT/EXCEPT with General and partitioned locus, but none of the columns are hashable
CREATE TABLE p1(a int) distributed by (a);
INSERT INTO p1 select generate_series(1,10);
explain (costs off)
select from generate_series(1,5) intersect select from p1;
select from generate_series(1,5) intersect select from p1;
explain (costs off)
select from generate_series(1,5) except select from p1;
select from generate_series(1,5) except select from p1;

--
-- Test for creation of MergeAppend paths.
--
-- We used to have a bug in creation of MergeAppend paths, so that this failed
-- with "could not find pathkey item to sort" error.  See
-- https://github.com/greenplum-db/gpdb/issues/5695
--
create table mergeappend_test ( a int, b int, x int ) distributed by (a,b);
insert into mergeappend_test select g/100, g/100, g from generate_series(1, 500) g;
analyze mergeappend_test;

select a, b, array_dims(array_agg(x)) from mergeappend_test r group by a, b
union all
select null, null, array_dims(array_agg(x)) from mergeappend_test r
order by 1,2;

-- Check that it's using a MergeAppend
set enable_hashagg=off;
explain (costs off)
select a, b, array_dims(array_agg(x)) from mergeappend_test r group by a, b
union all
select null, null, array_dims(array_agg(x)) from mergeappend_test r
order by 1,2;

-- This used to trip an assertion in MotionStateFinderWalker(), when we were
-- missing support for MergeAppend in planstate_walk_kids().
-- (https://github.com/greenplum-db/gpdb/issues/6668)
select a, b, array_dims(array_agg(x)) from mergeappend_test r group by a, b
union all
select null, null, array_dims(array_agg(x)) FROM mergeappend_test r, pg_sleep(0)
order by 1,2;

-- check that EXPLAIN ANALYZE works on MergeAppend, too.
explain analyze select a, b, array_dims(array_agg(x)) from mergeappend_test r group by a, b
union all
select null, null, array_dims(array_agg(x)) FROM mergeappend_test r
order by 1,2;

CREATE TABLE t1(c1 int, c2 int, c3 int);
CREATE TABLE t2(c1 int, c2 int, c3 int);
INSERT INTO t1 SELECT i, i ,i + 1 FROM generate_series(1,10) i;
INSERT INTO t2 SELECT i, i ,i + 1 FROM generate_series(1,10) i;
SET enable_hashagg = off;
with tcte(c1, c2, c3) as (
	SELECT c1, sum(c2) as c2, c3 FROM t1 WHERE c3 > 0 GROUP BY c1, c3
	UNION ALL
	SELECT c1, sum(c2) as c2, c3 FROM t2 WHERE c3 < 0 GROUP BY c1, c3
)
SELECT * FROM tcte WHERE c3 = 1;

