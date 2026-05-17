# transformers-bnlang

[onnxruntime-bnlang](../onnxruntime-bnlang)-এর উপরে Bnlang transformers — pipeline API। দ্রুত LLM generation-এর জন্য optionally [onnxruntime-genai-bnlang](../onnxruntime-genai-bnlang)।

v0.1.0-তে **text generation** সমর্থিত।

> এটি একটি বেসরকারি তৃতীয় পক্ষীয় বাইন্ডিং। Hugging Face বা Microsoft-এর সাথে সম্পর্কিত নয়।

*Read this in English — [README.en.md](README.en.md)*

## দ্রুত শুরু

```bnl
import "transformers-bnlang" as bt;

ধরি জেন = bt.পাইপলাইন("text-generation", "./models/Qwen2.5-0.5B-Instruct");
ধরি ফল  = জেন.চালান("Hello", {
    max_new_tokens: 100,
    temperature:    0.7,
    top_k:          50,
    do_sample:      true
});
লিখুন(ফল["generated_text"]);
জেন.বন্ধ_করুন();
```

## দুটি engine, একই public API

```bnl
// Default: নিজেদের লেখা native loop। যেকোনো decoder-only ONNX model
// (Qwen, Llama, Mistral, Phi, …) চলবে। স্ট্যান্ডার্ড HF লেআউট পড়ে
// (config.json, tokenizer.json, onnx/model.onnx)।
bt.পাইপলাইন("text-generation", "./models/Qwen2.5-0.5B-Instruct");

// Fast path: onnxruntime-genai-এর মধ্য দিয়ে (Microsoft-এর specialized LLM
// runtime)। ~১০× দ্রুত, কিন্তু genai_config.json-সহ একটি model dir চাই।
// একই run/chat/close API।
bt.পাইপলাইন("text-generation", "./models/qwen-0.5b-dml-int4",
            { engine: "onnxruntime-genai" });
```

## Export-গুলো

| নাম | ধরন |
|---|---|
| `সংস্করণ` | string |
| `পাইপলাইন(task, model_dir, options)` | factory → pipeline object |
| `অটো_টোকেনাইজার.মডেল_থেকে(dir)` | factory → tokenizer |
| `অটো_টোকেনাইজার.ফাইল_থেকে(path)` | factory → tokenizer |
| `tok.এনকোড_করুন(text)` | function → list of ids |
| `tok.ডিকোড_করুন(ids)` | function → string |
| `tok.বিশেষ_আইডি(name)` | function → int |
| `tok.বন্ধ_করুন()` | function |
| `gen.চালান(text, opts)` | function → result map |
| `gen.আলাপ_করুন(messages, opts)` | function → result map |
| `gen.বন্ধ_করুন()` | function |
| `স্থাপত্য_নিবন্ধন(name, descriptor)` | function — নতুন architecture যোগ |

## Pipeline option

| Option | ধরন | Default | অর্থ |
|---|---|---|---|
| `model` | string | `onnx/model.onnx` | ONNX file-এর path (relative অথবা absolute) |
| `config` | string | `config.json` | model config-এর path |
| `tokenizer` | string | `tokenizer.json` | tokenizer config-এর path |
| `architecture` | map | (config থেকে পড়ে) | per-arch descriptor: layer, KV head, EOS, chat template, … |
| `engine` | string | (our loop) | `"onnxruntime-genai"` হলে fast path |
| `execution_providers` | list | `["CPU"]` | ORT EP-গুলোর priority list, যেমন `["DML", "CPU"]` |
| `log_severity_level` | int | `3` | ORT log verbosity (0=verbose … 4=fatal) |

## Run-time option (`gen.চালান(text, opts)`)

| Option | Default | অর্থ |
|---|---|---|
| `max_new_tokens` | 32 | prompt-এর পরে কতগুলো token generate হবে |
| `do_sample` | `false` | `true` হলে temperature / top-k / top-p sampling চালু |
| `temperature` | `1.0` | softmax temperature |
| `top_k` | `0` (off) | শীর্ষ-K candidate-এ সীমাবদ্ধ |
| `top_p` | `1.0` (off) | nucleus filter |
| `seed` | `0` | non-zero দিলে sampler-কে reseed |

## চ্যাট

```bnl
ধরি ফল = জেন.আলাপ_করুন([
    { role: "system", content: "তুমি একজন সহায়ক assistant।" },
    { role: "user",   content: "বাংলাদেশের রাজধানী কী?" }
], { max_new_tokens: 64 });
লিখুন(ফল["new_text"]);
```

Chat template architecture descriptor থেকে আসে (Qwen-এর জন্য `chatml`, Llama/Mistral-এর জন্য `llama2`)। কাস্টম template 0.2-এ আসবে।

## সমর্থিত architecture (`config.json` `model_type` → descriptor)

| `model_type` | Layer | KV head | Head dim | EOS | Template |
|---|---|---|---|---|---|
| `qwen2` | 24 | 2 | 64 | 151643, 151645 | chatml |
| `llama` | 32 | 8 | 128 | 2 | llama2 |
| `mistral` | 32 | 8 | 128 | 2 | llama2 |

নতুন model family যোগ করতে একটি entry — দেখুন `lib/architectures.bnl` এবং `স্থাপত্য_নিবন্ধন(name, descriptor)` escape hatch।

## লোকাল বিল্ড

```powershell
# Windows
.\build.ps1        # cmake configure + build  ->  build/windows-x64/transformers-bnlang.dll
```

```bash
# macOS / Linux
./build.sh
```

## লেআউট

```
bnl.json                 manifest (main + targets ম্যাপ)
CMakeLists.txt           build config
CMakePresets.json        প্রতি platform-এ একটি preset

lib/
  index.bnl              public API (ইংরেজি + বাংলা re-export)
  pipeline.bnl           pipeline("text-generation", ...) dispatch + engine switch
  tokenizer.bnl          AutoTokenizer.from_pretrained / from_file
  architectures.bnl      Qwen2, Llama, Mistral-এর descriptor (extensible)
  chat_template.bnl      chatml + llama2 template
  generation.bnl         KV-cache loop + sampling ("our loop" engine)

src/                     C++ source (publish-এ আসে না)
  bnl/plugin.h           C ABI
  main.cpp               bnl_load + argmax_last + sample_last native
  bpe.{h,cpp}            byte-level BPE tokenizer (~700 LOC)
  external/json.hpp      vendored nlohmann/json single-header

build/<triple>/          cmake output (gitignored)
test/
  smoke.bnl              dtype + tokenizer round-trip
```

## লাইসেন্স

MIT. ORT এবং GenAI prebuilt গুলো Microsoft-এর MIT লাইসেন্সে। তৃতীয় পক্ষীয় attribution `NOTICES.md`-এ — বিশেষত nlohmann/json statically embed করা আছে।
