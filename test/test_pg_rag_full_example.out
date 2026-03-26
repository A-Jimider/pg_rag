-- ============================================
-- pg_rag 完整功能验证测试
-- ============================================
-- 使用说明：
-- 1. 修改下面的 API Keys（替换为你的真实 key）
-- 2. 可选：修改 API URL 和模型名称
-- 3. 运行: psql -d your_database -f test/test_pg_rag_full.sql

-- ============================================
-- 步骤 1: 加载扩展（必须先加载，才能配置 GUC 变量）
-- ============================================
CREATE EXTENSION IF NOT EXISTS vector;
CREATE EXTENSION IF NOT EXISTS pg_rag;

-- ============================================
-- 步骤 2: 配置 API Keys（请在此处替换）
-- ============================================

-- 设置 Embedding API Key（必需）
SET rag.embedding_api_key = 'your-embedding-api-key-here';

-- 设置 LLM API Key（必需）
SET rag.llm_api_key = 'your-llm-api-key-here';

-- ============================================
-- 可选配置（使用默认值可不修改）
-- ============================================

-- 自定义 API URL（可选，使用 OpenAI 官方可保持默认）
-- SET rag.embedding_api_url = 'https://api.openai.com/v1/embeddings';
-- SET rag.llm_api_url = 'https://api.openai.com/v1/chat/completions';

-- 自定义模型（可选）
-- SET rag.embedding_model = 'text-embedding-3-small';
-- SET rag.llm_model = 'gpt-4o-mini';
-- SET rag.llm_temperature = 0.7;

-- ============================================
-- API Key 验证（自动检查，请勿修改）
-- ============================================
DO $$
DECLARE
    embed_key TEXT;
    llm_key TEXT;
BEGIN
    embed_key := current_setting('rag.embedding_api_key', true);
    llm_key := current_setting('rag.llm_api_key', true);

    IF embed_key IS NULL OR embed_key = '' OR embed_key = 'your-embedding-api-key-here' THEN
        RAISE EXCEPTION '请先设置 rag.embedding_api_key。在文件开头找到 your-embedding-api-key-here 并替换为你的真实 API Key';
    END IF;

    IF llm_key IS NULL OR llm_key = '' OR llm_key = 'your-llm-api-key-here' THEN
        RAISE EXCEPTION '请先设置 rag.llm_api_key。在文件开头找到 your-llm-api-key-here 并替换为你的真实 API Key';
    END IF;

    RAISE NOTICE '✓ API Keys 已配置';
END $$;

-- ============================================
-- 步骤 3: 初始化环境
-- ============================================
SELECT '=== 步骤 3: 初始化环境 ===' AS section;

-- 删除旧的测试数据
DELETE FROM rag.documents WHERE kb_id IN (
    SELECT id FROM rag.knowledge_bases WHERE name LIKE 'full_test_%'
);
DELETE FROM rag.knowledge_bases WHERE name LIKE 'full_test_%';
DELETE FROM rag.config WHERE key LIKE 'full_test_%';

RAISE NOTICE '✓ 环境初始化完成';

-- ============================================
-- 步骤 4: 配置管理测试
-- ============================================
SELECT '=== 步骤 4: 配置管理测试 ===' AS section;

-- 修改分块配置（便于测试）
SELECT rag.set_config('chunk_size', '500');
SELECT rag.set_config('chunk_overlap', '50');
SELECT rag.set_config('system_prompt', '你是一个专业的技术助手，用简洁的中文回答。');

-- 验证配置
SELECT rag.get_config('chunk_size') AS chunk_size,
       rag.get_config('chunk_overlap') AS chunk_overlap;

-- ============================================
-- 步骤 5: 知识库管理测试
-- ============================================
SELECT '=== 步骤 5: 知识库管理测试 ===' AS section;

-- 创建测试知识库
SELECT rag.create_kb('full_test_tech_docs') AS tech_kb_id;
SELECT rag.create_kb('full_test_mini_docs', 768) AS mini_kb_id;

-- 验证知识库
SELECT id, name, embedding_dim FROM rag.knowledge_bases WHERE name LIKE 'full_test_%' ORDER BY id;

-- ============================================
-- 步骤 6: 文档插入测试（需要 Embedding API）
-- ============================================
SELECT '=== 步骤 6: 文档插入测试 ===' AS section;

-- 插入短文档（无 metadata）
SELECT rag.insert_document(
    'full_test_tech_docs',
    'PostgreSQL 是一个强大的开源关系型数据库管理系统。它支持 ACID 事务、MVCC、以及丰富的数据类型。'
) AS inserted_chunks_1;

-- 插入带 metadata 的文档
SELECT rag.insert_document(
    'full_test_tech_docs',
    'RAG（Retrieval-Augmented Generation）是一种将检索与生成结合的技术。它能有效减少大语言模型的幻觉问题，提高回答的准确性。',
    '{"source": "AI技术手册", "category": "技术", "author": "测试"}'::jsonb
) AS inserted_chunks_2;

-- 插入长文档（会被自动分块）
SELECT rag.insert_document(
    'full_test_tech_docs',
    '向量数据库是专门用于存储和查询向量嵌入的数据库。它们使用近似最近邻（ANN）算法来快速找到相似的向量。常见的向量数据库包括 pgvector、Milvus、Pinecone 等。pgvector 是 PostgreSQL 的扩展，它允许在 PostgreSQL 中直接存储和查询向量，无需额外的数据库系统。',
    '{"source": "数据库指南", "tags": ["向量", "数据库"]}'::jsonb
) AS inserted_chunks_3;

-- 验证插入的文档
SELECT
    d.id,
    d.chunk_index,
    LEFT(d.content, 50) AS content_preview,
    d.metadata,
    pg_column_size(d.embedding) AS embedding_bytes,
    CASE WHEN pg_column_size(d.embedding) > 6000 THEN '✓' ELSE '✗' END AS has_embedding
FROM rag.documents d
JOIN rag.knowledge_bases k ON d.kb_id = k.id
WHERE k.name = 'full_test_tech_docs'
ORDER BY d.id;

-- ============================================
-- 步骤 7: explain 测试（需要 Embedding API）
-- ============================================
SELECT '=== 步骤 7: explain 测试 ===' AS section;

-- 测试 rag.explain - 查看检索过程
SELECT
    LEFT(query_embedding::text, 20) AS query_embedding_preview,
    jsonb_array_length(retrieved_chunks) AS num_chunks_retrieved,
    LEFT(prompt, 200) AS prompt_preview,
    token_estimate,
    latency_ms
FROM rag.explain('full_test_tech_docs', '什么是RAG技术？', 3);

-- 查看检索到的 chunks 详情
SELECT jsonb_pretty(retrieved_chunks) AS retrieved_details
FROM rag.explain('full_test_tech_docs', '向量数据库如何工作？', 2);

-- ============================================
-- 步骤 8: query 测试（需要 Embedding + LLM API）
-- ============================================
SELECT '=== 步骤 8: query 测试 ===' AS section;

-- 测试 1: RAG 相关问题
SELECT
    '测试1: RAG相关问题' AS test_name,
    LEFT(answer, 100) AS answer_preview,
    latency_ms
FROM rag.query('full_test_tech_docs', '什么是RAG技术？', 3);

-- 测试 2: PostgreSQL 相关问题
SELECT
    '测试2: PostgreSQL相关问题' AS test_name,
    answer,
    latency_ms
FROM rag.query('full_test_tech_docs', 'PostgreSQL支持什么特性？', 3);

-- 测试 3: 向量数据库相关问题
SELECT
    '测试3: 向量数据库' AS test_name,
    LEFT(answer, 100) AS answer_preview,
    latency_ms
FROM rag.query('full_test_tech_docs', 'pgvector有什么优势？', 3);

-- ============================================
-- 步骤 9: 重复文档检测测试
-- ============================================
SELECT '=== 步骤 9: 重复文档检测测试 ===' AS section;

-- 插入新文档
SELECT rag.insert_document(
    'full_test_tech_docs',
    '这是一条用于测试重复检测的文档内容。'
) AS first_insert;

-- 再次插入相同内容（应该被跳过，返回 0）
SELECT rag.insert_document(
    'full_test_tech_docs',
    '这是一条用于测试重复检测的文档内容。'
) AS second_insert_should_be_0;

-- 验证文档数量
SELECT COUNT(*) AS total_docs FROM rag.documents
WHERE kb_id = (SELECT id FROM rag.knowledge_bases WHERE name = 'full_test_tech_docs');

-- ============================================
-- 步骤 10: 多知识库测试
-- ============================================
SELECT '=== 步骤 10: 多知识库测试 ===' AS section;

-- 在第二个知识库插入文档
SELECT rag.insert_document(
    'full_test_mini_docs',
    '这是一个使用768维向量的知识库。适用于一些轻量级的嵌入模型。'
) AS mini_kb_insert;

-- 查询第二个知识库
SELECT LEFT(answer, 100) AS answer_preview
FROM rag.query('full_test_mini_docs', '这个知识库有什么特点？', 2);

-- ============================================
-- 步骤 11: 错误处理测试
-- ============================================
SELECT '=== 步骤 11: 错误处理测试 ===' AS section;

-- 测试不存在的知识库
DO $$
BEGIN
    PERFORM rag.explain('non_existent_kb', 'test', 5);
EXCEPTION
    WHEN OTHERS THEN
        RAISE NOTICE 'OK: 不存在的知识库返回错误: %', SQLERRM;
END $$;

-- ============================================
-- 步骤 12: 性能统计
-- ============================================
SELECT '=== 步骤 12: 性能统计 ===' AS section;

SELECT
    k.name AS kb_name,
    COUNT(d.id) AS document_count,
    SUM(LENGTH(d.content)) AS total_content_length,
    AVG(pg_column_size(d.embedding)) AS avg_embedding_size
FROM rag.knowledge_bases k
LEFT JOIN rag.documents d ON k.id = d.kb_id
WHERE k.name LIKE 'full_test_%'
GROUP BY k.id, k.name;

-- ============================================
-- 步骤 13: 清理
-- ============================================
SELECT '=== 步骤 13: 清理测试数据 ===' AS section;

DELETE FROM rag.documents WHERE kb_id IN (
    SELECT id FROM rag.knowledge_bases WHERE name LIKE 'full_test_%'
);
DELETE FROM rag.knowledge_bases WHERE name LIKE 'full_test_%';
DELETE FROM rag.config WHERE key IN ('chunk_size', 'chunk_overlap', 'system_prompt');

SELECT COUNT(*) AS remaining_test_kbs FROM rag.knowledge_bases WHERE name LIKE 'full_test_%';

-- ============================================
-- 测试完成
-- ============================================
SELECT '=== 所有完整功能测试已完成 ===' AS summary;

/*
使用示例:

1. OpenAI 官方 API:
   SET rag.embedding_api_key = 'sk-xxxxxxxxxxxxxxxxxxxxxxxx';
   SET rag.llm_api_key = 'sk-xxxxxxxxxxxxxxxxxxxxxxxx';

2. 阿里云 DashScope (通义千问):
   SET rag.embedding_api_key = 'sk-xxxxxxxx';
   SET rag.llm_api_key = 'sk-xxxxxxxx';
   SET rag.embedding_api_url = 'https://dashscope.aliyuncs.com/compatible-mode/v1/embeddings';
   SET rag.llm_api_url = 'https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions';
   SET rag.embedding_model = 'text-embedding-v3';
   SET rag.llm_model = 'qwen-turbo';

3. Moonshot AI (Kimi):
   SET rag.embedding_api_key = 'sk-xxxxxxxx';
   SET rag.llm_api_key = 'sk-xxxxxxxx';
   SET rag.llm_api_url = 'https://api.moonshot.cn/v1/chat/completions';
   SET rag.llm_model = 'kimi-k2.5';
*/
