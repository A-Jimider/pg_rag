# pg_rag

[![License](https://img.shields.io/badge/license-Custom-blue.svg)](LICENSE)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-14+-blue.svg)](https://www.postgresql.org/)
[![pgvector](https://img.shields.io/badge/pgvector-required-green.svg)](https://github.com/pgvector/pgvector)

PostgreSQL RAG Extension - 用 SQL 一步完成检索增强生成 (Retrieval-Augmented Generation)

[English](README.md)

## 功能特性

- **纯 SQL 接口**：无需编写胶水代码，用 SQL 完成 RAG 全流程
- **自动分块**：滑动窗口算法，支持可配置的分块大小和重叠
- **向量检索**：基于 pgvector 的相似度搜索
- **配置管理**：GUC + 表存储，支持敏感信息环境变量配置
- **错误处理**：支持 `skip` 和 `fail` 两种错误处理策略

## 依赖

- PostgreSQL 14+
- pgvector 扩展
- libcurl

## 安装

```bash
# 克隆仓库
cd pg_rag

# 编译安装
make
sudo make install

# 在数据库中创建扩展
CREATE EXTENSION pg_rag;
```

## 快速开始

### 1. 配置 API Key

```sql
-- 方式1：GUC (推荐，session 级别)
SET rag.embedding_api_key = 'sk-your-key';
SET rag.llm_api_key = 'sk-your-key';

-- 方式2：环境变量
-- export PG_RAG_EMBEDDING_API_KEY='sk-your-key'
-- export PG_RAG_LLM_API_KEY='sk-your-key'
```

### 2. 创建知识库

```sql
SELECT rag.create_kb('docs');
```

### 3. 插入文档

```sql
SELECT rag.insert_document(
    'docs',
    'PostgreSQL is an advanced object-relational database management system...',
    '{"source": "official"}'::jsonb
);
```

### 4. RAG 查询

```sql
SELECT * FROM rag.query(
    'docs',
    'What is PostgreSQL?',
    5
);
```

返回结果：
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

## 配置

### 表存储配置（非敏感）

```sql
SELECT rag.set_config('chunk_size', '1200');
SELECT rag.set_config('chunk_overlap', '200');
SELECT rag.set_config('timeout_ms', '30000');
SELECT rag.set_config('system_prompt', 'You are a helpful assistant.');
SELECT rag.set_config('embedding_model', 'text-embedding-3-small');
SELECT rag.set_config('llm_model', 'gpt-4o-mini');
```

### GUC 配置（敏感）

```sql
SET rag.embedding_api_key = 'sk-xxx';
SET rag.llm_api_key = 'sk-xxx';
SET rag.embedding_api_url = 'https://api.openai.com/v1/embeddings';
SET rag.llm_api_url = 'https://api.openai.com/v1/chat/completions';
```

### 配置优先级

```
函数参数 > GUC (session) > 表 rag.config > 系统默认值
```

## API 参考

### 核心函数

| 函数 | 功能 | 返回值 |
|------|------|--------|
| `rag.create_kb(name, embedding_dim)` | 创建知识库 | `INT` - 知识库 ID |
| `rag.insert_document(kb, content, metadata, on_error)` | 插入文档（自动分块 + 生成 embedding） | `INT` - 插入的 chunk 数 |
| `rag.query(kb, query, top_k)` | RAG 查询（检索 + LLM 生成） | `rag.query_result` |
| `rag.explain(kb, query, top_k)` | 调试检索（仅检索，不调用 LLM） | `rag.explain_result` |
| `rag.set_config(key, value)` | 设置配置项 | `VOID` |
| `rag.get_config(key)` | 获取配置值 | `TEXT` |

### 函数详解

#### `rag.create_kb(name TEXT, embedding_dim INT DEFAULT 1536)`

创建知识库。

**参数：**
- `name`: 知识库名称（唯一）
- `embedding_dim`: 向量维度，默认 1536（OpenAI 默认）或 768（部分模型）

**示例：**
```sql
SELECT rag.create_kb('tech_docs');           -- 默认 1536 维
SELECT rag.create_kb('tech_docs', 768);      -- 自定义 768 维
```

---

#### `rag.insert_document(kb TEXT, content TEXT, metadata JSONB DEFAULT '{}', on_error TEXT DEFAULT NULL)`

插入文档，自动分块并生成 embedding。

**参数：**
- `kb`: 知识库名称
- `content`: 文档内容
- `metadata`: 元数据（JSONB，可选），如 `{"source": "manual"}`
- `on_error`: 错误处理策略，`'skip'`(默认) 或 `'fail'`

**返回值：** 实际插入的 chunk 数量（重复内容返回 0）

**示例：**
```sql
SELECT rag.insert_document(
    'tech_docs',
    'PostgreSQL 是一个强大的开源数据库...',
    '{"source": "官网", "category": "数据库"}'::jsonb
);
```

---

#### `rag.query(kb TEXT, query TEXT, top_k INT DEFAULT 5)`

RAG 查询：检索相关 chunks → 构建 prompt → 调用 LLM 生成回答。

**参数：**
- `kb`: 知识库名称
- `query`: 用户问题
- `top_k`: 检索最相似的 top_k 个 chunks（默认 5）

**返回值：** `rag.query_result` 类型
- `answer TEXT`: LLM 生成的回答
- `sources JSONB`: 来源文档信息
- `debug JSONB`: 调试信息
- `latency_ms INT`: 总耗时（毫秒）

**示例：**
```sql
SELECT answer, latency_ms
FROM rag.query('tech_docs', '什么是PostgreSQL？', 3);
```

---

#### `rag.explain(kb TEXT, query TEXT, top_k INT DEFAULT 5)`

调试功能：执行检索并返回详细信息，**不调用 LLM**（用于调试检索效果）。

**参数：**
- `kb`: 知识库名称
- `query`: 查询问题
- `top_k`: 检索数量（默认 5）

**返回值：** `rag.explain_result` 类型
- `query_embedding TEXT`: 查询向量（文本表示）
- `retrieved_chunks JSONB`: 检索到的文档 chunks（含相似度分数）
- `prompt TEXT`: 构建的完整 prompt（发给 LLM 的内容）
- `token_estimate INT`: 预估 token 数
- `latency_ms INT`: 检索耗时（毫秒）

**示例：**
```sql
-- 查看检索到的内容和 prompt
SELECT jsonb_pretty(retrieved_chunks), prompt
FROM rag.explain('tech_docs', '什么是向量数据库？', 2);
```

---

#### `rag.set_config(key TEXT, value TEXT)` / `rag.get_config(key TEXT)`

设置/获取持久化配置（存储在 `rag.config` 表）。

**常用配置键：**
- `chunk_size`: 分块大小（字符，默认 1200）
- `chunk_overlap`: 分块重叠（字符，默认 200）
- `system_prompt`: 系统提示词
- `embedding_model`: 嵌入模型名称
- `llm_model`: LLM 模型名称

**示例：**
```sql
SELECT rag.set_config('chunk_size', '800');
SELECT rag.set_config('system_prompt', '你是专业的技术支持助手');
SELECT rag.get_config('chunk_size');  -- 返回 '800'
```

## 数据表

### rag.knowledge_bases（知识库表）

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `id` | SERIAL | 主键 |
| `name` | TEXT | 知识库名称（唯一） |
| `embedding_dim` | INT | 向量维度（默认1536） |
| `created_at` | TIMESTAMP | 创建时间 |

### rag.documents（文档表）

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `id` | BIGSERIAL | 主键 |
| `kb_id` | INT | 知识库ID（外键） |
| `content` | TEXT | 文档内容 |
| `embedding` | vector(1536) | 向量（pgvector） |
| `metadata` | JSONB | 元数据 |
| `chunk_index` | INT | 分块索引 |
| `token_estimate` | INT | 预估token数 |
| `created_at` | TIMESTAMP | 创建时间 |

### rag.config（配置表）

| 字段名 | 类型 | 说明 |
|--------|------|------|
| `key` | TEXT | 配置键（主键） |
| `value` | TEXT | 配置值 |
| `updated_at` | TIMESTAMP | 更新时间 |

## 配置项列表

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `chunk_size` | 1200 | 分块大小（字符） |
| `chunk_overlap` | 200 | 分块重叠（字符） |
| `timeout_ms` | 30000 | API 超时（毫秒） |
| `system_prompt` | You are a helpful... | 系统提示词 |
| `embedding_model` | text-embedding-3-small | 嵌入模型 |
| `llm_model` | gpt-4o-mini | LLM 模型 |
| `on_error` | skip | 错误处理策略 |

## 项目结构

```
pg_rag/
├── pg_rag.control          # 扩展控制文件
├── Makefile
├── sql/
│   └── pg_rag--0.1.0.sql   # SQL 安装脚本
├── src/
│   ├── pg_rag.h            # 主头文件
│   ├── pg_rag.c            # 主入口 + SQL 函数
│   ├── config.c            # 配置管理
│   ├── kb.c                # 知识库管理
│   ├── chunk.c             # 文本分块
│   ├── http.c              # HTTP 客户端 (popen + curl)
│   ├── embedding.c         # Embedding API
│   ├── llm.c               # LLM Chat API
│   ├── retrieve.c          # pgvector 检索
│   └── utils.c             # 工具函数
└── README.md
```

## 分块策略

采用滑动窗口算法：

```
chunk_size = 1200        -- 字符
chunk_overlap = 200      -- 字符

chunk1: [0 - 1200]
chunk2: [1000 - 2200]    -- 重叠 200
chunk3: [2000 - 3200]    -- 重叠 200
```

优势：
- 避免句子被截断导致语义断裂
- 相邻 chunks 有上下文关联

## 错误处理

### rag.insert_document 错误策略

- `skip` (默认): 跳过失败的 chunk，继续处理其他 chunks
- `fail`: 任一 chunk 失败则整体 rollback

```sql
-- 使用默认策略（skip）
SELECT rag.insert_document('docs', 'content...');

-- 显式指定策略
SELECT rag.insert_document('docs', 'content...', on_error => 'fail');
```

### rag.query 错误处理

- embedding 失败: 报错（查询无法继续）
- LLM 失败: 返回 sources，answer 为 null

## Roadmap

- [ ] Hybrid Search (全文 + 向量)
- [ ] Rerank 支持
- [ ] 流式输出
- [ ] Embedding 缓存
- [ ] 多模型支持

## License

本项目采用自定义许可协议。非商业用途免费使用，商业用途需单独授权。
详见 [LICENSE](LICENSE) 文件。
