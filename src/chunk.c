/*-------------------------------------------------------------------------
 * chunk.c
 *     Text chunking with sliding window algorithm
 *-------------------------------------------------------------------------
 */
#include "pg_rag.h"

#include <string.h>

/*
 * rag_chunk_text - Split text into overlapping chunks
 * Uses sliding window: [0, size], [size-overlap, 2*size-overlap], ...
 *
 * Parameters:
 *   text - input text to chunk
 *   num_chunks - output: number of chunks created
 *   chunk_size - size of each chunk in characters
 *   chunk_overlap - overlap between consecutive chunks
 *
 * Returns: array of Chunk structs (must be freed with rag_chunks_free)
 */
Chunk *
rag_chunk_text(const char *text, int32 *num_chunks,
               int32 chunk_size, int32 chunk_overlap)
{
    int32       text_len;
    int32       step;
    int32       n_chunks;
    Chunk      *chunks;
    int32       i;
    int32       pos;

    if (text == NULL || text[0] == '\0')
    {
        *num_chunks = 0;
        return NULL;
    }

    text_len = strlen(text);

    /* If text fits in one chunk, return single chunk */
    if (text_len <= chunk_size)
    {
        chunks = (Chunk *) palloc(sizeof(Chunk));
        chunks[0].content = pstrdup(text);
        chunks[0].length = text_len;
        chunks[0].chunk_index = 0;
        chunks[0].token_estimate = text_len / CHARS_PER_TOKEN;
        *num_chunks = 1;
        return chunks;
    }

    /* Calculate number of chunks needed */
    step = chunk_size - chunk_overlap;
    if (step <= 0)
        elog(ERROR, "chunk_overlap must be less than chunk_size");

    n_chunks = 1 + (text_len - chunk_size + step - 1) / step;
    if (n_chunks < 1)
        n_chunks = 1;

    chunks = (Chunk *) palloc(sizeof(Chunk) * n_chunks);

    for (i = 0, pos = 0; i < n_chunks; i++, pos += step)
    {
        int32 chunk_len;

        if (pos + chunk_size > text_len)
        {
            /* Last chunk - may be shorter */
            pos = text_len - chunk_size;
            if (pos < 0)
                pos = 0;
            chunk_len = text_len - pos;
        }
        else
        {
            chunk_len = chunk_size;
        }

        chunks[i].content = pnstrdup(text + pos, chunk_len);
        chunks[i].length = chunk_len;
        chunks[i].chunk_index = i;
        chunks[i].token_estimate = chunk_len / CHARS_PER_TOKEN;
    }

    *num_chunks = n_chunks;
    return chunks;
}

/*
 * rag_chunks_free - Free memory allocated for chunks
 */
void
rag_chunks_free(Chunk *chunks, int32 num_chunks)
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
