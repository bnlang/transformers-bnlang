# Third-party notices — transformers-bnlang

This package contains code from third parties. Two distinct relationships:

1. **Statically embedded** — code compiled into our binary `.dll`/`.so`/`.dylib`.
   The original license requires we ship the copyright + permission notice
   alongside the binary. That's the full text below for **nlohmann/json**.

2. **Transitive runtime dependency** — code loaded at runtime via the
   onnxruntime-bnlang plugin (which itself dynamically links to ONNX
   Runtime). Attribution + upstream pointer is below.

---

## nlohmann/json — statically embedded in the binary

- **Project:** JSON for Modern C++
- **Upstream:** https://github.com/nlohmann/json
- **Version vendored:** 3.11.3 (at `src/external/json.hpp` in our source tree)
- **License:** MIT
- **Copyright:** Copyright (c) 2013–2023 Niels Lohmann
- **SPDX:** MIT

Used by the BPE tokenizer to parse `tokenizer.json`. Because the library
is header-only, its compiled code ends up inside our shipped binary.

### Full license text

```
MIT License

Copyright (c) 2013-2023 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to furnish persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## ONNX Runtime — transitive (via onnxruntime-bnlang)

- **Project:** Microsoft ONNX Runtime
- **Upstream:** https://github.com/microsoft/onnxruntime
- **License:** MIT
- **Copyright:** Copyright (c) Microsoft Corporation

This package depends on `onnxruntime-bnlang`, which downloads and dynamically
links to ORT's prebuilt binaries. We do not bundle ORT itself; see
`onnxruntime-bnlang/NOTICES.md` for the full attribution.

The full MIT license text is available at
https://github.com/microsoft/onnxruntime/blob/main/LICENSE
