-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION jsonb_explorer" to load this file. \quit


CREATE FUNCTION jsonb_tree(jsonb)
RETURNS text
AS 'MODULE_PATHNAME', 'jsonb_tree'
LANGUAGE C STRICT;

CREATE FUNCTION jsonb_paths(jsonb)
RETURNS text[][]
AS 'MODULE_PATHNAME', 'jsonb_paths'
LANGUAGE C STRICT;
