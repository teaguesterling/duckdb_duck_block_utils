-- 01_yaml_load_data.sql
-- Load projects.yaml into tables
-- Run with: duckdb_yaml/build/release/duckdb /tmp/dashboard.duckdb -f 01_yaml_load_data.sql

CREATE OR REPLACE TABLE config AS
SELECT default_owner FROM '../projects.yaml';

CREATE OR REPLACE TABLE projects AS
SELECT UNNEST(projects, recursive:=true) FROM '../projects.yaml';

-- Show what we loaded
SELECT category, count(*) as n FROM projects GROUP BY category ORDER BY category;
