太好了！你已有 Python 环境和本地 LLM（通过 `llama.cpp` + Python 封装，如 `llama-cpp-python`），这非常适合构建隐私安全、可自主演化的 Agentic 系统。

我们现在进入 **阶段 1：基础框架搭建**。

---

## ✅ 阶段 1 交付内容清单

1. **Agentic Graph YAML Schema (v1.0)**  
2. **表达式解析模块（安全、支持 Jinja2 风格变量）**  
3. **基础执行引擎（ExecutionEngine + 节点类）**  
4. **工具注册机制 + 示例工具**  
5. **LLM 节点集成（适配 llama-cpp-python）**  
6. **示例 Agentic Graph（YAML）**  
7. **运行脚本与验证命令**

---

## 1️⃣ Agentic Graph Schema (v1.0)

我们使用 **JSON Schema** 来定义合法结构，便于后续验证。

📁 文件：`schemas/agentic_graph_v1.json`

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "graph": {
      "type": "object",
      "properties": {
        "name": {"type": "string"},
        "version": {"type": "string", "pattern": "^\\d+\\.\\d+$"}
      },
      "required": ["name", "version"]
    },
    "nodes": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "id": {"type": "string"},
          "type": {"type": "string", "enum": ["start", "end", "set", "llm_call", "tool_call"]},
          "next": {"type": "string"}
        },
        "required": ["id", "type"],
        "allOf": [
          {
            "if": {"properties": {"type": {"const": "set"}}},
            "then": {
              "properties": {
                "assign": {"type": "object"}
              },
              "required": ["assign"]
            }
          },
          {
            "if": {"properties": {"type": {"const": "llm_call"}}},
            "then": {
              "properties": {
                "prompt_template": {"type": "string"},
                "output_key": {"type": "string"}
              },
              "required": ["prompt_template", "output_key"]
            }
          },
          {
            "if": {"properties": {"type": {"const": "tool_call"}}},
            "then": {
              "properties": {
                "tool": {"type": "string"},
                "args": {"type": "object"},
                "output_key": {"type": "string"}
              },
              "required": ["tool", "args", "output_key"]
            }
          }
        ],
        "additionalProperties": false
      },
      "minItems": 1
    }
  },
  "required": ["graph", "nodes"]
}
```

> ✅ 支持的节点类型：
> - `start` / `end`：流程起止
> - `set`：设置变量（如 `assign: { query: "天气如何" }`）
> - `llm_call`：调用本地 LLM
> - `tool_call`：调用注册工具

---

## 2️⃣ 表达式解析模块

我们使用 **Jinja2 模板引擎**（安全模式） + **simpleeval**（可选）来解析 `{{...}}` 变量。

📁 文件：`core/expr.py`

```python
from jinja2 import Template, StrictUndefined
import re

def render_expression(template_str: str, context: dict) -> str:
    """
    安全渲染 Jinja2 风格模板，仅允许变量访问，禁止执行代码。
    示例: "{{ input.query }}" -> "北京天气"
    """
    # 简单预处理：确保是字符串
    if not isinstance(template_str, str):
        return template_str

    # 使用 StrictUndefined 避免静默失败
    try:
        template = Template(template_str, undefined=StrictUndefined)
        return template.render(**context)
    except Exception as e:
        raise ValueError(f"Failed to render expression '{template_str}': {e}")

def eval_condition(expr: str, context: dict) -> bool:
    """
    评估条件表达式（简化版）：仅支持 ==, !=, and, or, not, True/False, 数字比较
    为安全起见，本阶段仅支持通过 render_expression + 字符串判断
    后续可用 simpleeval 扩展
    """
    rendered = render_expression(expr, context).strip().lower()
    if rendered in ("true", "1", "yes"):
        return True
    elif rendered in ("false", "0", "no", ""):
        return False
    else:
        # 尝试作为 Python 表达式（谨慎！）
        # 暂不启用，阶段1仅支持布尔字符串
        raise ValueError(f"Unsupported condition format: '{rendered}' (expected true/false)")
```

> 🔒 安全说明：Jinja2 在默认配置下是安全的（无代码执行），`StrictUndefined` 可帮助发现未定义变量。

---

## 3️⃣ 基础执行引擎

📁 文件：`core/engine.py`

```python
import json
import logging
from typing import Dict, Any, Callable
from .expr import render_expression

logger = logging.getLogger(__name__)

class Node:
    def __init__(self, def_dict: Dict[str, Any]):
        self.id = def_dict["id"]
        self.type = def_dict["type"]
        self.next = def_dict.get("next")
        self.raw = def_dict

    def execute(self, context: Dict[str, Any], tools: Dict[str, Callable], llm_client) -> str:
        raise NotImplementedError

class StartNode(Node):
    def execute(self, context, tools, llm_client):
        logger.info("▶ Start")
        return self.next

class EndNode(Node):
    def execute(self, context, tools, llm_client):
        logger.info("⏹ End")
        return None

class SetNode(Node):
    def execute(self, context, tools, llm_client):
        assign = self.raw["assign"]
        for key, value in assign.items():
            rendered = render_expression(value, context)
            context[key] = rendered
            logger.info(f"SET {key} = {rendered}")
        return self.next

class LLMCallNode(Node):
    def execute(self, context, tools, llm_client):
        prompt_tmpl = self.raw["prompt_template"]
        output_key = self.raw["output_key"]
        prompt = render_expression(prompt_tmpl, context)
        
        logger.info(f"🧠 LLM Prompt: {prompt[:100]}...")
        response = llm_client(prompt)
        context[output_key] = response
        logger.info(f"💬 LLM Response ({output_key}): {response[:100]}...")
        return self.next

class ToolCallNode(Node):
    def execute(self, context, tools, llm_client):
        tool_name = self.raw["tool"]
        args_tmpl = self.raw["args"]
        output_key = self.raw["output_key"]

        if tool_name not in tools:
            raise ValueError(f"Tool '{tool_name}' not registered")

        # 渲染参数
        args = {}
        for k, v in args_tmpl.items():
            args[k] = render_expression(v, context)

        logger.info(f"🛠️ Calling tool: {tool_name} with {args}")
        try:
            result = tools[tool_name](**args)
            context[output_key] = result
            logger.info(f"✅ Tool result ({output_key}): {str(result)[:100]}...")
        except Exception as e:
            logger.error(f"❌ Tool failed: {e}")
            raise
        return self.next

NODE_TYPES = {
    "start": StartNode,
    "end": EndNode,
    "set": SetNode,
    "llm_call": LLMCallNode,
    "tool_call": ToolCallNode,
}

class ExecutionEngine:
    def __init__(self, graph_def: dict, tools: dict, llm_client: Callable[[str], str]):
        self.graph_def = graph_def
        self.tools = tools
        self.llm_client = llm_client
        self.context = {}
        self.node_map = {node["id"]: node for node in graph_def["nodes"]}

    def run(self):
        current_id = "start"
        step = 0
        max_steps = 50  # 防止无限循环

        while current_id and step < max_steps:
            node_def = self.node_map.get(current_id)
            if not node_def:
                raise ValueError(f"Node '{current_id}' not found")

            node_type = node_def["type"]
            node_cls = NODE_TYPES.get(node_type)
            if not node_cls:
                raise ValueError(f"Unsupported node type: {node_type}")

            node = node_cls(node_def)
            try:
                next_id = node.execute(self.context, self.tools, self.llm_client)
                current_id = next_id
                step += 1
            except Exception as e:
                logger.exception(f"Execution failed at node '{current_id}': {e}")
                raise

        if step >= max_steps:
            logger.warning("Max steps reached, terminating.")
```

---

## 4️⃣ 工具注册 + 示例工具

📁 文件：`tools/__init__.py`

```python
def web_search(query: str) -> str:
    # 模拟搜索（后续可替换为真实 API）
    return f"Mock search result for: {query}"

def code_interpreter(code: str) -> str:
    # ⚠️ 仅用于演示！生产环境需沙箱
    try:
        # 简单表达式求值
        result = eval(code, {"__builtins__": {}}, {})
        return str(result)
    except Exception as e:
        return f"Error: {e}"

# 工具注册表
TOOLS = {
    "web_search": web_search,
    "code_interpreter": code_interpreter,
}
```

> 🔒 注意：`code_interpreter` 在阶段 1 仅为演示，**切勿用于真实用户输入**。后续可用 `asteval` 或 Docker 沙箱替代。

---

## 5️⃣ LLM 客户端（适配 llama-cpp-python）

假设你使用 [`llama-cpp-python`](https://github.com/abetlen/llama-cpp-python)：

```bash
pip install llama-cpp-python
```

📁 文件：`llm/llama_client.py`

```python
from llama_cpp import Llama

# 初始化模型（根据你的路径调整）
LLM = Llama(
    model_path="./models/gguf-model.bin",  # 替换为你的 .gguf 文件路径
    n_ctx=2048,
    n_threads=4,
    verbose=False
)

def llm_generate(prompt: str, max_tokens=256) -> str:
    output = LLM(
        prompt,
        max_tokens=max_tokens,
        stop=["\n", "###", "User:"],
        echo=False
    )
    return output["choices"][0]["text"].strip()
```

> 💡 提示：你可以将 `llm_generate` 封装为可配置函数，支持不同模型/参数。

---

## 6️⃣ 示例 Agentic Graph

📁 文件：`examples/weather_agent.yaml`

```yaml
graph:
  name: "Weather Agent"
  version: "1.0"

nodes:
  - id: start
    type: start
    next: set_query

  - id: set_query
    type: set
    assign:
      user_request: "What's the weather in Beijing today?"
      query: "weather in Beijing"
    next: search

  - id: search
    type: tool_call
    tool: web_search
    args:
      query: "{{query}}"
    output_key: search_result
    next: summarize

  - id: summarize
    type: llm_call
    prompt_template: "Summarize this search result in one sentence: {{search_result}}"
    output_key: final_answer
    next: end

  - id: end
    type: end
```

---

## 7️⃣ 运行脚本

📁 文件：`run_example.py`

```python
import yaml
import logging
from core.engine import ExecutionEngine
from tools import TOOLS
from llm.llama_client import llm_generate

# 设置日志
logging.basicConfig(level=logging.INFO)

def main():
    with open("examples/weather_agent.yaml") as f:
        graph = yaml.safe_load(f)

    engine = ExecutionEngine(
        graph_def=graph,
        tools=TOOLS,
        llm_client=llm_generate
    )
    engine.run()

    print("\n✅ Final context:")
    print(engine.context)

if __name__ == "__main__":
    main()
```

---

## ▶️ 如何运行

```bash
# 安装依赖
pip install pyyaml jinja2 llama-cpp-python

# 修改 llama_client.py 中的 model_path 为你本地的 .gguf 模型路径

# 运行示例
python run_example.py
```

---

## ✅ 阶段 1 验证点

- [ ] YAML 能被正确加载
- [ ] `set` 节点正确设置变量
- [ ] `tool_call` 调用 mock 工具并存入 context
- [ ] `llm_call` 调用本地 LLM 并返回结果
- [ ] 执行按 `next` 顺序流转
- [ ] 日志清晰显示每步操作

---

## 🚀 下一步

一旦你成功运行阶段 1，我们可以：
1. 添加 **JSON Schema 验证**（使用 `jsonschema` 库）
2. 增强 **表达式支持**（如 `{{ len(results) > 0 }}`）
3. 进入 **阶段 2：条件分支与循环**

请尝试运行，并告诉我是否遇到问题（尤其是 llama.cpp 模型加载部分）。我可以帮你调整 LLM 适配器。
