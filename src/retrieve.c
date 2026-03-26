/*-------------------------------------------------------------------------
 * retrieve.c
 *     pgvector vector search integration
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "lib/stringinfo.h"

/*
 * rag_retrieve - Retrieve similar chunks from knowledge base
 *
 * Note: Caller must be in SPI context
 */
RetrievedChunk *
rag_retrieve(const char *kb_name, const float4 *query_embedding,
             int32 top_k, int32 *num_results)
{
    RetrievedChunk *results = NULL;
    int32       kb_id;
    int         ret;
    int         i;

    /* Get kb_id - caller must be in SPI context */
    kb_id = rag_kb_get_id(kb_name);
    if (kb_id < 0)
    {
        elog(ERROR, "Knowledge base '%s' not found", kb_name);
        *num_results = 0;
        return NULL;
    }

    /* Build query embedding as string for SQL */
    {
        StringInfo  vectortext = makeStringInfo();
        int         j;
        appendStringInfoChar(vectortext, '[');
        for (j = 0; j < EMBEDDING_DIM; j++)
        {
            if (j > 0) appendStringInfoChar(vectortext, ',');
            appendStringInfo(vectortext, "%f", query_embedding[j]);
        }
        appendStringInfoChar(vectortext, ']');

        /* Execute vector similarity search */
        ret = SPI_execute_with_args(
            "SELECT d.content, d.chunk_index, d.metadata, "
            "       (d.embedding <=> $3::vector) as score "
            "FROM rag.documents d "
            "JOIN rag.knowledge_bases k ON d.kb_id = k.id "
            "WHERE k.name = $1 "
            "ORDER BY d.embedding <=> $3::vector "
            "LIMIT $2",
            3,
            (Oid[]){TEXTOID, INT4OID, TEXTOID},
            (Datum[]){CStringGetTextDatum(kb_name), Int32GetDatum(top_k), CStringGetTextDatum(vectortext->data)},
            NULL,
            false,
            top_k
        );

        pfree(vectortext->data);
        pfree(vectortext);
    }

    if (ret != SPI_OK_SELECT)
    {
        elog(ERROR, "Vector search failed: %s", SPI_result_code_string(ret));
        *num_results = 0;
        return NULL;
    }

    /* Copy results - allocate in TopTransactionContext so they persist after SPI_finish */
    *num_results = SPI_processed;
    if (*num_results > 0)
    {
        results = (RetrievedChunk *) MemoryContextAlloc(TopTransactionContext, sizeof(RetrievedChunk) * (*num_results));

        for (i = 0; i < *num_results; i++)
        {
            HeapTuple   tuple = SPI_tuptable->vals[i];
            TupleDesc   tupdesc = SPI_tuptable->tupdesc;
            bool        isnull;
            Datum       datum;

            /* content - copy to TopTransactionContext */
            datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            if (!isnull)
            {
                char *temp_content = TextDatumGetCString(datum);
                results[i].content = MemoryContextStrdup(TopTransactionContext, temp_content);
                pfree(temp_content);
            }
            else
                results[i].content = MemoryContextStrdup(TopTransactionContext, "");

            /* chunk_index */
            datum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
            if (!isnull)
                results[i].chunk_index = DatumGetInt32(datum);
            else
                results[i].chunk_index = -1;

            /* metadata */
            results[i].metadata = NULL;

            /* score - pgvector <=> operator returns float8 */
            datum = SPI_getbinval(tuple, tupdesc, 4, &isnull);
            if (!isnull)
                results[i].score = (float4) DatumGetFloat8(datum);
            else
                results[i].score = 0.0;

            /* token_estimate */
            results[i].token_estimate = results[i].content ? strlen(results[i].content) / CHARS_PER_TOKEN : 0;
        }
    }

    return results;
}

/*
 * rag_retrieved_chunks_free - Free retrieved chunks
 */
void
rag_retrieved_chunks_free(RetrievedChunk *chunks, int32 num_chunks)
{
    int32 i;

    if (chunks == NULL)
        return;

    for (i = 0; i < num_chunks; i++)
    {
        if (chunks[i].content)
            pfree(chunks[i].content);
    }

    pfree(chunks);
}
