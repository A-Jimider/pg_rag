/*-------------------------------------------------------------------------
 * utils.c
 *     Utility functions
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include "utils/timestamp.h"
#include "utils/jsonb.h"
#include "utils/builtins.h"

/*
 * rag_jsonb_to_cstring - Convert JsonbValue to cstring
 * Simplified version
 */
char *
rag_jsonb_to_cstring(JsonbValue *jbv)
{
    if (jbv == NULL)
        return pstrdup("{}");

    return pstrdup("{}");
}

/*
 * rag_cstring_to_jsonb - Convert cstring to Jsonb
 * Simplified version - stub implementation
 */
Jsonb *
rag_cstring_to_jsonb(const char *str)
{
    /* Stub - just return empty jsonb */
    return NULL;
}

/*
 * rag_get_current_time_ms - Get current time in milliseconds
 */
int64
rag_get_current_time_ms(void)
{
    TimestampTz ts = GetCurrentTimestamp();
    /* Convert to milliseconds since epoch */
    /* PostgreSQL timestamp is in microseconds */
    return (int64)(ts / 1000);
}
