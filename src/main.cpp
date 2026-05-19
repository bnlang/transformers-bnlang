#include "bnl/plugin.h"
#include "bpe.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

#define TRANSFORMERS_BNLANG_VERSION "0.1.0"

namespace
{

    bnl_value *version_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
    {
        (void)argc;
        (void)argv;
        (void)ud;
        const char *v = TRANSFORMERS_BNLANG_VERSION;
        return api->make_string(api, v, std::strlen(v));
    }

    bnl_value *argmax_last_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
    {
        (void)argc;
        (void)ud;
        try
        {
            bnl_value *data = argv[0];
            if (api->get_type(data) != BNL_TYPE_LIST)
                throw std::runtime_error("argmax_last: data must be a list");
            int vocab = static_cast<int>(api->get_number(argv[1]));
            if (vocab <= 0)
                throw std::runtime_error("argmax_last: vocab must be positive");

            std::size_t n = api->list_length(data);
            if (static_cast<int>(n) < vocab)
                throw std::runtime_error("argmax_last: data shorter than vocab");

            std::size_t start = n - static_cast<std::size_t>(vocab);
            double best_v = 0.0;
            int best_i = 0;
            bool first = true;
            for (int i = 0; i < vocab; ++i)
            {
                bnl_value *v = api->list_get(api, data, start + static_cast<std::size_t>(i));
                double x = api->get_number(v);
                if (first || x > best_v)
                {
                    best_v = x;
                    best_i = i;
                    first = false;
                }
            }
            return api->make_number(api, static_cast<double>(best_i));
        }
        catch (const std::exception &e)
        {
            api->throw_error(api, e.what());
            return nullptr;
        }
    }

    bnl_value *sample_last_fn(const bnl_api *api, int argc, bnl_value **argv, void *ud)
    {
        (void)argc;
        (void)ud;
        try
        {
            bnl_value *data = argv[0];
            if (api->get_type(data) != BNL_TYPE_LIST)
                throw std::runtime_error("sample_last: data must be a list");
            int vocab = static_cast<int>(api->get_number(argv[1]));
            double temperature = api->get_number(argv[2]);
            int top_k = static_cast<int>(api->get_number(argv[3]));
            double top_p = api->get_number(argv[4]);
            double seed_val = api->get_number(argv[5]);

            if (vocab <= 0)
                throw std::runtime_error("sample_last: vocab must be positive");
            std::size_t n = api->list_length(data);
            if (static_cast<int>(n) < vocab)
                throw std::runtime_error("sample_last: data shorter than vocab");
            std::size_t start = n - static_cast<std::size_t>(vocab);

            if (temperature <= 0.0)
            {
                double best_v = 0.0;
                int best_i = 0;
                bool first = true;
                for (int i = 0; i < vocab; ++i)
                {
                    double x = api->get_number(api->list_get(api, data, start + i));
                    if (first || x > best_v)
                    {
                        best_v = x;
                        best_i = i;
                        first = false;
                    }
                }
                return api->make_number(api, static_cast<double>(best_i));
            }

            std::vector<std::pair<float, int>> logits;
            logits.reserve(vocab);
            for (int i = 0; i < vocab; ++i)
            {
                double x = api->get_number(api->list_get(api, data, start + i));
                logits.emplace_back(static_cast<float>(x / temperature), i);
            }

            if (top_k > 0 && top_k < vocab)
            {
                std::nth_element(logits.begin(), logits.begin() + top_k, logits.end(),
                                 [](const auto &a, const auto &b)
                                 { return a.first > b.first; });
                logits.resize(top_k);
            }
            float max_l = logits[0].first;
            for (const auto &p : logits)
                if (p.first > max_l)
                    max_l = p.first;
            double sum = 0.0;
            std::vector<double> probs(logits.size());
            for (std::size_t i = 0; i < logits.size(); ++i)
            {
                double p = std::exp(static_cast<double>(logits[i].first - max_l));
                probs[i] = p;
                sum += p;
            }
            for (double &p : probs)
                p /= sum;

            if (top_p > 0.0 && top_p < 1.0)
            {
                std::vector<std::size_t> idx(probs.size());
                for (std::size_t i = 0; i < idx.size(); ++i)
                    idx[i] = i;
                std::sort(idx.begin(), idx.end(),
                          [&](std::size_t a, std::size_t b)
                          { return probs[a] > probs[b]; });
                double cum = 0.0;
                std::size_t keep = 0;
                for (; keep < idx.size(); ++keep)
                {
                    cum += probs[idx[keep]];
                    if (cum >= top_p)
                    {
                        ++keep;
                        break;
                    }
                }
                std::vector<std::pair<float, int>> filt_l;
                std::vector<double> filt_p;
                filt_l.reserve(keep);
                filt_p.reserve(keep);
                double s = 0.0;
                for (std::size_t k = 0; k < keep; ++k)
                {
                    filt_l.push_back(logits[idx[k]]);
                    filt_p.push_back(probs[idx[k]]);
                    s += probs[idx[k]];
                }
                for (double &p : filt_p)
                    p /= s;
                logits.swap(filt_l);
                probs.swap(filt_p);
            }

            static std::mt19937_64 rng{std::random_device{}()};
            if (seed_val != 0.0)
            {
                rng.seed(static_cast<std::uint64_t>(seed_val));
            }
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            double u = dist(rng);

            double cum = 0.0;
            int pick_id = logits.back().second;
            for (std::size_t i = 0; i < probs.size(); ++i)
            {
                cum += probs[i];
                if (u < cum)
                {
                    pick_id = logits[i].second;
                    break;
                }
            }
            return api->make_number(api, static_cast<double>(pick_id));
        }
        catch (const std::exception &e)
        {
            api->throw_error(api, e.what());
            return nullptr;
        }
    }

} // namespace

extern "C" BNL_EXPORT bnl_module *bnl_load(const bnl_api *api)
{
    bnl_module *mod = api->module_new(api, "transformers-bnlang");

    api->module_add_value(mod, "version",
                          api->make_string(api,
                                           TRANSFORMERS_BNLANG_VERSION,
                                           std::strlen(TRANSFORMERS_BNLANG_VERSION)));

    api->module_add_function(mod, "argmax_last",  2, &argmax_last_fn, nullptr);
    api->module_add_function(mod, "sample_last",  6, &sample_last_fn, nullptr);

    transformers::bpe::register_natives(api, mod);

    return mod;
}
