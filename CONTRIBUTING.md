# 贡献指南

感谢您对 pg_rag 项目的关注！本文档将帮助您了解如何参与项目贡献。

## 行为准则

参与本项目即表示您同意遵守以下准则：
- 尊重所有参与者
- 接受建设性批评
- 以项目最佳利益为出发点

## 如何贡献

### 报告问题 (Bug Report)

如果您发现了问题，请通过 GitHub Issues 提交，并包含以下信息：
- 问题描述（清晰简洁）
- 复现步骤
- 期望行为 vs 实际行为
- 环境信息：
  - PostgreSQL 版本
  - 操作系统
  - pg_rag 版本
- 相关日志或错误信息

### 提交功能请求 (Feature Request)

欢迎提出新功能建议！请描述：
- 功能的用途和场景
- 期望的 API 或行为
- 可能的实现方案（可选）

### 提交代码 (Pull Request)

1. **Fork 仓库** 并创建您的分支
   ```bash
   git checkout -b feature/my-feature
   # 或
   git checkout -b fix/my-bugfix
   ```

2. **开发规范**
   - 代码遵循 PostgreSQL C 代码规范
   - 添加适当的注释
   - 更新相关文档

3. **测试要求**
   - 确保 `make installcheck` 通过
   - 如需测试需要 API Key 的功能，请提供测试说明

4. **提交更改**
   ```bash
   git add .
   git commit -m "描述您的更改"
   git push origin feature/my-feature
   ```

5. **创建 Pull Request**
   - 描述 PR 的目的和变更内容
   - 关联相关的 Issue（如有）

## 开发环境设置

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt-get install -y postgresql-server-dev-XX postgresql-XX  # XX 为版本号
sudo apt-get install -y libcurl4-openssl-dev

# 安装 pgvector
git clone https://github.com/pgvector/pgvector.git
cd pgvector && make && sudo make install
```

### 编译安装

```bash
cd pg_rag
make
sudo make install
```

### 运行测试

```bash
# 回归测试（无需 API Key）
make installcheck

# 完整功能测试（需要配置 API Key）
psql -d your_db -f test/test_pg_rag_full.sql
```

## 代码结构

```
src/
├── pg_rag.c      # 主入口和 SQL 函数
├── pg_rag.h      # 头文件
├── config.c      # 配置管理
├── kb.c          # 知识库管理
├── chunk.c       # 文本分块
├── http.c        # HTTP 客户端
├── embedding.c   # Embedding API
├── llm.c         # LLM Chat API
├── retrieve.c    # 向量检索
└── utils.c       # 工具函数
```

## 提交信息规范

建议遵循 [Conventional Commits](https://www.conventionalcommits.org/):

- `feat:` 新功能
- `fix:` 修复问题
- `docs:` 文档更新
- `test:` 测试相关
- `refactor:` 代码重构
- `perf:` 性能优化
- `chore:` 其他杂项

示例：
```
feat: 添加对 OpenAI GPT-4 的支持

- 新增 rag.llm_model 配置项
- 更新 llm.c 支持不同模型的请求格式
```

## 版本发布

版本号遵循 [SemVer](https://semver.org/lang/zh-CN/)：
- `MAJOR.MINOR.PATCH`
- 例如：`0.1.0`

## 许可证

通过提交 PR，您同意您的代码将按照项目的 [LICENSE](../LICENSE) 进行许可。

## 联系我们

如有疑问，欢迎通过以下方式联系：
- GitHub Issues
- 邮箱：a_jimider@163.com

---

感谢您对 pg_rag 的贡献！
