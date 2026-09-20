# logs/ - AI Coding 日志目录

存放开发期与 AI 工具的对话日志，随作品代码一并提交。

## 目录结构

```
logs/
├── README.md
└── XWMU/                # 队内开发者 GitHub 登录名，一人一目录
    ├── manifest.json     # 会话清单（官方日志归集工具生成）
    └── <date>/           # 日期 YYYY-MM-DD
        └── <tool>__<sid>.jsonl   # 每会话一个文件
```

> 真实 JSONL 需由组委会提供的日志归集工具从 AI 工具本机 staging 导出，请按
> 《参赛代码提交指南》与《AI Coding 日志归集与提交手册》执行后补充提交。
> 开发过程（拆需求 / 方案 / 编码 / 调试 / 文档）与 AI 协作的说明见根 README 第五节。