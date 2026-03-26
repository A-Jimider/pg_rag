# pg_rag

[![License](https://img.shields.io/badge/license-Custom-blue.svg)](LICENSE)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-14+-blue.svg)](https://www.postgresql.org/)
[![pgvector](https://img.shields.io/badge/pgvector-required-green.svg)](https://github.com/pgvector/pgvector)

PostgreSQL RAG Extension - Retrieval-Augmented Generation in SQL

[中文文档](README.zh.md)

## Features

- **Pure SQL Interface**: Complete RAG workflow without glue code
- **Automatic Chunking**: Sliding window algorithm with configurable size and overlap
- **Vector Search**: Similarity search powered by pgvector
- **Configuration Management**: GUC variables + table storage, support for sensitive config via environment variables
- **Error Handling**: `skip` and `fail` strategies

## Requirements

- PostgreSQL 14+
- pgvector extension
- libcurl

## Installation

```bash
# Clone repository
cd pg_rag

# Build and install
make
sudo make install

# Create extension in database
CREATE EXTENSION pg_rag;
```

## Quick Start

### 1. Configure API Keys

```sql
-- Option 1: GUC variables (recommended, session level)
SET rag.embedding_api_key = 'sk-your-key';
SET rag.llm_api_key = 'sk-your-key';

-- Option 2: Environment variables
-- export PG_RAG_EMBEDDING_API_KEY='sk-your-key'
-- export PG_RAG_LLM_API_KEY='sk-your-key'
```

### 2. Create Knowledge Base

```sql
SELECT rag.create_kb('docs');
```

### 3. Insert Documents

```sql
SELECT rag.insert_document(
    'docs',
    'PostgreSQL is an advanced object-relational database management system...',
    '{"source": "official"}'::jsonb
);
```

### 4. RAG Query

```sql
SELECT * FROM rag.query(
    'docs',
    'What is PostgreSQL?',
    5
);
```

Returns:
```
answer: "PostgreSQL is an advanced object-relational database..."
sources: [{"content": "...", "score": 0.95, "chunk_index": 0}]
debug: {"prompt_length": 1200, "llm_latency_ms": 800}
latency_ms: 1200
```

### 5. Explain / Debug

```sql
SELECT * FROM rag.explain(
    'docs',
    'What is PostgreSQL?'
);
```

## Configuration

### Table-based Configuration (Non-sensitive)

```sql
SELECT rag.set_config('chunk_size', '1200');
SELECT rag.set_config('chunk_overlap', '200');
SELECT rag.set_config('timeout_ms', '30000');
SELECT rag.set_config('system_prompt', 'You are a helpful assistant.');
SELECT rag.set_config('embedding_model', 'text-embedding-3-small');
SELECT rag.set_config('llm_model', 'gpt-4o-mini');
```

### GUC Configuration (Sensitive)

```sql
SET rag.embedding_api_key = 'sk-xxx';
SET rag.llm_api_key = 'sk-xxx';
SET rag.embedding_api_url = 'https://api.openai.com/v1/embeddings';
SET rag.llm_api_url = 'https://api.openai.com/v1/chat/completions';
```

### Configuration Priority

```
Function parameters > GUC (session) > rag.config table > System defaults
```

## API Reference

### Core Functions

| Function | Description | Returns |
|----------|-------------|---------|
| `rag.create_kb(name, embedding_dim)` | Create knowledge base | `INT` - KB ID |
| `rag.insert_document(kb, content, metadata, on_error)` | Insert document (auto-chunk + embedding) | `INT` - Chunks inserted |
| `rag.query(kb, query, top_k)` | RAG query (retrieve + LLM generate) | `rag.query_result` |
| `rag.explain(kb, query, top_k)` | Debug retrieval (retrieve only, no LLM) | `rag.explain_result` |
| `rag.set_config(key, value)` | Set configuration | `VOID` |
| `rag.get_config(key)` | Get configuration | `TEXT` |

### Function Details

#### `rag.create_kb(name TEXT, embedding_dim INT DEFAULT 1536)`

Create a knowledge base.

**Parameters:**
- `name`: Knowledge base name (unique)
- `embedding_dim`: Vector dimension, default 1536 (OpenAI) or 768 (some models)

**Example:**
```sql
SELECT rag.create_kb('tech_docs');           -- Default 1536 dim
SELECT rag.create_kb('tech_docs', 768);      -- Custom 768 dim
```

---

#### `rag.insert_document(kb TEXT, content TEXT, metadata JSONB DEFAULT '{}', on_error TEXT DEFAULT NULL)`

Insert document with automatic chunking and embedding generation.

**Parameters:**
- `kb`: Knowledge base name
- `content`: Document content
- `metadata`: Metadata (JSONB, optional), e.g., `{"source": "manual"}`
- `on_error`: Error handling strategy, `'skip'`(default) or `'fail'`

**Returns:** Number of chunks actually inserted (0 for duplicates)

**Example:**
```sql
SELECT rag.insert_document(
    'tech_docs',
    'PostgreSQL is a powerful open source database...',
    '{"source": "official", "category": "database"}'::jsonb
);
```

---

#### `rag.query(kb TEXT, query TEXT, top_k INT DEFAULT 5)`

RAG query: Retrieve relevant chunks → Build prompt → Call LLM to generate answer.

**Parameters:**
- `kb`: Knowledge base name
- `query`: User question
- `top_k`: Retrieve top_k most similar chunks (default 5)

**Returns:** `rag.query_result` type
- `answer TEXT`: LLM generated answer
- `sources JSONB`: Source document information
- `debug JSONB`: Debug information
- `latency_ms INT`: Total latency (milliseconds)

**Example:**
```sql
SELECT answer, latency_ms
FROM rag.query('tech_docs', 'What is PostgreSQL?', 3);
```

---

#### `rag.explain(kb TEXT, query TEXT, top_k INT DEFAULT 5)`

Debug function: Execute retrieval and return detailed information, **no LLM call** (useful for debugging retrieval quality).

**Parameters:**
- `kb`: Knowledge base name
- `query`: Query text
- `top_k`: Number of chunks to retrieve (default 5)

**Returns:** `rag.explain_result` type
- `query_embedding TEXT`: Query vector (text representation)
- `retrieved_chunks JSONB`: Retrieved document chunks (with similarity scores)
- `prompt TEXT`: Full constructed prompt (what would be sent to LLM)
- `token_estimate INT`: Estimated token count
- `latency_ms INT`: Retrieval latency (milliseconds)

**Example:**
```sql
-- View retrieved content and prompt
SELECT jsonb_pretty(retrieved_chunks), prompt
FROM rag.explain('tech_docs', 'What is vector database?', 2);
```

---

#### `rag.set_config(key TEXT, value TEXT)` / `rag.get_config(key TEXT)`

Set/Get persistent configuration (stored in `rag.config` table).

**Common config keys:**
- `chunk_size`: Chunk size (characters, default 1200)
- `chunk_overlap`: Chunk overlap (characters, default 200)
- `system_prompt`: System prompt
- `embedding_model`: Embedding model name
- `llm_model`: LLM model name

**Example:**
```sql
SELECT rag.set_config('chunk_size', '800');
SELECT rag.set_config('system_prompt', 'You are a professional technical support assistant');
SELECT rag.get_config('chunk_size');  -- Returns '800'
```

## Data Tables

### rag.knowledge_bases

| Column | Type | Description |
|--------|------|-------------|
| `id` | SERIAL | Primary key |
| `name` | TEXT | Knowledge base name (unique) |
| `embedding_dim` | INT | Vector dimension (default 1536) |
| `created_at` | TIMESTAMP | Creation time |

### rag.documents

| Column | Type | Description |
|--------|------|-------------|
| `id` | BIGSERIAL | Primary key |
| `kb_id` | INT | Knowledge base ID (foreign key) |
| `content` | TEXT | Document content |
| `embedding` | vector(1536) | Vector (pgvector) |
| `metadata` | JSONB | Metadata |
| `chunk_index` | INT | Chunk index |
| `token_estimate` | INT | Estimated token count |
| `created_at` | TIMESTAMP | Creation time |

### rag.config

| Column | Type | Description |
|--------|------|-------------|
| `key` | TEXT | Config key (primary key) |
| `value` | TEXT | Config value |
| `updated_at` | TIMESTAMP | Update time |

## Configuration Options

| Key | Default | Description |
|-----|---------|-------------|
| `chunk_size` | 1200 | Chunk size (characters) |
| `chunk_overlap` | 200 | Chunk overlap (characters) |
| `timeout_ms` | 30000 | API timeout (milliseconds) |
| `system_prompt` | You are a helpful... | System prompt |
| `embedding_model` | text-embedding-3-small | Embedding model |
| `llm_model` | gpt-4o-mini | LLM model |
| `on_error` | skip | Error handling strategy |

## Project Structure

```
pg_rag/
├── pg_rag.control          # Extension control file
├── Makefile
├── sql/
│   └── pg_rag--0.1.0.sql   # SQL installation script
├── src/
│   ├── pg_rag.h            # Main header
│   ├── pg_rag.c            # Main entry + SQL functions
│   ├── config.c            # Configuration management
│   ├── kb.c                # Knowledge base management
│   ├── chunk.c             # Text chunking
│   ├── http.c              # HTTP client (popen + curl)
│   ├── embedding.c         # Embedding API
│   ├── llm.c               # LLM Chat API
│   ├── retrieve.c          # pgvector retrieval
│   └── utils.c             # Utility functions
└── README.md
```

## Chunking Strategy

Sliding window algorithm:

```
chunk_size = 1200        -- characters
chunk_overlap = 200      -- characters

chunk1: [0 - 1200]
chunk2: [1000 - 2200]    -- overlap 200
chunk3: [2000 - 3200]    -- overlap 200
```

Advantages:
- Avoids sentence truncation and semantic breakage
- Adjacent chunks have context association

## Error Handling

### rag.insert_document Error Strategy

- `skip` (default): Skip failed chunks, continue processing others
- `fail`: Rollback entire transaction if any chunk fails

```sql
-- Default strategy (skip)
SELECT rag.insert_document('docs', 'content...');

-- Explicit strategy
SELECT rag.insert_document('docs', 'content...', on_error => 'fail');
```

### rag.query Error Handling

- Embedding failure: Error (query cannot continue)
- LLM failure: Returns sources, answer is null

## Roadmap

- [ ] Hybrid Search (full-text + vector)
- [ ] Rerank support
- [ ] Streaming output
- [ ] Embedding cache
- [ ] Multi-model support

## License

This project uses a custom license. Free for non-commercial use, commercial use requires separate authorization.
See [LICENSE](LICENSE) file for details.
