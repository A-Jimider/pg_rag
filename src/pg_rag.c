/*-------------------------------------------------------------------------
 * pg_rag.c
 *     PostgreSQL RAG Extension - Main entry point
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "access/htup_details.h"
#include "catalog/namespace.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/json.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"

PG_MODULE_MAGIC;

/*
 * GUC variables for sensitive configuration
 */
char *rag_embedding_api_key = NULL;
char *rag_llm_api_key = NULL;
char *rag_embedding_api_url = NULL;
char *rag_llm_api_url = NULL;
char *rag_embedding_model = NULL;
char *rag_llm_model = NULL;
double rag_llm_temperature = DEFAULT_LLM_TEMPERATURE;

/*
 * _PG_init - Extension initialization
 */
void
_PG_init(void)
{
    /* Define GUC variables for sensitive configs */
    DefineCustomStringVariable("rag.embedding_api_key",
                               "OpenAI API key for embeddings",
                               NULL,
                               &rag_embedding_api_key,
                               "",
                               PGC_USERSET,
                               GUC_SUPERUSER_ONLY,
                               NULL, NULL, NULL);

    DefineCustomStringVariable("rag.llm_api_key",
                               "OpenAI API key for LLM",
                               NULL,
                               &rag_llm_api_key,
                               "",
                               PGC_USERSET,
                               GUC_SUPERUSER_ONLY,
                               NULL, NULL, NULL);

    DefineCustomStringVariable("rag.embedding_api_url",
                               "OpenAI embedding API URL",
                               NULL,
                               &rag_embedding_api_url,
                               DEFAULT_EMBEDDING_URL,
                               PGC_USERSET,
                               0,
                               NULL, NULL, NULL);

    DefineCustomStringVariable("rag.llm_api_url",
                               "OpenAI chat completions API URL",
                               NULL,
                               &rag_llm_api_url,
                               DEFAULT_LLM_URL,
                               PGC_USERSET,
                               0,
                               NULL, NULL, NULL);

    DefineCustomStringVariable("rag.embedding_model",
                               "Embedding model name",
                               NULL,
                               &rag_embedding_model,
                               DEFAULT_EMBEDDING_MODEL,
                               PGC_USERSET,
                               0,
                               NULL, NULL, NULL);

    DefineCustomStringVariable("rag.llm_model",
                               "LLM model name",
                               NULL,
                               &rag_llm_model,
                               DEFAULT_LLM_MODEL,
                               PGC_USERSET,
                               0,
                               NULL, NULL, NULL);

    DefineCustomRealVariable("rag.llm_temperature",
                             "LLM temperature parameter (0.0-2.0)",
                             NULL,
                             &rag_llm_temperature,
                             DEFAULT_LLM_TEMPERATURE,
                             0.0,
                             2.0,
                             PGC_USERSET,
                             0,
                             NULL, NULL, NULL);

    /* Also check environment variables */
    if (getenv("PG_RAG_EMBEDDING_API_KEY"))
    {
        if (rag_embedding_api_key == NULL || rag_embedding_api_key[0] == '\0')
            rag_embedding_api_key = pstrdup(getenv("PG_RAG_EMBEDDING_API_KEY"));
    }

    if (getenv("PG_RAG_LLM_API_KEY"))
    {
        if (rag_llm_api_key == NULL || rag_llm_api_key[0] == '\0')
            rag_llm_api_key = pstrdup(getenv("PG_RAG_LLM_API_KEY"));
    }

    /* Initialize config module */
    rag_config_init();
}

/*
 * rag_set_config - Set configuration value in rag.config table
 */
PG_FUNCTION_INFO_V1(rag_set_config);
Datum
rag_set_config(PG_FUNCTION_ARGS)
{
    text       *key_text = PG_GETARG_TEXT_PP(0);
    text       *value_text = PG_GETARG_TEXT_PP(1);
    char       *key = text_to_cstring(key_text);
    char       *value = text_to_cstring(value_text);
    int         ret;

    /* Connect to SPI */
    if (SPI_connect() != SPI_OK_CONNECT)
        elog(ERROR, "SPI_connect failed");

    /* UPSERT into rag.config */
    ret = SPI_execute_with_args(
        "INSERT INTO rag.config (key, value, updated_at) "
        "VALUES ($1, $2, now()) "
        "ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, updated_at = now()",
        2,
        (Oid[]){TEXTOID, TEXTOID},
        (Datum[]){CStringGetTextDatum(key), CStringGetTextDatum(value)},
        NULL,
        false,
        0
    );

    if (ret != SPI_OK_INSERT && ret != SPI_OK_UTILITY)
        elog(ERROR, "Failed to set config: %s", SPI_result_code_string(ret));

    SPI_finish();

    PG_RETURN_VOID();
}

/*
 * rag_get_config - Get configuration value from rag.config table
 */
PG_FUNCTION_INFO_V1(rag_get_config);
Datum
rag_get_config(PG_FUNCTION_ARGS)
{
    text       *key_text = PG_GETARG_TEXT_PP(0);
    char       *key = text_to_cstring(key_text);
    char       *value = NULL;
    int         ret;

    /* Connect to SPI */
    if (SPI_connect() != SPI_OK_CONNECT)
        elog(ERROR, "SPI_connect failed");

    ret = SPI_execute_with_args(
        "SELECT value FROM rag.config WHERE key = $1",
        1,
        (Oid[]){TEXTOID},
        (Datum[]){CStringGetTextDatum(key)},
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
            value = text_to_cstring(DatumGetTextPP(datum));
    }

    SPI_finish();

    if (value == NULL)
        PG_RETURN_NULL();

    PG_RETURN_TEXT_P(cstring_to_text(value));
}

/*
 * rag_insert - Insert document with automatic chunking and embedding
 */
PG_FUNCTION_INFO_V1(rag_insert);
Datum
rag_insert(PG_FUNCTION_ARGS)
{
    text       *kb_text = PG_GETARG_TEXT_PP(0);
    text       *content_text = PG_GETARG_TEXT_PP(1);
    char       *kb_name = text_to_cstring(kb_text);
    char       *content = text_to_cstring(content_text);
    RagConfig   config;
    Chunk      *chunks = NULL;
    int32       num_chunks = 0;
    int32       inserted_count = 0;
    int         i;
    Datum       metadata_datum;
    bool        metadata_isnull = true;

    /* Get optional parameters */
    if (PG_NARGS() > 2 && !PG_ARGISNULL(2))
    {
        metadata_datum = PG_GETARG_DATUM(2);
        metadata_isnull = false;
    }

    /* Get config */
    rag_config_get(&config);

    /* Chunk the content */
    chunks = rag_chunk_text(content, &num_chunks, config.chunk_size, config.chunk_overlap);
    if (num_chunks == 0)
        PG_RETURN_INT32(0);

    /* Connect to SPI */
    if (SPI_connect() != SPI_OK_CONNECT)
        elog(ERROR, "SPI_connect failed");

    /* Check for duplicate content before processing */
    {
        int ret_check;
        Datum check_values[2];
        Oid argtypes[2] = { TEXTOID, TEXTOID };
        char nulls[2] = { ' ', ' ' };

        check_values[0] = CStringGetTextDatum(kb_name);
        check_values[1] = CStringGetTextDatum(content);

        ret_check = SPI_execute_with_args(
            "SELECT 1 FROM rag.documents d "
            "JOIN rag.knowledge_bases k ON d.kb_id = k.id "
            "WHERE k.name = $1 AND d.content = $2 "
            "LIMIT 1",
            2, argtypes, check_values, nulls, true, 1);

        /* Clean up datums */
        pfree(DatumGetPointer(check_values[0]));
        pfree(DatumGetPointer(check_values[1]));

        if (ret_check == SPI_OK_SELECT && SPI_processed > 0)
        {
            /* Duplicate found, skip insertion */
            SPI_finish();
            rag_chunks_free(chunks, num_chunks);
            elog(NOTICE, "Document already exists in knowledge base '%s', skipping", kb_name);
            PG_RETURN_INT32(0);
        }
    }

    /* Insert each chunk with embedding */
    for (i = 0; i < num_chunks; i++)
    {
        float4      embedding[EMBEDDING_DIM];
        int32       token_count;
        Datum       values[6];
        int         ret;

        /* Generate embedding */
        if (!rag_embedding_get(chunks[i].content, embedding, &token_count))
        {
            elog(WARNING, "Failed to get embedding for chunk %d", i);
        }

        /* Build vector text representation for embedding */
        {
            StringInfo  vectortext = makeStringInfo();
            int         j;
            appendStringInfoChar(vectortext, '[');
            for (j = 0; j < EMBEDDING_DIM; j++)
            {
                if (j > 0) appendStringInfoChar(vectortext, ',');
                appendStringInfo(vectortext, "%f", embedding[j]);
            }
            appendStringInfoChar(vectortext, ']');

            if (metadata_isnull)
            {
                /* Without metadata: $1=kb, $2=content, $3=chunk_idx, $4=tokens, $5=embedding */
                values[0] = CStringGetTextDatum(kb_name);
                values[1] = CStringGetTextDatum(chunks[i].content);
                values[2] = Int32GetDatum(chunks[i].chunk_index);
                values[3] = Int32GetDatum(token_count > 0 ? token_count : chunks[i].token_estimate);
                values[4] = CStringGetTextDatum(vectortext->data);
            }
            else
            {
                /* With metadata: $1=kb, $2=content, $3=chunk_idx, $4=tokens, $5=embedding, $6=metadata */
                values[0] = CStringGetTextDatum(kb_name);
                values[1] = CStringGetTextDatum(chunks[i].content);
                values[2] = Int32GetDatum(chunks[i].chunk_index);
                values[3] = Int32GetDatum(token_count > 0 ? token_count : chunks[i].token_estimate);
                values[4] = CStringGetTextDatum(vectortext->data);
                values[5] = metadata_datum;
            }

            pfree(vectortext->data);
            pfree(vectortext);
        }

        if (metadata_isnull)
        {
            ret = SPI_execute_with_args(
                "INSERT INTO rag.documents (kb_id, content, chunk_index, token_estimate, embedding) "
                "SELECT id, $2, $3, $4, $5::vector FROM rag.knowledge_bases WHERE name = $1",
                5,
                (Oid[]){TEXTOID, TEXTOID, INT4OID, INT4OID, TEXTOID},
                values,
                NULL,
                false,
                0
            );
        }
        else
        {
            ret = SPI_execute_with_args(
                "INSERT INTO rag.documents (kb_id, content, chunk_index, token_estimate, embedding, metadata) "
                "SELECT id, $2, $3, $4, $5::vector, $6 FROM rag.knowledge_bases WHERE name = $1",
                6,
                (Oid[]){TEXTOID, TEXTOID, INT4OID, INT4OID, TEXTOID, JSONBOID},
                values,
                NULL,
                false,
                0
            );
        }

        if (ret == SPI_OK_INSERT)
            inserted_count++;
        else
            elog(WARNING, "Failed to insert chunk %d: %s", i, SPI_result_code_string(ret));
    }

    SPI_finish();
    rag_chunks_free(chunks, num_chunks);

    PG_RETURN_INT32(inserted_count);
}

/*
 * rag_query - RAG query (retrieve + generate)
 */
PG_FUNCTION_INFO_V1(rag_query);
Datum
rag_query(PG_FUNCTION_ARGS)
{
    text       *kb_text = PG_GETARG_TEXT_PP(0);
    text       *query_text = PG_GETARG_TEXT_PP(1);
    int32       top_k = PG_GETARG_INT32(2);
    char       *kb_name = text_to_cstring(kb_text);
    char       *query = text_to_cstring(query_text);
    RagConfig   config;
    float4      query_embedding[EMBEDDING_DIM];
    int32       token_count;
    int32       num_retrieved = 0;
    char       *llm_response = NULL;
    HeapTuple   tuple;
    TupleDesc   tupdesc;
    Datum       values[4];
    bool        nulls[4] = {false, false, false, false};
    int64       start_time;
    RetrievedChunk *retrieved = NULL;
    int32       llm_latency = 0;
    MemoryContext oldcontext;

    elog(DEBUG1, "rag_query: starting");

    /* Switch to upper memory context for cross-call safety */
    oldcontext = MemoryContextSwitchTo(TopTransactionContext);

    /* Get result type tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        elog(ERROR, "rag_query: return type must be a composite type");

    /* Bless the tuple descriptor */
    tupdesc = BlessTupleDesc(tupdesc);

    /* Get config */
    rag_config_get(&config);

    /* Start timing */
    start_time = rag_get_current_time_ms();

    /* Embed query */
    if (!rag_embedding_get(query, query_embedding, &token_count))
        elog(ERROR, "Failed to embed query");

    /* Connect to SPI for retrieval */
    if (SPI_connect() != SPI_OK_CONNECT)
        elog(ERROR, "SPI_connect failed");

    /* Retrieve similar chunks */
    retrieved = rag_retrieve(kb_name, query_embedding, top_k, &num_retrieved);

    if (num_retrieved == 0)
    {
        SPI_finish();
        /* No results found */
        values[0] = CStringGetTextDatum("No relevant documents found.");
        values[1] = JsonbPGetDatum(DatumGetJsonbP(DirectFunctionCall1(jsonb_in, CStringGetDatum("[]"))));
        values[2] = JsonbPGetDatum(DatumGetJsonbP(DirectFunctionCall1(jsonb_in, CStringGetDatum("{}"))));
        values[3] = Int32GetDatum((int32)(rag_get_current_time_ms() - start_time));

        tuple = heap_form_tuple(tupdesc, values, nulls);
        /* Switch back to original memory context before returning */
        MemoryContextSwitchTo(oldcontext);
        PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
    }

    /* Disconnect SPI before calling LLM */
    SPI_finish();

    /* Call LLM with full context from all retrieved chunks */
    {
        int32 j;
        StringInfo full_prompt = makeStringInfo();

        /* Build system prompt with all retrieved chunks */
        appendStringInfo(full_prompt, "%s\n\n", config.system_prompt);
        appendStringInfo(full_prompt, "基于以下上下文回答问题:\n\n");

        for (j = 0; j < num_retrieved; j++)
        {
            appendStringInfo(full_prompt, "[文档 %d]\n%s\n\n", j + 1, retrieved[j].content);
        }

        appendStringInfo(full_prompt, "问题: %s\n\n", query);
        appendStringInfo(full_prompt, "请根据上述上下文回答:");

        llm_response = rag_llm_generate(full_prompt->data, &llm_latency);

        /* Don't manually free - let memory context handle it */
        /* pfree(full_prompt->data); */
        /* pfree(full_prompt); */
    }

    if (!llm_response)
        llm_response = pstrdup("LLM response placeholder - set rag.llm_api_key for real responses");

    /* Build result */
    values[0] = CStringGetTextDatum(llm_response);
    values[1] = JsonbPGetDatum(DatumGetJsonbP(DirectFunctionCall1(jsonb_in, CStringGetDatum("[]"))));
    values[2] = JsonbPGetDatum(DatumGetJsonbP(DirectFunctionCall1(jsonb_in, CStringGetDatum("{}"))));
    values[3] = Int32GetDatum((int32)(rag_get_current_time_ms() - start_time));

    tuple = heap_form_tuple(tupdesc, values, nulls);

    /* Cleanup retrieved chunks - let llm_response be freed by context cleanup */
    if (retrieved)
        rag_retrieved_chunks_free(retrieved, num_retrieved);

    /* Switch back to original memory context before returning */
    MemoryContextSwitchTo(oldcontext);

    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

/*
 * rag_explain - Debug/explain RAG query
 */
PG_FUNCTION_INFO_V1(rag_explain);
Datum
rag_explain(PG_FUNCTION_ARGS)
{
    text       *kb_text = PG_GETARG_TEXT_PP(0);
    text       *query_text = PG_GETARG_TEXT_PP(1);
    int32       top_k = PG_GETARG_INT32(2);
    char       *kb_name = text_to_cstring(kb_text);
    char       *query = text_to_cstring(query_text);
    RagConfig   config;
    float4      query_embedding[EMBEDDING_DIM];
    int32       token_count;
    HeapTuple   tuple;
    TupleDesc   tupdesc;
    Datum       values[5];
    bool        nulls[5] = {false, false, false, false, false};
    int64       start_time, embedding_time, retrieve_time, prompt_time;
    MemoryContext oldcontext;
    StringInfo  embedding_str;
    StringInfo  chunks_json;
    StringInfo  prompt_si;
    int         i, j;
    int32       num_retrieved = 0;
    RetrievedChunk *retrieved = NULL;
    int32       total_tokens = 0;

    /* Switch to upper memory context */
    oldcontext = MemoryContextSwitchTo(TopTransactionContext);

    /* Get result type tuple descriptor */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
    {
        MemoryContextSwitchTo(oldcontext);
        elog(ERROR, "rag_explain: return type must be a composite type");
    }

    /* Bless the tuple descriptor */
    tupdesc = BlessTupleDesc(tupdesc);

    /* Get config */
    rag_config_get(&config);

    /* Start timing */
    start_time = rag_get_current_time_ms();

    /* Embed query */
    if (!rag_embedding_get(query, query_embedding, &token_count))
    {
        MemoryContextSwitchTo(oldcontext);
        elog(ERROR, "Failed to embed query");
    }
    embedding_time = rag_get_current_time_ms();

    /* Build query embedding string for return (as vector text representation) */
    embedding_str = makeStringInfo();
    appendStringInfoChar(embedding_str, '[');
    for (i = 0; i < EMBEDDING_DIM && i < 10; i++)
    {
        if (i > 0) appendStringInfoChar(embedding_str, ',');
        appendStringInfo(embedding_str, "%f", query_embedding[i]);
    }
    appendStringInfo(embedding_str, "...]");
    /* Return as vector type - use DirectFunctionCall to convert text to vector */
    values[0] = CStringGetTextDatum(embedding_str->data);

    /* Connect to SPI for retrieval */
    if (SPI_connect() != SPI_OK_CONNECT)
    {
        MemoryContextSwitchTo(oldcontext);
        elog(ERROR, "SPI_connect failed");
    }

    /* Retrieve similar chunks */
    retrieved = rag_retrieve(kb_name, query_embedding, top_k, &num_retrieved);
    retrieve_time = rag_get_current_time_ms();

    /* Build prompt and chunks JSON */
    prompt_si = makeStringInfo();
    chunks_json = makeStringInfo();

    if (num_retrieved > 0)
    {
        /* Build system prompt */
        appendStringInfo(prompt_si, "%s\n\n", config.system_prompt);
        appendStringInfo(prompt_si, "基于以下上下文回答问题:\n\n");

        /* Build chunks JSON array */
        appendStringInfo(chunks_json, "[");

        for (i = 0; i < num_retrieved; i++)
        {
            /* Add to prompt */
            appendStringInfo(prompt_si, "[文档 %d]\n%s\n\n", i + 1, retrieved[i].content);

            /* Add to JSON */
            if (i > 0) appendStringInfoChar(chunks_json, ',');
            appendStringInfo(chunks_json, "{");
            appendStringInfo(chunks_json, "\"chunk_index\":%d,", retrieved[i].chunk_index);
            appendStringInfo(chunks_json, "\"score\":%f,", retrieved[i].score);
            appendStringInfo(chunks_json, "\"token_estimate\":%d,",
                           (int)(strlen(retrieved[i].content) / CHARS_PER_TOKEN));
            appendStringInfo(chunks_json, "\"content\":\"");
            /* Add to JSON with proper escaping */
            for (j = 0; retrieved[i].content[j]; j++)
            {
                char c = retrieved[i].content[j];
                /* Escape quotes and backslashes */
                if (c == '"' || c == '\\')
                {
                    appendStringInfoChar(chunks_json, '\\');
                    appendStringInfoChar(chunks_json, c);
                }
                /* Escape control characters */
                else if (c == '\n')
                    appendStringInfo(chunks_json, "\\n");
                else if (c == '\r')
                    appendStringInfo(chunks_json, "\\r");
                else if (c == '\t')
                    appendStringInfo(chunks_json, "\\t");
                /* Keep all other characters including Chinese */
                else
                    appendStringInfoChar(chunks_json, c);
            }
            appendStringInfo(chunks_json, "\"}");

            total_tokens += strlen(retrieved[i].content) / CHARS_PER_TOKEN;
        }
        appendStringInfo(chunks_json, "]");

        /* Finish prompt */
        appendStringInfo(prompt_si, "问题: %s\n\n", query);
        appendStringInfo(prompt_si, "请根据上述上下文回答:");

        rag_retrieved_chunks_free(retrieved, num_retrieved);
    }
    else
    {
        appendStringInfo(chunks_json, "[]");
        appendStringInfo(prompt_si, "No relevant documents found.");
    }

    prompt_time = rag_get_current_time_ms();

    values[1] = JsonbPGetDatum(DatumGetJsonbP(DirectFunctionCall1(jsonb_in, CStringGetDatum(chunks_json->data))));
    values[2] = CStringGetTextDatum(prompt_si->data);
    values[3] = Int32GetDatum(total_tokens);
    values[4] = Int32GetDatum((int32)(prompt_time - start_time));

    SPI_finish();

    tuple = heap_form_tuple(tupdesc, values, nulls);

    /* Don't manually free - let PostgreSQL clean up memory context */
    /* Memory cleanup handled by TopTransactionContext */

    MemoryContextSwitchTo(oldcontext);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}
