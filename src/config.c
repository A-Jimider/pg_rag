/*-------------------------------------------------------------------------
 * config.c
 *     Configuration management - simplified version
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "utils/guc.h"

/*
 * rag_config_init - Initialize config module
 */
void
rag_config_init(void)
{
    /* Nothing special needed here, GUC vars are set in _PG_init */
}

/*
 * rag_config_get - Get all configs into a struct
 * Uses CurrentMemoryContext for string allocations
 */
void
rag_config_get(RagConfig *config)
{
    config->chunk_size = DEFAULT_CHUNK_SIZE;
    config->chunk_overlap = DEFAULT_CHUNK_OVERLAP;
    config->timeout_ms = DEFAULT_TIMEOUT_MS;
    config->llm_temperature = rag_llm_temperature;

    /* Allocate strings in CurrentMemoryContext */
    config->system_prompt = MemoryContextStrdup(CurrentMemoryContext, "You are a helpful assistant.");

    /* Use GUC variables if set, otherwise use defaults */
    if (rag_embedding_model && rag_embedding_model[0] != '\0')
        config->embedding_model = MemoryContextStrdup(CurrentMemoryContext, rag_embedding_model);
    else
        config->embedding_model = MemoryContextStrdup(CurrentMemoryContext, DEFAULT_EMBEDDING_MODEL);

    if (rag_llm_model && rag_llm_model[0] != '\0')
        config->llm_model = MemoryContextStrdup(CurrentMemoryContext, rag_llm_model);
    else
        config->llm_model = MemoryContextStrdup(CurrentMemoryContext, DEFAULT_LLM_MODEL);

    config->on_error = MemoryContextStrdup(CurrentMemoryContext, ON_ERROR_SKIP);
}

/*
 * rag_config_get_int - Get integer config value
 */
int32
rag_config_get_int(const char *key, int32 default_val)
{
    return default_val;
}

/*
 * rag_config_get_str - Get string config value
 */
char *
rag_config_get_str(const char *key, const char *default_val)
{
    return pstrdup(default_val);
}
