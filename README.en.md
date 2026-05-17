# transformers-bnlang

Bnlang transformers — pipeline API on top of [onnxruntime-bnlang](../onnxruntime-bnlang) (and optionally [onnxruntime-genai-bnlang](../onnxruntime-genai-bnlang) for fast LLM generation).

v0.1.0 supports **text generation**.

> Unofficial third-party binding. Not affiliated with Hugging Face or Microsoft.

*এই README-এর বাংলা সংস্করণ — [README.md](README.md)*

## Quick start

```bnl
import "transformers-bnlang" as bt;

var gen = bt.pipeline("text-generation", "./models/Qwen2.5-0.5B-Instruct");
var out = gen.run("Hello", {
    max_new_tokens: 100,
    temperature:    0.7,
    top_k:          50,
    do_sample:      true
});
print(out["generated_text"]);
gen.close();
```

## Two engines, one public API

```bnl
// Default: our hand-rolled native loop. Runs ANY decoder-only ONNX
// model (Qwen, Llama, Mistral, Phi, …). Reads standard HF layout
// (config.json, tokenizer.json, onnx/model.onnx).
bt.pipeline("text-generation", "./models/Qwen2.5-0.5B-Instruct");

// Fast path: routes through onnxruntime-genai (Microsoft's
// specialized LLM runtime). ~10× faster, requires a model dir
// with genai_config.json. Same run/chat/close API.
bt.pipeline("text-generation", "./models/qwen-0.5b-dml-int4",
            { engine: "onnxruntime-genai" });
```

## Exports

| Name | Kind |
|---|---|
| `version` | string |
| `pipeline(task, model_dir, options)` | factory → pipeline object |
| `AutoTokenizer.from_pretrained(dir)` | factory → tokenizer |
| `AutoTokenizer.from_file(path)` | factory → tokenizer |
| `tok.encode(text)` | function → list of ids |
| `tok.decode(ids)` | function → string |
| `tok.special_id(name)` | function → int |
| `tok.close()` | function |
| `gen.run(text, opts)` | function → result map |
| `gen.chat(messages, opts)` | function → result map |
| `gen.close()` | function |
| `register_architecture(name, descriptor)` | function — add a new architecture |

## Pipeline options

| Option | Type | Default | Meaning |
|---|---|---|---|
| `model` | string | `onnx/model.onnx` | path to the ONNX file (relative or absolute) |
| `config` | string | `config.json` | path to the model config |
| `tokenizer` | string | `tokenizer.json` | path to the tokenizer config |
| `architecture` | map | (read from config) | per-arch descriptor: layers, KV heads, EOS, chat template, … |
| `engine` | string | (our loop) | `"onnxruntime-genai"` to use the fast path |
| `execution_providers` | list | `["CPU"]` | ORT EPs to try in order, e.g. `["DML", "CPU"]` |
| `log_severity_level` | int | `3` | ORT log verbosity (0=verbose … 4=fatal) |

## Run-time options (`gen.run(text, opts)`)

| Option | Default | Meaning |
|---|---|---|
| `max_new_tokens` | 32 | tokens to generate after the prompt |
| `do_sample` | `false` | `true` enables temperature / top-k / top-p sampling |
| `temperature` | `1.0` | softmax temperature |
| `top_k` | `0` (off) | restrict to top-K candidates |
| `top_p` | `1.0` (off) | nucleus filter |
| `seed` | `0` | non-zero reseeds the sampler |

## Chat

```bnl
var out = gen.chat([
    { role: "system", content: "You are a helpful assistant." },
    { role: "user",   content: "What is the capital of Bangladesh?" }
], { max_new_tokens: 64 });
print(out["new_text"]);
```

Chat templates are picked from the architecture descriptor (`chatml` for Qwen, `llama2` for Llama / Mistral). Custom templates land in 0.2.

## Supported architectures (`config.json` `model_type` → descriptor)

| `model_type` | Layers | KV heads | Head dim | EOS | Template |
|---|---|---|---|---|---|
| `qwen2` | 24 | 2 | 64 | 151643, 151645 | chatml |
| `llama` | 32 | 8 | 128 | 2 | llama2 |
| `mistral` | 32 | 8 | 128 | 2 | llama2 |

Add a new model family with one entry — see `lib/architectures.bnl` and the `register_architecture(name, descriptor)` escape hatch.

## Build (local)

```powershell
# Windows
.\build.ps1        # cmake configure + build  ->  build/windows-x64/transformers-bnlang.dll
```

```bash
# macOS / Linux
./build.sh
```

## Layout

```
bnl.json                 manifest (main + targets map)
CMakeLists.txt           build config
CMakePresets.json        one preset per platform

lib/
  index.bnl              public API (English + Bangla re-exports)
  pipeline.bnl           pipeline("text-generation", ...) dispatch + engine switch
  tokenizer.bnl          AutoTokenizer.from_pretrained / from_file
  architectures.bnl      descriptors for Qwen2, Llama, Mistral (extensible)
  chat_template.bnl      chatml + llama2 templates
  generation.bnl         KV-cache loop + sampling (the "our loop" engine)

src/                     C++ — BPE tokenizer + argmax/sample (excluded from published tarball)
  bnl/plugin.h           C ABI contract
  main.cpp               bnl_load + argmax_last + sample_last natives
  bpe.{h,cpp}            byte-level BPE tokenizer (~700 LOC)
  external/json.hpp      vendored nlohmann/json single-header

build/<triple>/          cmake output (gitignored)
test/
  smoke.bnl              dtype + tokenizer round-trip
```

## License

MIT. Underlying ORT and GenAI prebuilts are MIT-licensed by Microsoft. See `NOTICES.md` for third-party attribution — in particular nlohmann/json, which is statically embedded.
