"""Read-only snapshot of public Qianwen response schemas; no model submissions.

Input: an already downloaded official docs index/full text and live model IDs.
Output: versioned public schemas and mechanically extracted evidence, not a
claim that the account can invoke every advertised model.
"""
import concurrent.futures
import hashlib
import json
import re
import urllib.request
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parents[1] / "evidence/plan-20261103/model-usage-audit"
SCHEMAS = ROOT / "schemas"
DOC_ORIGIN = "https://platform.qianwenai.com/docs/"


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def walk(value, path=()):
    yield path, value
    if isinstance(value, dict):
        for key, child in value.items():
            yield from walk(child, path + (key,))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk(child, path + (str(index),))


def valid_model(value):
    return (isinstance(value, str) and 2 < len(value) < 130
            and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_./:+-]*", value)
            and value.lower() not in {"string", "model", "model-name", "your-model", "xxx"})


def model_values(node):
    if isinstance(node, str):
        return [node] if valid_model(node) else []
    if not isinstance(node, dict):
        return []
    result = []
    for key in ("enum", "default", "example", "const"):
        value = node.get(key)
        for entry in value if isinstance(value, list) else [value]:
            if valid_model(entry):
                result.append(entry)
    return result


def fetch(url):
    name = url.rsplit("/", 1)[-1]
    path = SCHEMAS / name
    try:
        if path.exists():
            raw = path.read_bytes()
            json.loads(raw)
            status = 200
        else:
            response = requests.get(url, timeout=35, allow_redirects=False,
                                    proxies=urllib.request.getproxies())
            status = response.status_code
            if status != 200:
                return {"url": url, "status": status}
            raw = response.content
            json.loads(raw)
            path.write_bytes(raw)
        return {"url": url, "status": status, "path": str(path.relative_to(ROOT)),
                "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}
    except (ValueError, OSError, requests.RequestException) as error:
        return {"url": url, "error": type(error).__name__}


def resolve(schema, root, seen=()):
    if not isinstance(schema, dict):
        return {}
    ref = schema.get("$ref")
    if ref and ref.startswith("#/") and ref not in seen:
        value = root
        for key in ref[2:].split("/"):
            value = value.get(key.replace("~1", "/").replace("~0", "~"), {})
        return resolve(value, root, seen + (ref,))
    return schema


def response_fields(schema, root, path="", depth=0):
    """Follow only response schemas; return optional documented usage leaves."""
    if depth > 18:
        return []
    node = resolve(schema, root)
    result = []
    for kind in ("oneOf", "anyOf", "allOf"):
        for alternative in node.get(kind, []):
            result.extend(response_fields(alternative, root, path, depth + 1))
    if node.get("type") == "array":
        result.extend(response_fields(node.get("items", {}), root, path + "[]", depth + 1))
    props = node.get("properties", {})
    for name, child in props.items():
        result.extend(response_fields(child, root, (path + "." if path else "") + name, depth + 1))
    if not props and path and any(x in path.split(".") for x in ("usage", "billing", "cost", "credits", "extra_info")):
        if node.get("type") not in ("object", "array"):
            result.append({"path": path, "type": node.get("type"), "description": node.get("description", "")})
    return result


def build_matrix(extracted):
    pages = json.loads((ROOT / "pages.json").read_text(encoding="utf-8"))
    by_url = {x["url"]: x for x in pages}
    price_source = DOC_ORIGIN + "developer-guides/getting-started/model-prices"
    price_text = by_url[price_source]["text"]
    group, header, previous = "", [], []
    prices = {}
    for line in price_text.splitlines():
        if line.startswith("#"):
            group = re.sub(r"\s+\{#.*", "", line.lstrip("# "))
            header, previous = [], []
        if not line.startswith("|"):
            continue
        cells = [v.strip() for v in re.split(r"(?<!\\)\|", line)[1:-1]]
        if cells and "模型" in cells[0] and not "`" in cells[0]:
            header, previous = cells, []
            continue
        ids = [v for v in re.findall(r"`([^`]+)`", cells[0]) if valid_model(v)] if cells else []
        if cells and "sambert-*" in cells[0]:
            ids = ["sambert-*"]
        if not ids and cells and not cells[0]:
            ids = previous
        if ids:
            previous = ids
            for model in ids:
                prices.setdefault(model, []).append({"section": group, "columns": header, "values": cells, "source": price_source})

    protocols = {}
    for item in extracted:
        key = item["source"].rsplit("/", 1)[-1].removeprefix("openapi-").removesuffix(".json")
        raw = json.loads((SCHEMAS / item["source"].rsplit("/", 1)[-1]).read_text(encoding="utf-8"))
        fields, timing = {}, set()
        for route, path in raw.get("paths", {}).items():
            if not isinstance(path, dict):
                continue
            for method, op in path.items():
                if not isinstance(op, dict):
                    continue
                for code, response in op.get("responses", {}).items():
                    if not str(code).startswith("2"):
                        continue
                    response = resolve(response, raw)
                    for media, value in response.get("content", {}).items():
                        timing.add("stream_terminal_usage" if "event-stream" in media else
                                   "task_poll_terminal" if "task" in route.lower() and method == "get" else "response_body")
                        for f in response_fields(value.get("schema", {}), raw):
                            fields.setdefault(f["path"], f)
        # Supplement examples only when the formal response schema omits leaves.
        for item2 in item["usage_candidates"]:
            node = item2["definition"]
            if "responses/" not in item2["schema_path"] or not isinstance(node, dict):
                continue
            if not any(part in item2["schema_path"] for part in ("/example/", "/examples/")):
                continue
            schema_path = item2["schema_path"]
            prefix = (schema_path.rsplit("/value/", 1)[-1] if "/value/" in schema_path
                      else schema_path.rsplit("/example/", 1)[-1]).replace("/", ".")
            for path, val in walk(node):
                if path and not isinstance(val, (dict, list)):
                    name = prefix + "." + ".".join(path)
                    fields.setdefault(name, {"path": name, "type": type(val).__name__, "description": "Response example; optional and not a per-model guarantee."})
        protocols[key] = {"id": key, "title": item["name"], "sources": [item["source"]],
                          "explicit_models": list(item["models"]), "operations": item["operations"],
                          "fields": list(fields.values()), "timing": sorted(timing),
                          "verification": "official_schema_not_live_call"}

    def add_doc(key, suffix, fields, timing, note):
        url = DOC_ORIGIN + "api-reference/" + suffix
        assert url in by_url, url
        protocols[key] = {"id": key, "title": by_url[url]["title"], "sources": [url],
                          "explicit_models": [], "operations": [],
                          "fields": [{"path": x, "description": note} for x in fields],
                          "timing": [timing], "verification": "official_document_not_live_call", "note": note}

    add_doc("qwen-audio-tts-ws", "speech-synthesis/qwen-audio-tts/server-events",
            ["payload.usage." + x for x in ("characters", "input_tokens", "output_tokens", "total_tokens")],
            "sentence-end_and_task-finished", "字段依模型不同；累计值覆盖，不能叠加每句和终态；3.1 plus 仍按字符价，flash 按 token 价。")
    add_doc("cosyvoice-ws", "speech-synthesis/cosyvoice/server-events", ["payload.usage.characters"],
            "sentence-end_and_task-finished", "截止当前累计字符数；最终值覆盖，不重复求和。")
    add_doc("sambert-ws", "speech-synthesis/sambert/server-events", ["payload.usage.characters"],
            "result-generated_and_task-finished", "截止当前累计字符数。")
    add_doc("qwen-tts-ws", "speech-synthesis/qwen-tts-realtime/server-events", ["response.usage.characters"],
            "response.done", "按 response ID 去重，每个响应独立计量。")
    add_doc("fun-asr-ws", "speech-recognition/fun-asr-realtime/server-events", ["payload.usage.duration"],
            "sentence_end_true", "当前任务计费秒数；句中 usage 为 null；按任务最终累计值。")
    add_doc("paraformer-ws", "speech-recognition/paraformer-realtime/server-events",
            ["payload.usage.duration", "payload.output.usage.duration"], "sentence_end_true",
            "文档示例为 payload.usage、字段表为 payload.output.usage，存在路径矛盾；兼容解析且必须实包确认，不能两者重复计量。")
    add_doc("qwen-asr-realtime-ws", "speech-recognition/qwen-asr-realtime/server-events", [],
            "usage_not_documented", "服务端事件页未定义 usage；只能记录上传音频长度为本机估算，并通过账单核验。")
    add_doc("qwen-audio-asr-streaming-ws", "speech-recognition/qwen-audio-asr-streaming/server-events",
            ["payload.usage." + x for x in ("duration", "input_tokens", "output_tokens", "total_tokens")],
            "sentence_end_and_task_terminal", "当前任务累计用量；3.0 按秒，3.1 按 token，字段不能互相折算。")
    add_doc("qwen-asr-message-ws", "speech-recognition/qwen-asr-message/server-events",
            ["payload.usage." + x for x in ("duration", "input_tokens", "output_tokens", "total_tokens")],
            "result-generated_and_task-finished", "当前任务累计用量；duration 为兼容字段，3.1 message 按输入/输出 token 计费。")
    token_fields = ["response.usage." + x for x in ("input_tokens", "output_tokens", "total_tokens", "input_tokens_details", "output_tokens_details")]
    add_doc("qwen-audio-realtime-ws", "qwen-audio-realtime/server-events", token_fields + ["response.usage.plugins.search.count"],
            "response.done_completed", "按响应去重；被取消响应可能无 usage；搜索 count 不是实际扣费次数。")
    add_doc("omni-realtime-ws", "real-time-multimodal/server-events", token_fields + ["response.usage.plugins.search.count"],
            "response.done", "按响应去重；音频/文本分项不能与合计重复相加。")
    add_doc("translate-realtime-ws", "speech-translation/livetranslate-realtime/server-events", token_fields,
            "response.done", "按响应去重；保留输入/输出以及模态分项。")
    add_doc("multimodal-embedding", "multimodal-embedding/dashscope-multimodal-embedding",
            ["usage." + x for x in ("input_tokens", "input_tokens_details.image_tokens", "input_tokens_details.text_tokens", "output_tokens", "total_tokens", "image_tokens")],
            "response_body", "tongyi-embedding-vision 使用 input_tokens_details.image_tokens；qwen VL/multimodal-embedding-v1 使用顶层 image_tokens，不混算。")
    add_doc("text-embedding-native", "text-embedding/dashscope-embedding", ["usage.total_tokens"],
            "response_body", "原生接口字段与 OpenAI 兼容不同。")
    add_doc("rerank-native", "rerank/dashscope-rerank", ["usage.prompt_tokens", "usage.total_tokens"],
            "response_body", "prompt_tokens 仅 qwen3.7-text-rerank 返回；total_tokens 已是总量。")

    models = {}
    def entry(model):
        return models.setdefault(model, {"id": model, "inventory_sources": [], "protocols": [],
                                        "protocol_basis": {}, "pricing_rows": [], "gaps": []})
    def bind(model, key, basis):
        assert key in protocols, key
        value = entry(model)
        if key not in value["protocols"]:
            value["protocols"].append(key)
        value["protocol_basis"][key] = basis

    live = json.loads((ROOT / "live-models.json").read_text(encoding="utf-8"))
    for model in live["ids"]:
        entry(model)["inventory_sources"].append("live_compatible_models_endpoint")
    for model, rows in prices.items():
        if model == "sambert-*":
            continue
        value = entry(model)
        value["inventory_sources"].append("official_price_table")
        value["pricing_rows"] = rows
    for key, protocol in protocols.items():
        for model in protocol["explicit_models"]:
            entry(model)["inventory_sources"].append("official_openapi_model_literal")
            bind(model, key, "explicit_model_in_schema_or_example")
    catalog = json.loads((ROOT.parents[2] / "components/p4_board/qianwen_catalog.json").read_text(encoding="utf-8"))
    for cap in catalog["capabilities"].values():
        for model in cap["models"]:
            entry(model)["inventory_sources"].append("existing_device_capability_snapshot")
    sambert_page = by_url[DOC_ORIGIN + "api-reference/speech-synthesis/sambert/java-sdk"]
    for model in sorted(set(re.findall(r"\bsambert-[A-Za-z0-9_-]+-v1\b", sambert_page["text"]))):
        value = entry(model)
        value["inventory_sources"].append("official_sambert_model_table")
        value["pricing_rows"] = prices.get("sambert-*", [])
        bind(model, "sambert-ws", "explicit_sambert_model_table_and_shared_protocol")

    for model, value in sorted(models.items()):
        sections = {x["section"] for x in value["pricing_rows"]}
        family = "documented_family_protocol_not_individual_receipt"
        if "文本生成" in sections and not value["protocols"]:
            bind(model, "openai-chat", "official_text_model_price_category_and_shared_chat_contract")
        if model.startswith("sambert-"):
            pass
        elif model.startswith("cosyvoice-"):
            bind(model, "cosyvoice-ws", family)
            bind(model, "cosyvoice-tts-http-api", family)
        elif model.startswith("qwen-audio-") and "tts" in model:
            if model.endswith("tts-next"):
                bind(model, "audio-generation-api", family)
            else:
                bind(model, "qwen-audio-tts-ws", family)
                bind(model, "qwen-audio-tts-http-api", family)
        elif model.startswith("qwen3-tts-"):
            bind(model, "qwen-tts-ws" if "realtime" in model else "qwen-tts-api", family)
        elif model.startswith("MiniMax/speech-"):
            bind(model, "minimax-speech-synthesis", family)
        elif model.startswith("qwen-audio-") and "asr" in model:
            key = "qwen-audio-asr-streaming-ws" if "streaming" in model else "qwen-audio-asr-filetrans-http-api" if "filetrans" in model else "qwen-audio-asr-http-api"
            if "message" not in model:
                bind(model, key, family)
            else:
                bind(model, "qwen-asr-message-ws", family)
        elif model.startswith("fun-asr"):
            bind(model, "fun-asr-ws" if "realtime" in model else "fun-asr-flash-recorded-speech-recognition-http-api" if "flash" in model else "fun-asr-recorded-speech-recognition-http-api", family)
        elif model.startswith("paraformer"):
            bind(model, "paraformer-ws" if "realtime" in model else "paraformer-recorded-speech-recognition-restful-api", family)
        elif model.startswith("qwen3-asr"):
            bind(model, "qwen-asr-realtime-ws" if "realtime" in model else "qwen-asr-api-reference", family)
        elif model.startswith("qwen-audio-") and "realtime" in model:
            bind(model, "qwen-audio-realtime-ws", family)
        elif "livetranslate" in model:
            bind(model, "translate-realtime-ws" if "realtime" in model else "qwen3-livetranslate-flash-api", family)
        elif "omni" in model and model.startswith("qwen"):
            bind(model, "omni-realtime-ws" if "realtime" in model else "openai-chat", family)
        elif "embedding" in model:
            if any(word in model for word in ("vision", "multimodal", "-vl-")):
                bind(model, "multimodal-embedding", family)
            elif "async" in model:
                value["gaps"].append("异步 embedding 需确认结果文件/任务计量位置，不能仅套用同步回执。")
            else:
                bind(model, "text-embedding", family)
                bind(model, "text-embedding-native", family)
        elif "rerank" in model:
            bind(model, "reranking", family)
            bind(model, "rerank-native", family)
        if model == "happyoyster-1.0-adventure":
            bind(model, "happyoyster-adventure", family)
            value["gaps"].append("公开 World/RTC REST 回执没有标准 usage；创建按次、体验按秒，精确计费用量需管理账单。")
        if model == "qwen-audio-3.1-asr-flash-filetrans":
            value["gaps"].append("价格表按输入/输出 token，当前该 OpenAPI 仅定义 duration；不足以据此计算完整费用。")
        if model in ("qwen-image-edit-max", "qwen-image-edit-plus", "qwen-mt-turbo", "qwen-voice-design", "MiniMax-M3", "qwen3-tts-instruct-flash"):
            value["gaps"].append("现有设备目录条目；当前价格表/专用 schema 未明确同名 ID，需核对别名或下线状态。")
        if not value["protocols"]:
            value["gaps"].append("没有明确绑定的当前回执协议；不根据名称假定字段。")
        if not value["pricing_rows"]:
            value["gaps"].append("全量价格表未提供该精确 ID 单价；费用保持未知。")
        value["inventory_sources"] = sorted(set(value["inventory_sources"]))
        value["protocols"] = sorted(set(value["protocols"]))
        value["receipt_fields"] = sorted({f["path"] for key in value["protocols"] for f in protocols[key]["fields"]})
        value["billing_dimensions"] = sorted({c for row in value["pricing_rows"] for c in row["columns"][1:] if "元/" in c or "计费" in c})
        value["returned_money"] = "no_CNY_amount_documented_in_matched_receipts"
        value["availability"] = "advertised_or_documented_not_permission_tested"
        value["verification"] = "documentation_evaluation_only"
        if not value["receipt_fields"]:
            value["gaps"].append("匹配协议未公开可直接取得的用量字段；缺失不记零。")
        elif any("Token" in x for x in value["billing_dimensions"]) and not any("token" in x for x in value["receipt_fields"]):
            value["gaps"].append("价格含 token 维度，但匹配回执未公开 token 数；不能据时长/张数直接换算费用。")
    save(ROOT / "protocol-receipts.json", protocols)
    save(ROOT / "all-models-usage.json", {"snapshot_utc": live["retrieved_utc"],
         "scope": "union of live compatibility IDs, official price rows, OpenAPI model literals, Sambert model table and existing device catalog",
         "models": [models[k] for k in sorted(models)], "price_source": price_source,
         "prices_are_catalog_estimates_not_account_bills": True})
    counts = {"model_ids": len(models), "live_compatibility_ids": len(live["ids"]),
              "price_table_model_ids": len(prices) - ("sambert-*" in prices),
              "openapi_documents": len(extracted), "protocols": len(protocols),
              "with_documented_usage_fields": sum(bool(x["receipt_fields"]) for x in models.values()),
              "no_documented_usage_fields": sum(not x["receipt_fields"] for x in models.values()),
              "with_catalog_price_rows": sum(bool(x["pricing_rows"]) for x in models.values()),
              "no_catalog_price_rows": sum(not x["pricing_rows"] for x in models.values()),
              "paid_model_calls_for_audit": 0}
    save(ROOT / "coverage.json", counts)
    lines = ["# 千问全模型回执与计量索引", "", "核对日：2026-11-03。文档评估；未逐个付费调用、未逐个验证账户权限。",
             "", "包含当前接口、价格表及文档中的旧版本/别名；收录不等于当前可用。表内字段为匹配协议的可选字段集合，具体模型条件、原始字段说明、全部阶梯价行与来源见 JSON。未知用量不按零计。",
             "", "完整来源与统计：[coverage.json](coverage.json)、[protocol-receipts.json](protocol-receipts.json)、[all-models-usage.json](all-models-usage.json)。",
             "", "| 模型 ID | 目录来源 | 回执协议 | 回执字段 | 价格维度 | 待确认项 |", "|---|---|---|---|---|---|"]
    source_labels = {"live_compatible_models_endpoint": "实时目录", "official_price_table": "价格表", "official_openapi_model_literal": "API定义", "existing_device_capability_snapshot": "设备旧目录", "official_sambert_model_table": "音色模型表"}
    for model in sorted(models):
        value = models[model]
        fields = "; ".join(value["receipt_fields"]) or "未公开/未确认"
        cells = [model, "; ".join(source_labels[x] for x in value["inventory_sources"]),
                 "; ".join(value["protocols"]) or "待确认", fields,
                 "; ".join(value["billing_dimensions"]) or "未确认", "; ".join(value["gaps"]) or "文档定义；未实包逐模型验证"]
        lines.append("| " + " | ".join(x.replace("|", "\\|").replace("\n", " ") for x in cells) + " |")
    (ROOT / "ALL_MODELS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return counts


def main():
    SCHEMAS.mkdir(parents=True, exist_ok=True)
    index = (ROOT / "docs-llms.txt").read_text(encoding="utf-8")
    urls = sorted(set(re.findall(r"https://platform\.qianwenai\.com/docs/openapi[^)\s]+\.json", index)))
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        receipts = list(pool.map(fetch, urls))
    save(ROOT / "schema-downloads.json", receipts)
    extracted = []
    for receipt in receipts:
        if "path" not in receipt:
            continue
        data = json.loads((ROOT / receipt["path"]).read_text(encoding="utf-8-sig"))
        models, fields, operations = {}, [], []
        for path, node in walk(data):
            if path and path[-1] == "model":
                for model in model_values(resolve(node, data) if isinstance(node, dict) else node):
                    models.setdefault(model, []).append("/".join(path))
            if path and path[-1] in ("usage", "billing", "cost", "credits", "extra_info", "consume"):
                fields.append({"schema_path": "/".join(path), "definition": node})
        for route, item in data.get("paths", {}).items():
            if not isinstance(item, dict):
                continue
            for method, op in item.items():
                if method not in ("get", "post", "delete", "put", "patch") or not isinstance(op, dict):
                    continue
                operations.append({"method": method.upper(), "route": route,
                                   "summary": op.get("summary"), "operationId": op.get("operationId")})
        extracted.append({"source": receipt["url"], "name": data.get("info", {}).get("title"),
                          "models": models, "operations": operations, "usage_candidates": fields})
    save(ROOT / "schema-evidence.json", extracted)
    counts = build_matrix(extracted)
    print(json.dumps({"downloaded_utc": datetime.now(timezone.utc).isoformat(),
                      "schema_urls": len(urls), "downloaded": len(extracted),
                      "errors": [x for x in receipts if "path" not in x],
                      "unique_schema_models": len(set(m for x in extracted for m in x["models"])),
                      "schemas_with_usage_candidates": sum(bool(x["usage_candidates"]) for x in extracted)},
                     ensure_ascii=False, indent=2))
    print(json.dumps(counts, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
