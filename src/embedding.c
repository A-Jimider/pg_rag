/*-------------------------------------------------------------------------
 * embedding.c
 *     OpenAI/Alibaba Embedding API integration with JSON parsing
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "utils/json.h"
#include "utils/jsonb.h"
#include <string.h>
#include <ctype.h>

/*
 * parse_embedding_from_json - Simple parser to extract embedding array from JSON
 * Looks for: "embedding": [0.1, 0.2, ...]
 */
static bool
parse_embedding_from_json(const char *json, float4 *embedding)
{
    const char *p = json;
    const char *embedding_key = "\"embedding\"";
    int found = 0;
    int i;

    /* Find "embedding" key */
    p = strstr(p, embedding_key);
    if (!p)
        return false;
    p += strlen(embedding_key);

    /* Skip whitespace and colon */
    while (*p && (isspace((unsigned char) *p) || *p == ':'))
        p++;

    /* Expect opening bracket */
    if (*p != '[')
        return false;
    p++;

    /* Parse 1536 float values */
    for (i = 0; i < EMBEDDING_DIM && *p; i++)
    {
        /* Skip whitespace and comma */
        while (*p && (isspace((unsigned char) *p) || *p == ','))
            p++;

        if (*p == ']')
            break;

        /* Parse float */
        embedding[i] = (float4) strtod(p, (char **) &p);
        found++;
    }

    return (found == EMBEDDING_DIM);
}

/*
 * rag_embedding_get - Get embedding for text from API
 */
bool
rag_embedding_get(const char *text, float4 *embedding, int32 *token_count)
{
    HttpResponse *resp = NULL;
    StringInfo  json_body;
    RagConfig   config;
    const char *headers[3];
    char        auth_header[512];
    int         i;
    bool        success = false;
    float4      api_embedding[EMBEDDING_DIM];

    /* Get config */
    rag_config_get(&config);

    /* Estimate token count */
    *token_count = strlen(text) / CHARS_PER_TOKEN;
    if (*token_count < 1) *token_count = 1;

    /* Try to call real API if key is set */
    if (rag_embedding_api_key != NULL && rag_embedding_api_key[0] != '\0')
    {
        /* Build request body using StringInfo */
        json_body = makeStringInfo();
        appendStringInfo(json_body, "{\"model\": \"%s\", \"dimensions\": 1536, \"input\": \"", config.embedding_model);

        /* Simple escape - just replace quotes and backslashes */
        for (i = 0; text[i]; i++)
        {
            if (text[i] == '"' || text[i] == '\\')
                appendStringInfoChar(json_body, '\\');
            appendStringInfoChar(json_body, text[i]);
        }
        appendStringInfo(json_body, "\"}");

        /* Build headers */
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", rag_embedding_api_key);
        headers[0] = "Content-Type: application/json";
        headers[1] = auth_header;
        headers[2] = NULL;

        /* Make request */
        resp = rag_http_post(rag_embedding_api_url ? rag_embedding_api_url : DEFAULT_EMBEDDING_URL,
                             headers, json_body->data, config.timeout_ms);

        pfree(json_body->data);
        pfree(json_body);

        if (resp->status_code == 200 && resp->body && resp->body[0] != '\0')
        {
            /* Parse the JSON response */
            if (parse_embedding_from_json(resp->body, api_embedding))
            {
                /* Copy parsed embedding to output */
                for (i = 0; i < EMBEDDING_DIM; i++)
                    embedding[i] = api_embedding[i];
                success = true;
            }
            else
            {
                elog(WARNING, "Failed to parse embedding response, using hash-based fallback");
                /* Fallback to hash-based - already done below */
            }
        }
        else
        {
            if (resp->status_code != 200)
                elog(WARNING, "Embedding API returned status %ld: %s",
                     resp->status_code, (resp->body && resp->body[0]) ? resp->body : "no body");
            else if (resp->error)
                elog(WARNING, "Embedding API error: %s", resp->error);
            /* Fallback to hash-based embedding - will be done below */
        }

        if (resp)
            rag_http_response_free(resp);
    }

    /* Generate hash-based fallback embedding if API not available or failed */
    if (!success)
    {
        unsigned int hash = 0;
        for (i = 0; text[i]; i++)
            hash = hash * 31 + text[i];

        for (i = 0; i < EMBEDDING_DIM; i++)
        {
            hash = hash * 1103515245 + 12345;
            embedding[i] = ((float)(hash % 1000) / 500.0) - 1.0;
        }
        success = true;
    }

    return success;
}
