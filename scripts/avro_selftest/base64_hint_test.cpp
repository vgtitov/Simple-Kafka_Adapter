// Регрессионный тест поведения resolveBase64Input()/Base64Hint (см. CHANGELOG,
// issue https://github.com/NuclearAPK/Simple-Kafka_Adapter/issues/87).
//
// 1С в ряде случаев маршалит ДвоичныеДанные в нативный метод как base64-текст.
// Раньше DecodeAvroMessage/GetAvroSchema всегда угадывали (эвристика: похоже
// на base64 — декодируем, иначе берём как есть). Автор апстрима отклонил
// угадывание как "закрытый ящик" (сырое тело теоретически может целиком
// состоять из символов алфавита base64) и попросил явный флаг — вызывающий
// сам говорит, что перед ним. Добавлен параметр IsBase64 (tri-state через
// std::monostate = "не передан"):
//   не передан -> Auto: эвристика, как раньше (обратная совместимость);
//   true       -> ForceYes: декодировать, при невалидном base64 — явная ошибка;
//   false      -> ForceNo: никогда не декодировать, брать байты как есть.
//
// Тест самодостаточный: копия tryBase64Decode/resolveBase64Input из utils.cpp
// (та же схема, что у avro_union_encode_test — мирроринг вместо линковки,
// чтобы не тащить сюда boost/json.hpp ради двух функций без внешних зависимостей).
// Запуск: avro_base64_hint_test → код возврата 0 (все проверки прошли) или 1.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "  OK   " : "  FAIL ") << what << "\n";
    if (!ok)
        ++failures;
}

// === точная копия src/utils.cpp: tryBase64Decode + resolveBase64Input ===

static bool tryBase64Decode(const std::string &input, std::vector<char> &out)
{
    auto val = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };

    std::vector<int> sextets;
    sextets.reserve(input.size());
    for (unsigned char c : input)
    {
        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        const int v = val(c);
        if (v < 0) return false;
        sextets.push_back(v);
    }

    const size_t k = sextets.size();
    if (k == 0 || (k % 4) == 1) return false;

    std::vector<char> result;
    result.reserve((k / 4) * 3 + 2);

    size_t i = 0;
    for (; i + 4 <= k; i += 4)
    {
        const uint32_t t = (static_cast<uint32_t>(sextets[i]) << 18) |
                           (static_cast<uint32_t>(sextets[i + 1]) << 12) |
                           (static_cast<uint32_t>(sextets[i + 2]) << 6) |
                           static_cast<uint32_t>(sextets[i + 3]);
        result.push_back(static_cast<char>((t >> 16) & 0xFF));
        result.push_back(static_cast<char>((t >> 8) & 0xFF));
        result.push_back(static_cast<char>(t & 0xFF));
    }

    const size_t rem = k - i;
    if (rem == 2)
    {
        const uint32_t t = (static_cast<uint32_t>(sextets[i]) << 18) |
                           (static_cast<uint32_t>(sextets[i + 1]) << 12);
        result.push_back(static_cast<char>((t >> 16) & 0xFF));
    }
    else if (rem == 3)
    {
        const uint32_t t = (static_cast<uint32_t>(sextets[i]) << 18) |
                           (static_cast<uint32_t>(sextets[i + 1]) << 12) |
                           (static_cast<uint32_t>(sextets[i + 2]) << 6);
        result.push_back(static_cast<char>((t >> 16) & 0xFF));
        result.push_back(static_cast<char>((t >> 8) & 0xFF));
    }

    out = std::move(result);
    return true;
}

enum class Base64Hint { Auto, ForceYes, ForceNo };

static bool resolveBase64Input(const std::string &input, Base64Hint hint, std::vector<char> &out,
                                bool &wasBase64Decoded, std::string &errorOut)
{
    switch (hint)
    {
    case Base64Hint::ForceNo:
        out.assign(input.begin(), input.end());
        wasBase64Decoded = false;
        return true;

    case Base64Hint::ForceYes:
    {
        std::vector<char> decoded;
        if (!tryBase64Decode(input, decoded))
        {
            errorOut = "IsBase64=true, but input is not valid base64";
            return false;
        }
        out = std::move(decoded);
        wasBase64Decoded = true;
        return true;
    }

    case Base64Hint::Auto:
    default:
    {
        std::vector<char> decoded;
        if (tryBase64Decode(input, decoded))
        {
            out = std::move(decoded);
            wasBase64Decoded = true;
        }
        else
        {
            out.assign(input.begin(), input.end());
            wasBase64Decoded = false;
        }
        return true;
    }
    }
}

// === тесты ===

static std::string bytesToStr(const std::vector<char> &v)
{
    return std::string(v.begin(), v.end());
}

int main()
{
    std::vector<char> out;
    bool decoded = false;
    std::string err;

    // Confluent wire format (0x00 magic) — не является валидным base64 (0x00
    // вне алфавита), Auto никогда его не спутает.
    const std::string confluentRaw = std::string("\x00\x00\x00\x00\x01Z", 6);

    // Реальный base64 текст (то, что 1С может прислать вместо сырых байт).
    const std::string realPayload = "hello avro";
    std::string base64OfPayload;
    {
        // Кодируем вручную (тот же алфавит), чтобы не тащить base64Encode из utils.cpp.
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < realPayload.size(); i += 3)
        {
            uint32_t a = (uint8_t)realPayload[i];
            uint32_t b = (i + 1 < realPayload.size()) ? (uint8_t)realPayload[i + 1] : 0;
            uint32_t c = (i + 2 < realPayload.size()) ? (uint8_t)realPayload[i + 2] : 0;
            uint32_t triple = (a << 16) | (b << 8) | c;
            base64OfPayload += table[(triple >> 18) & 0x3F];
            base64OfPayload += table[(triple >> 12) & 0x3F];
            base64OfPayload += (i + 1 < realPayload.size()) ? table[(triple >> 6) & 0x3F] : '=';
            base64OfPayload += (i + 2 < realPayload.size()) ? table[triple & 0x3F] : '=';
        }
    }

    // --- Auto: обратная совместимость с прежней эвристикой ---
    out.clear(); decoded = false; err.clear();
    resolveBase64Input(confluentRaw, Base64Hint::Auto, out, decoded, err);
    check(!decoded && bytesToStr(out) == confluentRaw, "Auto: Confluent-заголовок (0x00) не принимается за base64");

    out.clear(); decoded = false; err.clear();
    resolveBase64Input(base64OfPayload, Base64Hint::Auto, out, decoded, err);
    check(decoded && bytesToStr(out) == realPayload, "Auto: валидный base64 декодируется как раньше");

    // --- ForceYes: явно велено декодировать ---
    out.clear(); decoded = false; err.clear();
    bool ok1 = resolveBase64Input(base64OfPayload, Base64Hint::ForceYes, out, decoded, err);
    check(ok1 && decoded && bytesToStr(out) == realPayload, "ForceYes: валидный base64 декодируется");

    out.clear(); decoded = false; err.clear();
    bool ok2 = resolveBase64Input(confluentRaw, Base64Hint::ForceYes, out, decoded, err);
    check(!ok2 && !err.empty(), "ForceYes: невалидный base64 -> явная ошибка, а не тихий фолбэк на сырые байты");

    // --- ForceNo: никогда не декодировать, даже если похоже на base64 ---
    out.clear(); decoded = false; err.clear();
    bool ok3 = resolveBase64Input(base64OfPayload, Base64Hint::ForceNo, out, decoded, err);
    check(ok3 && !decoded && bytesToStr(out) == base64OfPayload,
          "ForceNo: валидный на вид base64 НЕ декодируется — байты берутся как есть");

    // Ключевой сценарий из вопроса автора: "чёрный ящик" эвристики может
    // ошибочно раскодировать сырое тело, целиком состоящее из base64-алфавита.
    // ForceNo снимает этот риск полностью — именно это и просил автор (вариант 2).
    out.clear(); decoded = false; err.clear();
    const std::string rawLooksLikeBase64 = "QUJD"; // случайно валидные байты "ABC" по совпадению
    resolveBase64Input(rawLooksLikeBase64, Base64Hint::ForceNo, out, decoded, err);
    check(!decoded && bytesToStr(out) == rawLooksLikeBase64,
          "ForceNo: 'чёрный ящик' из вопроса автора устранён — вызывающий управляет поведением явно");

    std::cout << "\n" << (failures == 0 ? "ALL OK" : "FAILURES") << "\n";
    return failures == 0 ? 0 : 1;
}
