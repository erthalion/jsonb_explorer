MODULE_big = jsonb_explorer
OBJS = jsonb_explorer.o jsonb_explorer_utils.o

DATA = jsonb_explorer--1.0.sql
EXTENSION = jsonb_explorer

REGRESS = jsonb_explorer

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
