-- ============================================
-- pg_rag regression test
-- ============================================
-- This test validates core functionality without requiring external API calls

-- Load required extensions
CREATE EXTENSION IF NOT EXISTS vector;
CREATE EXTENSION IF NOT EXISTS pg_rag;

-- ============================================
-- Test 1: Configuration Management
-- ============================================
SELECT '=== Test 1: Configuration Management ===' AS section;

-- Test setting and getting config values
SELECT rag.set_config('test_key', 'test_value');
SELECT rag.get_config('test_key');

-- Test updating existing config
SELECT rag.set_config('test_key', 'updated_value');
SELECT rag.get_config('test_key');

-- Test getting non-existent config (should return NULL)
SELECT rag.get_config('non_existent_key');

-- Test default configs exist
SELECT key, value FROM rag.config WHERE key != 'test_key' ORDER BY key;

-- ============================================
-- Test 2: Knowledge Base Management
-- ============================================
SELECT '=== Test 2: Knowledge Base Management ===' AS section;

-- Clean up test KBs if they exist from previous runs
DELETE FROM rag.documents WHERE kb_id IN (SELECT id FROM rag.knowledge_bases WHERE name LIKE 'test_%');
DELETE FROM rag.knowledge_bases WHERE name LIKE 'test_%';

-- Test creating a knowledge base
SELECT rag.create_kb('test_kb_1') AS kb_id_1;

-- Test creating KB with custom dimension
SELECT rag.create_kb('test_kb_2', 768) AS kb_id_2;

-- Verify KB creation
SELECT name, embedding_dim FROM rag.knowledge_bases WHERE name LIKE 'test_kb_%' ORDER BY name;

-- Test creating duplicate KB (should fail with unique violation)
DO $$
BEGIN
    PERFORM rag.create_kb('test_kb_1');
    RAISE NOTICE 'ERROR: Duplicate KB creation should have failed';
EXCEPTION
    WHEN unique_violation THEN
        RAISE NOTICE 'OK: Duplicate KB correctly rejected';
END $$;

-- ============================================
-- Test 3: Schema and Table Verification
-- ============================================
SELECT '=== Test 3: Schema and Table Verification ===' AS section;

-- Verify schema exists
SELECT schema_name FROM information_schema.schemata WHERE schema_name = 'rag';

-- Verify tables exist
SELECT table_name FROM information_schema.tables WHERE table_schema = 'rag' ORDER BY table_name;

-- Verify columns in knowledge_bases
SELECT column_name, data_type FROM information_schema.columns
WHERE table_schema = 'rag' AND table_name = 'knowledge_bases' ORDER BY ordinal_position;

-- Verify columns in documents (embedding shows as USER-DEFINED)
SELECT column_name, data_type FROM information_schema.columns
WHERE table_schema = 'rag' AND table_name = 'documents' ORDER BY ordinal_position;

-- Verify indexes exist
SELECT indexname FROM pg_indexes WHERE schemaname = 'rag' ORDER BY indexname;

-- ============================================
-- Test 4: Return Types Verification
-- ============================================
SELECT '=== Test 4: Return Types Verification ===' AS section;

-- Verify custom types exist
SELECT typname FROM pg_type WHERE typname IN ('query_result', 'explain_result') ORDER BY typname;

-- Verify composite type structure for query_result
SELECT a.attname, t.typname
FROM pg_type ty
JOIN pg_attribute a ON a.attrelid = ty.typrelid
JOIN pg_type t ON a.atttypid = t.oid
WHERE ty.typname = 'query_result' AND a.attnum > 0
ORDER BY a.attnum;

-- Verify composite type structure for explain_result
SELECT a.attname, t.typname
FROM pg_type ty
JOIN pg_attribute a ON a.attrelid = ty.typrelid
JOIN pg_type t ON a.atttypid = t.oid
WHERE ty.typname = 'explain_result' AND a.attnum > 0
ORDER BY a.attnum;

-- ============================================
-- Test 5: GUC Variables
-- ============================================
SELECT '=== Test 5: GUC Variables ===' AS section;

-- Show GUC settings (without values since they're sensitive)
SELECT name, setting FROM pg_settings WHERE name LIKE 'rag.%' ORDER BY name;

-- Set GUC values (these are session-level)
SET rag.embedding_api_url = 'https://test.example.com/embeddings';
SET rag.llm_api_url = 'https://test.example.com/chat';
SET rag.llm_temperature = 0.5;

-- Verify GUC values
SELECT name, setting FROM pg_settings WHERE name LIKE 'rag.%' ORDER BY name;

-- Reset GUC values
RESET rag.embedding_api_url;
RESET rag.llm_api_url;
RESET rag.llm_temperature;

-- ============================================
-- Test 6: Error Handling - Invalid Inputs
-- ============================================
SELECT '=== Test 6: Error Handling ===' AS section;

-- Test query on non-existent knowledge base (should error)
DO $$
BEGIN
    PERFORM rag.explain('non_existent_kb', 'test query', 5);
EXCEPTION
    WHEN OTHERS THEN
        RAISE NOTICE 'Expected error for non-existent KB: %', SQLERRM;
END $$;

-- ============================================
-- Test 7: Cleanup
-- ============================================
SELECT '=== Test 7: Cleanup ===' AS section;

-- Clean up test data
DELETE FROM rag.documents WHERE kb_id IN (SELECT id FROM rag.knowledge_bases WHERE name LIKE 'test_%');
DELETE FROM rag.knowledge_bases WHERE name LIKE 'test_%';
DELETE FROM rag.config WHERE key = 'test_key';

-- Verify cleanup
SELECT COUNT(*) AS remaining_test_kbs FROM rag.knowledge_bases WHERE name LIKE 'test_%';

-- ============================================
-- Test Summary
-- ============================================
SELECT '=== All regression tests completed ===' AS summary;
