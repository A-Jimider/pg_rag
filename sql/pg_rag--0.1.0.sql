-- pg_rag extension 0.1.0
-- PostgreSQL RAG Extension

-- Schema 'rag' is created automatically by PostgreSQL
-- based on the 'schema = rag' in pg_rag.control

-- ============================================
-- Types
-- ============================================

-- Return type for rag.query
CREATE TYPE rag.query_result AS (
    answer TEXT,
    sources JSONB,
    debug JSONB,
    latency_ms INT
);

-- Return type for rag.explain
CREATE TYPE rag.explain_result AS (
    query_embedding TEXT,
    retrieved_chunks JSONB,
    prompt TEXT,
    token_estimate INT,
    latency_ms INT
);

-- ============================================
-- Tables
-- ============================================

-- Knowledge base table
CREATE TABLE rag.knowledge_bases (
    id SERIAL PRIMARY KEY,
    name TEXT UNIQUE NOT NULL,
    embedding_dim INT DEFAULT 1536,
    created_at TIMESTAMP DEFAULT now()
);

-- Documents/chunks table
CREATE TABLE rag.documents (
    id BIGSERIAL PRIMARY KEY,
    kb_id INT REFERENCES rag.knowledge_bases(id),
    content TEXT,
    metadata JSONB,
    embedding public.vector(1536),
    chunk_index INT,
    source_doc_id TEXT,
    token_estimate INT,
    created_at TIMESTAMP DEFAULT now()
);

-- Config table (non-sensitive configs)
CREATE TABLE rag.config (
    key TEXT PRIMARY KEY,
    value TEXT,
    updated_at TIMESTAMP DEFAULT now()
);

-- Index on embedding for similarity search
CREATE INDEX ON rag.documents
USING hnsw (embedding public.vector_cosine_ops);

CREATE INDEX ON rag.documents(kb_id);

-- ============================================
-- Default configs
-- ============================================

INSERT INTO rag.config (key, value) VALUES
    ('chunk_size', '1200'),
    ('chunk_overlap', '200'),
    ('timeout_ms', '30000'),
    ('system_prompt', 'You are a helpful assistant. Use the provided context to answer the question.'),
    ('embedding_model', 'text-embedding-3-small'),
    ('llm_model', 'gpt-4o-mini'),
    ('on_error', 'skip');

-- ============================================
-- Functions
-- ============================================

-- Config management
CREATE OR REPLACE FUNCTION rag.set_config(key TEXT, value TEXT)
RETURNS VOID
LANGUAGE C
AS 'MODULE_PATHNAME', 'rag_set_config';

CREATE OR REPLACE FUNCTION rag.get_config(key TEXT)
RETURNS TEXT
LANGUAGE C
AS 'MODULE_PATHNAME', 'rag_get_config';

-- Knowledge base management
CREATE OR REPLACE FUNCTION rag.create_kb(name TEXT, embedding_dim INT DEFAULT 1536)
RETURNS INT
LANGUAGE C
AS 'MODULE_PATHNAME', 'rag_create_kb';

-- Document insertion with automatic chunking and embedding
CREATE OR REPLACE FUNCTION rag.insert_document(
    kb TEXT,
    content TEXT,
    metadata JSONB DEFAULT '{}',
    on_error TEXT DEFAULT NULL  -- NULL means use config
)
RETURNS INT
LANGUAGE C
AS 'MODULE_PATHNAME', 'rag_insert';

-- RAG query (retrieve + generate)
CREATE OR REPLACE FUNCTION rag.query(
    kb TEXT,
    query TEXT,
    top_k INT DEFAULT 5
)
RETURNS rag.query_result
LANGUAGE C
AS 'MODULE_PATHNAME', 'rag_query';

-- Explain/debug (retrieve only)
CREATE OR REPLACE FUNCTION rag.explain(
    kb TEXT,
    query TEXT,
    top_k INT DEFAULT 5
)
RETURNS rag.explain_result
LANGUAGE C
AS 'MODULE_PATHNAME', 'rag_explain';

-- ============================================
-- GUC variables (sensitive configs)
-- Will be registered by C code during _PG_init
-- ============================================

-- These are configured via:
-- SET rag.embedding_api_key = 'sk-xxx';
-- SET rag.llm_api_key = 'sk-xxx';
-- SET rag.embedding_api_url = 'https://api.openai.com/v1/embeddings';
-- SET rag.llm_api_url = 'https://api.openai.com/v1/chat/completions';
