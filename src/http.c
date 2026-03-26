/*-------------------------------------------------------------------------
 * http.c
 *     HTTP client - using popen for safer process execution
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "utils/memutils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * rag_http_post - Perform HTTP POST request using popen
 * Safer alternative to system() that doesn't interfere with PostgreSQL signals
 */
HttpResponse *
rag_http_post(const char *url, const char *headers[],
              const char *body, int32 timeout_ms)
{
    HttpResponse *resp;
    char        temp_file[] = "/tmp/pg_rag_resp_XXXXXX";
    char        body_file[] = "/tmp/pg_rag_body_XXXXXX";
    char        cmd[8192];
    char        header_str[2048] = "";
    int         fd;
    int         i;
    FILE       *fp;
    char        line[4096];
    long        status_code = 0;
    size_t      body_len;
    int         ret;
    int         body_len_written;

    /* Allocate response - use TopMemoryContext for cross-context safety */
    resp = (HttpResponse *) MemoryContextAlloc(TopMemoryContext, sizeof(HttpResponse));
    memset(resp, 0, sizeof(HttpResponse));
    resp->body = NULL;

    /* Create temp file for response */
    fd = mkstemp(temp_file);
    if (fd < 0)
    {
        resp->error = MemoryContextStrdup(TopMemoryContext, "Failed to create temp file");
        return resp;
    }
    close(fd);

    /* Create temp file for body */
    fd = mkstemp(body_file);
    if (fd < 0)
    {
        resp->error = MemoryContextStrdup(TopMemoryContext, "Failed to create body temp file");
        unlink(temp_file);
        return resp;
    }

    /* Write body to file */
    body_len = strlen(body);
    body_len_written = write(fd, body, body_len);
    if (body_len_written != (ssize_t) body_len)
    {
        resp->error = MemoryContextStrdup(TopMemoryContext, "Failed to write body to temp file");
        close(fd);
        unlink(temp_file);
        unlink(body_file);
        return resp;
    }
    close(fd);

    /* Build headers */
    for (i = 0; headers && headers[i]; i++)
    {
        strcat(header_str, " -H '");
        strcat(header_str, headers[i]);
        strcat(header_str, "'");
    }

    /* Build curl command - use @file for body to avoid shell escaping issues */
    snprintf(cmd, sizeof(cmd),
             "curl -s -w '\\nHTTP_CODE:%%{http_code}' -m %d %s -d '@%s' '%s' > %s 2>/dev/null",
             timeout_ms / 1000, header_str, body_file, url, temp_file);

    /* Execute curl with retry using popen instead of system() */
    for (i = 0; i < 3; i++)
    {
        /* Use popen to execute command - safer than system() for PostgreSQL */
        fp = popen(cmd, "r");
        if (fp == NULL)
        {
            ret = -1;
        }
        else
        {
            /* Wait for command to complete */
            ret = pclose(fp);
            /* Normalize exit code */
            if (ret == -1)
                ret = -1;
            else if (WIFEXITED(ret))
                ret = WEXITSTATUS(ret);
            else
                ret = -1;
        }

        if (ret == 0)
            break;
        if (i < 2)
            usleep(100000);  /* 100ms retry delay */
    }

    if (ret != 0)
    {
        resp->error = MemoryContextAlloc(TopMemoryContext, 256);
        if (resp->error)
            snprintf(resp->error, 256, "curl command failed with exit code %d", ret);
        else
            resp->error = MemoryContextStrdup(TopMemoryContext, "curl command failed");
        unlink(temp_file);
        unlink(body_file);
        return resp;
    }

    /* Cleanup body file */
    unlink(body_file);

    /* Read response */
    fp = fopen(temp_file, "r");
    if (!fp)
    {
        resp->error = MemoryContextStrdup(TopMemoryContext, "Failed to read response");
        unlink(temp_file);
        return resp;
    }

    /* Read response body */
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "HTTP_CODE:", 10) == 0)
        {
            status_code = atol(line + 10);
        }
        else
        {
            /* Append to body */
            if (resp->body == NULL)
            {
                resp->body = MemoryContextStrdup(TopMemoryContext, line);
            }
            else
            {
                char *new_body = MemoryContextAlloc(TopMemoryContext, strlen(resp->body) + strlen(line) + 1);
                strcpy(new_body, resp->body);
                strcat(new_body, line);
                pfree(resp->body);
                resp->body = new_body;
            }
        }
    }
    fclose(fp);
    unlink(temp_file);

    if (resp->body == NULL)
        resp->body = MemoryContextStrdup(TopMemoryContext, "");

    resp->status_code = status_code;

    return resp;
}

/*
 * rag_http_response_free - Free HttpResponse
 */
void
rag_http_response_free(HttpResponse *resp)
{
    if (resp == NULL)
        return;

    if (resp->body)
        pfree(resp->body);
    if (resp->error)
        pfree(resp->error);

    pfree(resp);
}
