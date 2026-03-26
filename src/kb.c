/*-------------------------------------------------------------------------
 * kb.c
 *     Knowledge base management
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "executor/spi.h"
#include "utils/builtins.h"

/*
 * rag_kb_get_id - Get kb_id from knowledge_base name
 * Returns -1 if not found
 * Note: Caller must be in SPI context
 */
int32
rag_kb_get_id(const char *name)
{
    int32       kb_id = -1;
    int         ret;

    /* Caller must be in SPI context */
    ret = SPI_execute_with_args(
        "SELECT id FROM rag.knowledge_bases WHERE name = $1",
        1,
        (Oid[]){TEXTOID},
        (Datum[]){CStringGetTextDatum(name)},
        NULL,
        true,
        1
    );

    if (ret == SPI_OK_SELECT && SPI_processed > 0)
    {
        bool isnull;
        Datum datum = SPI_getbinval(SPI_tuptable->vals[0],
                                     SPI_tuptable->tupdesc, 1, &isnull);
        if (!isnull)
            kb_id = DatumGetInt32(datum);
    }

    return kb_id;
}

/*
 * rag_kb_create - Create a new knowledge base in knowledge_bases table
 * Returns the new kb_id
 * Note: Caller must be in SPI context
 */
int32
rag_kb_create(const char *name, int32 embedding_dim)
{
    int32       kb_id = -1;
    int         ret;

    ret = SPI_execute_with_args(
        "INSERT INTO rag.knowledge_bases (name, embedding_dim) VALUES ($1, $2) RETURNING id",
        2,
        (Oid[]){TEXTOID, INT4OID},
        (Datum[]){CStringGetTextDatum(name), Int32GetDatum(embedding_dim)},
        NULL,
        false,
        1
    );

    if (ret == SPI_OK_INSERT_RETURNING && SPI_processed == 1)
    {
        bool isnull;
        Datum datum = SPI_getbinval(SPI_tuptable->vals[0],
                                     SPI_tuptable->tupdesc, 1, &isnull);
        if (!isnull)
            kb_id = DatumGetInt32(datum);
    }
    else
    {
        elog(ERROR, "Failed to create knowledge base '%s': %s",
             name, SPI_result_code_string(ret));
    }

    return kb_id;
}

/*
 * rag_create_kb - SQL function to create knowledge base
 */
PG_FUNCTION_INFO_V1(rag_create_kb);
Datum
rag_create_kb(PG_FUNCTION_ARGS)
{
    text       *name_text = PG_GETARG_TEXT_PP(0);
    int32       embedding_dim = PG_GETARG_INT32(1);
    char       *name = text_to_cstring(name_text);
    int32       kb_id;

    if (SPI_connect() != SPI_OK_CONNECT)
        elog(ERROR, "SPI_connect failed");

    kb_id = rag_kb_create(name, embedding_dim);

    SPI_finish();

    PG_RETURN_INT32(kb_id);
}
