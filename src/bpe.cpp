#include "bpe.h"
#include "external/json.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using nlohmann::json;

namespace transformers::bpe
{

    namespace
    {
        struct ByteUnicodeMap
        {
            std::array<std::string, 256> byte_to_str;             // UTF-8 encoded Unicode char(s)
            std::unordered_map<std::string, uint8_t> str_to_byte; // reverse
        };

        std::string codepoint_to_utf8(uint32_t cp)
        {
            std::string out;
            if (cp < 0x80)
            {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            return out;
        }

        const ByteUnicodeMap &bytes_to_unicode()
        {
            static ByteUnicodeMap m = []()
            {
                ByteUnicodeMap r;
                std::vector<int> bs;
                std::vector<uint32_t> cs;
                for (int b = 0x21; b <= 0x7E; ++b)
                {
                    bs.push_back(b);
                    cs.push_back(static_cast<uint32_t>(b));
                }
                for (int b = 0xA1; b <= 0xAC; ++b)
                {
                    bs.push_back(b);
                    cs.push_back(static_cast<uint32_t>(b));
                }
                for (int b = 0xAE; b <= 0xFF; ++b)
                {
                    bs.push_back(b);
                    cs.push_back(static_cast<uint32_t>(b));
                }
                uint32_t n = 0;
                for (int b = 0; b < 256; ++b)
                {
                    if (std::find(bs.begin(), bs.end(), b) == bs.end())
                    {
                        bs.push_back(b);
                        cs.push_back(256 + n);
                        ++n;
                    }
                }
                for (std::size_t i = 0; i < bs.size(); ++i)
                {
                    std::string u8 = codepoint_to_utf8(cs[i]);
                    r.byte_to_str[bs[i]] = u8;
                    r.str_to_byte[u8] = static_cast<uint8_t>(bs[i]);
                }
                return r;
            }();
            return m;
        }

        std::string byte_encode(const std::string &utf8)
        {
            const auto &m = bytes_to_unicode();
            std::string out;
            out.reserve(utf8.size() * 2);
            for (unsigned char c : utf8)
                out += m.byte_to_str[c];
            return out;
        }

        std::string byte_decode(const std::string &encoded)
        {
            const auto &m = bytes_to_unicode();
            std::string out;
            out.reserve(encoded.size());
            std::size_t i = 0;
            while (i < encoded.size())
            {
                unsigned char lead = static_cast<unsigned char>(encoded[i]);
                std::size_t len = 1;
                if ((lead & 0x80) == 0x00)
                    len = 1;
                else if ((lead & 0xE0) == 0xC0)
                    len = 2;
                else if ((lead & 0xF0) == 0xE0)
                    len = 3;
                else if ((lead & 0xF8) == 0xF0)
                    len = 4;
                if (i + len > encoded.size())
                    break;
                std::string ch = encoded.substr(i, len);
                auto it = m.str_to_byte.find(ch);
                if (it != m.str_to_byte.end())
                {
                    out.push_back(static_cast<char>(it->second));
                }
                i += len;
            }
            return out;
        }

        bool is_letter_ascii(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }
        bool is_digit_ascii(char c)
        {
            return c >= '0' && c <= '9';
        }
        bool is_ws(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
        }
        bool is_nl(char c) { return c == '\r' || c == '\n'; }

        std::size_t match_contraction(const std::string &s, std::size_t i)
        {
            if (i >= s.size() || s[i] != '\'')
                return 0;
            auto lc = [&](std::size_t j) -> char
            {
                if (j >= s.size())
                    return 0;
                char c = s[j];
                return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            };
            char a = lc(i + 1);
            char b = lc(i + 2);
            // 3-char: 're, 've, 'll
            if ((a == 'r' && b == 'e') || (a == 'v' && b == 'e') ||
                (a == 'l' && b == 'l'))
                return 3;
            // 2-char: 's, 't, 'm, 'd
            if (a == 's' || a == 't' || a == 'm' || a == 'd')
                return 2;
            return 0;
        }

        std::vector<std::string> pretokenize(const std::string &text)
        {
            std::vector<std::string> out;
            std::size_t n = text.size();
            std::size_t i = 0;

            while (i < n)
            {
                // 1) contractions
                if (auto k = match_contraction(text, i); k > 0)
                {
                    out.push_back(text.substr(i, k));
                    i += k;
                    continue;
                }
                {
                    std::size_t start = i;
                    if (i < n && !is_letter_ascii(text[i]) && !is_digit_ascii(text[i]) && !is_nl(text[i]))
                    {
                        unsigned char lead = static_cast<unsigned char>(text[i]);
                        if ((lead & 0x80) == 0)
                        {
                            ++i;
                        }
                        else
                        {
                            std::size_t len = 1;
                            if ((lead & 0xE0) == 0xC0)
                                len = 2;
                            else if ((lead & 0xF0) == 0xE0)
                                len = 3;
                            else if ((lead & 0xF8) == 0xF0)
                                len = 4;
                            if (i + len <= n)
                                i += len;
                            else
                            {
                                i = start;
                                goto try_digit;
                            }
                        }
                    }
                    std::size_t j = i;
                    while (j < n && is_letter_ascii(text[j]))
                        ++j;
                    if (j > i)
                    {
                        out.push_back(text.substr(start, j - start));
                        i = j;
                        continue;
                    }
                    i = start;
                }
            try_digit:
                if (i < n && is_digit_ascii(text[i]))
                {
                    out.push_back(text.substr(i, 1));
                    ++i;
                    continue;
                }
                {
                    std::size_t start = i;
                    bool had_space = false;
                    if (i < n && text[i] == ' ')
                    {
                        ++i;
                        had_space = true;
                    }
                    std::size_t punct_start = i;
                    while (i < n && !is_letter_ascii(text[i]) && !is_digit_ascii(text[i]) && !is_ws(text[i]))
                    {
                        ++i;
                    }
                    if (i > punct_start)
                    {
                        while (i < n && is_nl(text[i]))
                            ++i;
                        out.push_back(text.substr(start, i - start));
                        continue;
                    }
                    i = start;
                    had_space = false;
                    (void)had_space;
                }
                {
                    std::size_t start = i;
                    while (i < n && is_ws(text[i]) && !is_nl(text[i]))
                        ++i;
                    if (i < n && is_nl(text[i]))
                    {
                        while (i < n && is_nl(text[i]))
                            ++i;
                        out.push_back(text.substr(start, i - start));
                        continue;
                    }
                    i = start;
                }
                {
                    std::size_t start = i;
                    while (i < n && is_ws(text[i]))
                        ++i;
                    if (i == n && i > start)
                    {
                        out.push_back(text.substr(start, i - start));
                        continue;
                    }
                    if (i > start && i < n)
                    {
                        std::size_t total = i - start;
                        std::size_t emit = (total >= 2) ? (total - 1) : 1;
                        out.push_back(text.substr(start, emit));
                        i = start + emit;
                        continue;
                    }
                    i = start;
                }
                out.push_back(text.substr(i, 1));
                ++i;
            }
            return out;
        }

        struct Tokenizer
        {
            std::unordered_map<std::string, int> vocab;          // byte-encoded -> id
            std::unordered_map<std::string, int> merge_ranks;    // "left right" -> rank
            std::unordered_map<std::string, int> special_tokens; // literal -> id
            std::vector<int> special_ids;
            std::vector<std::string> id_to_token; // for decode

            void load_from_json(const std::string &path);
            std::vector<int> encode(const std::string &text) const;
            std::string decode(const std::vector<int> &ids) const;

        private:
            std::vector<std::string> bpe_split(const std::string &word) const;
        };

        std::vector<std::string> split_codepoints(const std::string &s)
        {
            std::vector<std::string> out;
            std::size_t i = 0;
            while (i < s.size())
            {
                unsigned char lead = static_cast<unsigned char>(s[i]);
                std::size_t len = 1;
                if ((lead & 0x80) == 0x00)
                    len = 1;
                else if ((lead & 0xE0) == 0xC0)
                    len = 2;
                else if ((lead & 0xF0) == 0xE0)
                    len = 3;
                else if ((lead & 0xF8) == 0xF0)
                    len = 4;
                if (i + len > s.size())
                    len = 1;
                out.push_back(s.substr(i, len));
                i += len;
            }
            return out;
        }

        std::vector<std::string> Tokenizer::bpe_split(const std::string &word) const
        {
            auto chars = split_codepoints(word);
            if (chars.size() < 2)
                return chars;

            while (true)
            {
                int best_rank = -1;
                std::size_t best_i = 0;
                for (std::size_t i = 0; i + 1 < chars.size(); ++i)
                {
                    std::string key = chars[i] + " " + chars[i + 1];
                    auto it = merge_ranks.find(key);
                    if (it != merge_ranks.end())
                    {
                        if (best_rank < 0 || it->second < best_rank)
                        {
                            best_rank = it->second;
                            best_i = i;
                        }
                    }
                }
                if (best_rank < 0)
                    break;

                std::string merged = chars[best_i] + chars[best_i + 1];
                std::vector<std::string> next;
                next.reserve(chars.size() - 1);
                for (std::size_t i = 0; i < chars.size(); ++i)
                {
                    if (i == best_i)
                    {
                        next.push_back(merged);
                        ++i;
                    }
                    else
                    {
                        next.push_back(chars[i]);
                    }
                }
                chars = std::move(next);
            }
            return chars;
        }

        void Tokenizer::load_from_json(const std::string &path)
        {
            std::ifstream in(path);
            if (!in)
                throw std::runtime_error("tokenizer_open: cannot open " + path);
            json j;
            in >> j;

            if (j.contains("added_tokens") && j["added_tokens"].is_array())
            {
                for (const auto &at : j["added_tokens"])
                {
                    if (!at.is_object())
                        continue;
                    int id = at.value("id", -1);
                    std::string content = at.value("content", "");
                    if (id >= 0 && !content.empty())
                    {
                        special_tokens[content] = id;
                        special_ids.push_back(id);
                    }
                }
            }

            if (!j.contains("model"))
                throw std::runtime_error("tokenizer_open: missing 'model'");
            const auto &m = j["model"];
            if (m.value("type", "") != "BPE")
                throw std::runtime_error("tokenizer_open: only BPE supported in v0.1 (got: " +
                                         m.value("type", "?") + ")");

            if (!m.contains("vocab") || !m["vocab"].is_object())
                throw std::runtime_error("tokenizer_open: missing model.vocab");
            int max_id = -1;
            for (auto it = m["vocab"].begin(); it != m["vocab"].end(); ++it)
            {
                if (!it.value().is_number_integer())
                    continue;
                int id = it.value().get<int>();
                vocab[it.key()] = id;
                if (id > max_id)
                    max_id = id;
            }

            id_to_token.assign(static_cast<std::size_t>(max_id + 1), std::string());
            for (const auto &[tok, id] : vocab)
            {
                if (id >= 0 && static_cast<std::size_t>(id) < id_to_token.size())
                    id_to_token[id] = tok;
            }
            for (const auto &[tok, id] : special_tokens)
            {
                if (id >= 0)
                {
                    if (static_cast<std::size_t>(id) >= id_to_token.size())
                        id_to_token.resize(id + 1);
                    id_to_token[id] = tok;
                }
            }

            if (m.contains("merges") && m["merges"].is_array())
            {
                int rank = 0;
                for (const auto &mr : m["merges"])
                {
                    std::string key;
                    if (mr.is_array() && mr.size() == 2 &&
                        mr[0].is_string() && mr[1].is_string())
                    {
                        key = mr[0].get<std::string>() + " " + mr[1].get<std::string>();
                    }
                    else if (mr.is_string())
                    {
                        key = mr.get<std::string>();
                    }
                    else
                    {
                        continue;
                    }
                    merge_ranks[key] = rank++;
                }
            }
        }

        std::vector<int> Tokenizer::encode(const std::string &text) const
        {
            std::vector<int> out;
            if (text.empty())
                return out;

            struct Piece
            {
                bool is_special;
                std::string s;
                int id;
            };
            std::vector<Piece> pieces;
            pieces.push_back({false, text, 0});
            for (const auto &[content, id] : special_tokens)
            {
                std::vector<Piece> next;
                for (const auto &p : pieces)
                {
                    if (p.is_special)
                    {
                        next.push_back(p);
                        continue;
                    }
                    std::size_t start = 0;
                    while (true)
                    {
                        std::size_t pos = p.s.find(content, start);
                        if (pos == std::string::npos)
                        {
                            if (start < p.s.size())
                                next.push_back({false, p.s.substr(start), 0});
                            break;
                        }
                        if (pos > start)
                            next.push_back({false, p.s.substr(start, pos - start), 0});
                        next.push_back({true, content, id});
                        start = pos + content.size();
                    }
                }
                pieces = std::move(next);
            }

            for (const auto &p : pieces)
            {
                if (p.is_special)
                {
                    out.push_back(p.id);
                    continue;
                }
                if (p.s.empty())
                    continue;
                for (const auto &pre : pretokenize(p.s))
                {
                    std::string encoded = byte_encode(pre);
                    auto pieces2 = bpe_split(encoded);
                    for (const auto &piece : pieces2)
                    {
                        auto it = vocab.find(piece);
                        if (it != vocab.end())
                        {
                            out.push_back(it->second);
                        }
                        else
                        {
                            for (char c : piece)
                            {
                                const auto &m = bytes_to_unicode();
                                std::string single = m.byte_to_str[static_cast<unsigned char>(c)];
                                auto it2 = vocab.find(single);
                                if (it2 != vocab.end())
                                    out.push_back(it2->second);
                            }
                        }
                    }
                }
            }
            return out;
        }

        std::string Tokenizer::decode(const std::vector<int> &ids) const
        {
            std::string encoded;
            for (int id : ids)
            {
                if (id < 0 || static_cast<std::size_t>(id) >= id_to_token.size())
                    continue;
                encoded += id_to_token[id];
            }
            return byte_decode(encoded);
        }

        struct Table
        {
            std::mutex mu;
            int next_id = 1;
            std::unordered_map<int, std::unique_ptr<Tokenizer>> toks;
        };
        Table &table()
        {
            static Table t;
            return t;
        }

        bnl_value *tokenizer_open_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
        {
            (void)argc;
            (void)ud;
            try
            {
                std::size_t plen = 0;
                const char *p = api->get_string(argv[0], &plen);
                std::string path(p, plen);

                auto tok = std::make_unique<Tokenizer>();
                tok->load_from_json(path);

                auto &t = table();
                std::lock_guard<std::mutex> lk(t.mu);
                int id = t.next_id++;
                t.toks.emplace(id, std::move(tok));
                return api->make_number(api, static_cast<double>(id));
            }
            catch (const std::exception &e)
            {
                api->throw_error(api, e.what());
                return nullptr;
            }
        }

        bnl_value *tokenizer_close_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
        {
            (void)argc;
            (void)ud;
            try
            {
                int id = static_cast<int>(api->get_number(argv[0]));
                auto &t = table();
                std::lock_guard<std::mutex> lk(t.mu);
                t.toks.erase(id);
                return api->make_null(api);
            }
            catch (const std::exception &e)
            {
                api->throw_error(api, e.what());
                return nullptr;
            }
        }

        bnl_value *tokenizer_encode_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
        {
            (void)argc;
            (void)ud;
            try
            {
                int id = static_cast<int>(api->get_number(argv[0]));
                std::size_t tlen = 0;
                const char *tp = api->get_string(argv[1], &tlen);
                std::string text(tp, tlen);

                Tokenizer *tok = nullptr;
                {
                    auto &t = table();
                    std::lock_guard<std::mutex> lk(t.mu);
                    auto it = t.toks.find(id);
                    if (it == t.toks.end())
                        throw std::runtime_error("invalid tokenizer handle");
                    tok = it->second.get();
                }

                auto ids = tok->encode(text);
                bnl_value *list = api->make_list(api);
                for (int x : ids)
                    api->list_push(list, api->make_number(api, static_cast<double>(x)));
                return list;
            }
            catch (const std::exception &e)
            {
                api->throw_error(api, e.what());
                return nullptr;
            }
        }

        bnl_value *tokenizer_decode_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
        {
            (void)argc;
            (void)ud;
            try
            {
                int id = static_cast<int>(api->get_number(argv[0]));
                std::vector<int> ids;
                std::size_t n = api->list_length(argv[1]);
                for (std::size_t i = 0; i < n; ++i)
                {
                    bnl_value *v = api->list_get(api, argv[1], i);
                    ids.push_back(static_cast<int>(api->get_number(v)));
                }

                Tokenizer *tok = nullptr;
                {
                    auto &t = table();
                    std::lock_guard<std::mutex> lk(t.mu);
                    auto it = t.toks.find(id);
                    if (it == t.toks.end())
                        throw std::runtime_error("invalid tokenizer handle");
                    tok = it->second.get();
                }
                std::string out = tok->decode(ids);
                return api->make_string(api, out.data(), out.size());
            }
            catch (const std::exception &e)
            {
                api->throw_error(api, e.what());
                return nullptr;
            }
        }

        bnl_value *tokenizer_special_id_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
        {
            (void)argc;
            (void)ud;
            try
            {
                int id = static_cast<int>(api->get_number(argv[0]));
                std::size_t nlen = 0;
                const char *np = api->get_string(argv[1], &nlen);
                std::string name(np, nlen);

                Tokenizer *tok = nullptr;
                {
                    auto &t = table();
                    std::lock_guard<std::mutex> lk(t.mu);
                    auto it = t.toks.find(id);
                    if (it == t.toks.end())
                        throw std::runtime_error("invalid tokenizer handle");
                    tok = it->second.get();
                }
                auto it2 = tok->special_tokens.find(name);
                if (it2 == tok->special_tokens.end())
                    return api->make_null(api);
                return api->make_number(api, static_cast<double>(it2->second));
            }
            catch (const std::exception &e)
            {
                api->throw_error(api, e.what());
                return nullptr;
            }
        }

    } // namespace

    void register_natives(const bnl_api *api, bnl_module *mod)
    {
        (void)api;
        api->module_add_function(mod, "tokenizer_open", 1, &tokenizer_open_fn, nullptr);
        api->module_add_function(mod, "tokenizer_close", 1, &tokenizer_close_fn, nullptr);
        api->module_add_function(mod, "tokenizer_encode", 2, &tokenizer_encode_fn, nullptr);
        api->module_add_function(mod, "tokenizer_decode", 2, &tokenizer_decode_fn, nullptr);
        api->module_add_function(mod, "tokenizer_special_id", 2, &tokenizer_special_id_fn, nullptr);
    }

} // namespace transformers::bpe
