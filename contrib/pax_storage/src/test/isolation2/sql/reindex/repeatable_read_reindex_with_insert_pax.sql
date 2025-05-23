DROP TABLE IF EXISTS reindex_serialize_tab_pax;

CREATE TABLE reindex_serialize_tab_pax (a INT, b text, c date, d numeric, e bigint, f char(10), g float) distributed by (a);
insert into reindex_serialize_tab_pax select i, 'abc'||i, now(),i*100.43, i*-187, 'a'|| i*-1, i*2.23 from generate_series(1,10) i;
create index idxa_reindex_serialize_tab_pax on reindex_serialize_tab_pax(a);
create index idxb_reindex_serialize_tab_pax on reindex_serialize_tab_pax(b);
create index idxc_reindex_serialize_tab_pax on reindex_serialize_tab_pax(c);
create index idxd_reindex_serialize_tab_pax on reindex_serialize_tab_pax(d);
create index idxe_reindex_serialize_tab_pax on reindex_serialize_tab_pax(e);
create index idxf_reindex_serialize_tab_pax on reindex_serialize_tab_pax(f);
create index idxg_reindex_serialize_tab_pax on reindex_serialize_tab_pax(g);
-- start_ignore
SET gp_create_table_random_default_distribution=off;
-- end_ignore
1: BEGIN;
1: SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
1: select 'dummy select to establish snapshot';
2: BEGIN;
2: insert into reindex_serialize_tab_pax values(99,'green',now(),10,15.10);
2: COMMIT;
1: select a,b,d,e,f,g from reindex_serialize_tab_pax order by 1;
1: select a,b,d,e,f,g from reindex_serialize_tab_pax where a = 99;
1: reindex table reindex_serialize_tab_pax;
1: COMMIT;
4: select a,b,d,e,f,g from reindex_serialize_tab_pax where a = 99;
3: set enable_seqscan=false;
3: set enable_indexscan=true;
3: select a,b,d,e,f,g from reindex_serialize_tab_pax where a = 99;
