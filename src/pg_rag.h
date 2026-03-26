/*-------------------------------------------------------------------------
 * pg_rag.h
 *     PostgreSQL RAG Extension - Main header file
 *-------------------------------------------------------------------------
 */
#ifndef PG_RAG_H
#define PG_RAG_H

#include "postgres.h"
#include "fmgr.h"
#include "utils/jsonb.h"

/* Extension version */
#define PG_RAG_VERSION "0.1.0"

/* Default values */
#define DEFAULT_CHUNK_SIZE      1200
#define DEFAULT_CHUNK_OVERLAP   200
#define DEFAULT_TIMEOUT_MS      30000
#define DEFAULT_TOP_K           5
#define DEFAULT_LLM_TEMPERATURE 1.0

/* Error handling strategies */
#define ON_ERROR_SKIP   "skip"
#define ON_ERROR_FAIL   "fail"

/* OpenAI defaults */
#define DEFAULT_EMBEDDING_MODEL "text-embedding-v4"
#define DEFAULT_LLM_MODEL       "kimi-k2.5"
#define DEFAULT_EMBEDDING_URL   "https://dashscope.aliyuncs.com/compatible-mode/v1/embeddings"
#define DEFAULT_LLM_URL         "https://api.moonshot.cn/v1/chat/completions"

/* Embedding dimension */
#define EMBEDDING_DIM           1536

/* Token estimation (chars per token for OpenAI) */
#define CHARS_PER_TOKEN         4

/*
 * Result structures
 */
typedef struct RagResult
{
    char       *answer;
    JsonbValue *sources;
    JsonbValue *debug;
    int32       latency_ms;
} RagResult;

typedef struct RagExplainResult
{
    float4      query_embedding[EMBEDDING_DIM];
    JsonbValue *retrieved_chunks;
    char       *prompt;
    int32       token_estimate;
    int32       latency_ms;
} RagExplainResult;

/*
 * Config management
 */
typedef struct RagConfig
{
    int32       chunk_size;
    int32       chunk_overlap;
    int32       timeout_ms;
    double      llm_temperature;
    char       *system_prompt;
    char       *embedding_model;
    char       *llm_model;
    char       *on_error;
} RagConfig;

/* GUC variables (extern declarations) */
extern char *rag_embedding_api_key;
extern char *rag_llm_api_key;
extern char *rag_embedding_api_url;
extern char *rag_llm_api_url;
extern char *rag_embedding_model;
extern char *rag_llm_model;
extern double rag_llm_temperature;

/*
 * Function declarations
 */

/* config.c */
extern void rag_config_init(void);
extern void rag_config_get(RagConfig *config);
extern int32 rag_config_get_int(const char *key, int32 default_val);
extern char *rag_config_get_str(const char *key, const char *default_val);

/* kb.c */
extern int32 rag_kb_get_id(const char *name);
extern int32 rag_kb_create(const char *name, int32 embedding_dim);

/* chunk.c */
typedef struct Chunk
{
    char       *content;
    int32       length;
    int32       chunk_index;
    int32       token_estimate;
} Chunk;

extern Chunk *rag_chunk_text(const char *text, int32 *num_chunks,
                              int32 chunk_size, int32 chunk_overlap);
extern void rag_chunks_free(Chunk *chunks, int32 num_chunks);

/* http.c */
typedef struct HttpResponse
{
    char       *body;
    long        status_code;
    char       *error;
} HttpResponse;

extern HttpResponse *rag_http_post(const char *url, const char *headers[],
                                    const char *body, int32 timeout_ms);
extern void rag_http_response_free(HttpResponse *resp);

/* embedding.c */
extern bool rag_embedding_get(const char *text, float4 *embedding,
                               int32 *token_count);

/* llm.c */
extern char *rag_llm_generate(const char *prompt, int32 *latency_ms);
extern char *rag_llm_build_prompt(const char *query, Chunk *chunks,
                                   int32 num_chunks, const char *system_prompt);

/* retrieve.c */
typedef struct RetrievedChunk
{
    char       *content;
    float4      score;
    int32       chunk_index;
    int32       token_estimate;
    JsonbValue *metadata;
} RetrievedChunk;

extern RetrievedChunk *rag_retrieve(const char *kb_name,
                                     const float4 *query_embedding,
                                     int32 top_k, int32 *num_results);
extern void rag_retrieved_chunks_free(RetrievedChunk *chunks, int32 num_chunks);

/* utils.c */
extern char *rag_jsonb_to_cstring(JsonbValue *jbv);
extern Jsonb *rag_cstring_to_jsonb(const char *str);
extern int64 rag_get_current_time_ms(void);

#endif /* PG_RAG_H */
