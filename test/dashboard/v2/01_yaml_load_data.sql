-- 01_yaml_load_data.sql
-- Load projects.yaml into tables
--
-- Run: duckdb_yaml/build/release/duckdb /tmp/dashboard.duckdb -f 01_yaml_load_data.sql

-- Extract configuration
CREATE OR REPLACE TABLE config AS
SELECT default_owner
FROM '../../projects.yaml';

-- Flatten projects array into a relational table
CREATE OR REPLACE TABLE projects AS
SELECT UNNEST(projects, recursive := true)
FROM '../../projects.yaml';

-- Summary
SELECT category, count(*) as n
FROM projects
GROUP BY category
ORDER BY category;
