/*-------------------------------------------------------------------------
 * llm.c
 *     OpenAI/Kimi Chat Completions API integration
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "utils/json.h"
#include "utils/jsonb.h"
#include <string.h>
#include <ctype.h>
#include <unistd.h>

/*
 * parse_content_from_json - Simple parser to extract content from LLM response
 * Looks for: "content": "..."
 * Returns NULL if not found
 */
static char *
parse_content_from_json(const char *json)
{
    const char *p = json;
    const char *content_key = "\"content\"";
    char       *result = NULL;
    const char *start;
    int         len = 0;
    int         j = 0;
    int         i;

    /* Find "content" key */
    p = strstr(p, content_key);
    if (!p)
        return NULL;
    p += strlen(content_key);

    /* Skip whitespace and colon */
    while (*p && (isspace((unsigned char) *p) || *p == ':'))
        p++;

    /* Expect opening quote */
    if (*p != '"')
        return NULL;
    p++;

    /* Find closing quote (handle escaped quotes) */
    start = p;
    while (*p)
    {
        if (*p == '\\' && p[1])
        {
            /* Skip escaped character */
            p += 2;
            continue;
        }
        if (*p == '"')
            break;
        p++;
    }

    len = p - start;
    if (len <= 0)
        return NULL;

    /* Copy and unescape */
    result = palloc(len + 1);
    for (i = 0; i < len; i++)
    {
        if (start[i] == '\\' && i + 1 < len)
        {
            i++;
            /* Handle common escape sequences */
            switch (start[i])
            {
                case 'n': result[j++] = '\n'; break;
                case 't': result[j++] = '\t'; break;
                case 'r': result[j++] = '\r'; break;
                case '\\': result[j++] = '\\'; break;
                case '"': result[j++] = '"'; break;
                default: result[j++] = start[i]; break;
            }
        }
        else
        {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';

    return result;
}

/*
 * rag_llm_generate - Generate response from LLM
 */
char *
rag_llm_generate(const char *prompt, int32 *latency_ms)
{
    HttpResponse *resp = NULL;
    StringInfo  json_body;
    RagConfig   config;
    const char *headers[3];
    char        auth_header[512];
    int64       start_time;
    char       *result = NULL;
    int         i;

    elog(DEBUG1, "rag_llm_generate: starting");

    /* Get config */
    rag_config_get(&config);

    /* Check API key */
    if (rag_llm_api_key == NULL || rag_llm_api_key[0] == '\0')
    {
        elog(WARNING, "LLM API key not set. Set via SET rag.llm_api_key = '...' or PG_RAG_LLM_API_KEY env var");
        return NULL;
    }

    start_time = rag_get_current_time_ms();

    /* Build request body - escape system_prompt and prompt */
    json_body = makeStringInfo();
    appendStringInfo(json_body, "{");
    appendStringInfo(json_body, "\"model\": \"%s\", ", config.llm_model);
    appendStringInfo(json_body, "\"messages\": [");
    appendStringInfo(json_body, "{\"role\": \"system\", \"content\": \"");
    /* Simple escape for system_prompt */
    for (i = 0; config.system_prompt && config.system_prompt[i]; i++)
    {
        if (config.system_prompt[i] == '"' || config.system_prompt[i] == '\\')
            appendStringInfoChar(json_body, '\\');
        appendStringInfoChar(json_body, config.system_prompt[i]);
    }
    appendStringInfo(json_body, "\"}, ");
    appendStringInfo(json_body, "{\"role\": \"user\", \"content\": \"");
    /* Simple escape for prompt */
    for (i = 0; prompt && prompt[i]; i++)
    {
        if (prompt[i] == '"' || prompt[i] == '\\')
            appendStringInfoChar(json_body, '\\');
        appendStringInfoChar(json_body, prompt[i]);
    }
    appendStringInfo(json_body, "\"}");
    appendStringInfo(json_body, "], ");
    appendStringInfo(json_body, "\"temperature\": %.1f", config.llm_temperature);
    appendStringInfo(json_body, "}");

    /* Build headers */
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", rag_llm_api_key);
    headers[0] = "Content-Type: application/json";
    headers[1] = auth_header;
    headers[2] = NULL;

    /* Make request with retry for 429 errors */
    for (i = 0; i < 3; i++)
    {
        resp = rag_http_post(rag_llm_api_url ? rag_llm_api_url : DEFAULT_LLM_URL,
                             headers, json_body->data, config.timeout_ms);

        /* Check if resp is valid */
        if (resp == NULL)
        {
            elog(WARNING, "LLM API: rag_http_post returned NULL");
            if (i < 2)
            {
                pg_usleep(500000);
                continue;
            }
            break;
        }

        if (resp->error == NULL && resp->status_code == 200)
            break;

        /* Retry on 429 (rate limit) or connection errors */
        if (i < 2 && (resp->status_code == 429 || resp->error != NULL))
        {
            elog(WARNING, "LLM API attempt %d failed, retrying...", i + 1);
            rag_http_response_free(resp);
            resp = NULL;
            /* 500ms backoff */
            pg_usleep(500000);
        }
        else
        {
            break;
        }
    }

    pfree(json_body->data);
    pfree(json_body);

    if (latency_ms)
        *latency_ms = (int32)(rag_get_current_time_ms() - start_time);

    /* Check if resp is NULL (all retries failed) */
    if (resp == NULL)
    {
        elog(WARNING, "LLM API failed after all retries");
        result = NULL;
        goto final_cleanup;
    }

    if (resp->error != NULL)
    {
        elog(WARNING, "LLM API error: %s", resp->error);
        goto cleanup;
    }

    if (resp->status_code != 200)
    {
        elog(WARNING, "LLM API returned status %ld: %s",
             resp->status_code, resp->body ? resp->body : "no body");
        goto cleanup;
    }

    /* Parse JSON response to extract content */
    if (resp->body)
    {
        result = parse_content_from_json(resp->body);
        if (!result)
        {
            elog(WARNING, "Failed to parse LLM response content");
            /* Return raw response for debugging */
            result = pstrdup(resp->body);
        }
    }

    cleanup:
    if (resp)
        rag_http_response_free(resp);

    final_cleanup:
    return result;
}

/*
 * rag_llm_build_prompt - Build prompt from retrieved chunks
 */
char *
rag_llm_build_prompt(const char *query, Chunk *chunks, int32 num_chunks,
                     const char *system_prompt)
{
    StringInfo  si = makeStringInfo();
    int         i;

    /* System prompt */
    appendStringInfo(si, "%s\n\n", system_prompt);

    /* Context from chunks */
    appendStringInfo(si, "基于以下上下文回答问题:\n\n");
    for (i = 0; i < num_chunks; i++)
    {
        appendStringInfo(si, "[文档 %d]\n%s\n\n", i + 1, chunks[i].content);
    }

    /* Question */
    appendStringInfo(si, "问题: %s\n\n", query);
    appendStringInfo(si, "请根据上述上下文回答:");

    return si->data;
}
