# PostgreSQL RAG Extension Makefile

MODULE_big = pg_rag
OBJS = \
    src/pg_rag.o \
    src/config.o \
    src/kb.o \
    src/chunk.o \
    src/http.o \
    src/embedding.o \
    src/llm.o \
    src/retrieve.o \
    src/utils.o

EXTENSION = pg_rag
DATA = sql/pg_rag--0.1.0.sql

# Regression tests
REGRESS = pg_rag_test

# PostgreSQL build flags
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Additional C flags
override CFLAGS += -I$(libpq_srcdir) -std=c99

# Install target
install: install-headers

install-headers:
	@mkdir -p $(includedir_server)/extension/pg_rag
	@cp src/*.h $(includedir_server)/extension/pg_rag/
