-- 01_yaml_load_data.sql
-- Load projects.yaml into tables
--
-- Run: duckdb_yaml/build/release/duckdb /tmp/dashboard.duckdb -f 01_yaml_load_data.sql

LOAD yaml;

CREATE yaml_data AS FROM '../../projects.yaml';

CREATE OR REPLACE TABLE config AS
SELECT default_owner FROM yaml_data;

CREATE OR REPLACE TABLE projects AS
SELECT UNNEST(projects, recursive := true)
FROM yaml_data;

SELECT category, count(*) AS n
FROM projects
GROUP BY category
ORDER BY category;
