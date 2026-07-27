// Регрессионный тест допущений avro-cpp, на которых стоит фикс кодирования union
// (см. CHANGELOG 1.9.2-fork.1 и docs/POSTMORTEM-avro-base64-decode.md).
//
// Что проверяем — ровно те свойства avro-cpp, из-за неверного понимания которых
// компонента падала с SIGSEGV в fillAvroFromJson:
//   1. GenericDatum::type() / logicalType() САМИ разворачивают union — значит
//      ветку case AVRO_UNION в switch(datum.type()) писать бессмысленно.
//   2. Схему ветвей union НЕЛЬЗЯ достать из самого датума (value<GenericUnion>()
//      разворачивает union и разыменовывает nullptr). Единственный корректный
//      источник — узел схемы от вызывающего: record.schema()->leafAt(i) и т.д.
//   3. Выбор ветки по узлу схемы даёт корректный round-trip: значение, null,
//      union где null НЕ первый, union с record-веткой, рекурсивная схема
//      (ссылка на именованный тип = AVRO_SYMBOLIC).
//
// Тест самодостаточный: схемы внутри, аргументы не нужны.
// Запуск: avro_union_encode_test   → код возврата 0 (все проверки прошли) или 1.

#include <avro/Compiler.hh>
#include <avro/Decoder.hh>
#include <avro/Encoder.hh>
#include <avro/Generic.hh>
#include <avro/GenericDatum.hh>
#include <avro/Stream.hh>
#include <avro/ValidSchema.hh>

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

// Индекс ветки с null — так же, как avroNullBranch() в компоненте.
static size_t nullBranch(const avro::NodePtr &node)
{
    if (!node || node->type() != avro::AVRO_UNION)
        return 0;
    for (size_t i = 0; i < node->leaves(); ++i)
        if (node->leafAt(i)->type() == avro::AVRO_NULL)
            return i;
    return 0;
}

// Индекс ветки под конкретный тип — упрощённый аналог подбора в fillAvroFromJson.
static size_t branchOfType(const avro::NodePtr &node, avro::Type wanted)
{
    for (size_t i = 0; i < node->leaves(); ++i)
        if (node->leafAt(i)->type() == wanted)
            return i;
    return 0;
}

static avro::GenericDatum roundTrip(const avro::ValidSchema &schema, const avro::GenericDatum &datum)
{
    std::unique_ptr<avro::OutputStream> out = avro::memoryOutputStream();
    avro::EncoderPtr enc = avro::binaryEncoder();
    enc->init(*out);
    avro::GenericWriter::write(*enc, datum, schema);
    enc->flush();

    std::unique_ptr<avro::InputStream> in = avro::memoryInputStream(*out);
    avro::DecoderPtr dec = avro::binaryDecoder();
    dec->init(*in);
    avro::GenericReader reader(schema, dec);
    avro::GenericDatum result(schema);
    reader.read(result);
    return result;
}

int main()
{
    try
    {
        // ---- 1. union ["null","string"]: значение и null ---------------------
        {
            const avro::ValidSchema s = avro::compileJsonSchemaFromString(R"({
                "type":"record","name":"R1","fields":[
                    {"name":"comment","type":["null","string"]}
                ]})");

            avro::GenericDatum d(s);
            avro::GenericRecord &rec = d.value<avro::GenericRecord>();
            const avro::NodePtr fieldNode = rec.schema()->leafAt(0);

            rec.fieldAt(0).selectBranch(branchOfType(fieldNode, avro::AVRO_STRING));
            rec.fieldAt(0).value<std::string>() = "привет";

            avro::GenericDatum back = roundTrip(s, d);
            const avro::GenericDatum &f = back.value<avro::GenericRecord>().fieldAt(0);
            check(f.type() == avro::AVRO_STRING, "[null,string] со значением: type() разворачивает union");
            check(f.value<std::string>() == "привет", "[null,string] со значением: round-trip");

            avro::GenericDatum dn(s);
            dn.value<avro::GenericRecord>().fieldAt(0).selectBranch(nullBranch(fieldNode));
            avro::GenericDatum backN = roundTrip(s, dn);
            check(backN.value<avro::GenericRecord>().fieldAt(0).type() == avro::AVRO_NULL,
                  "[null,string] с null: round-trip");
        }

        // ---- 2. union, где null НЕ первый ------------------------------------
        // Прежний код жёстко брал ветку 0 для отсутствующего поля и писал строку
        // вместо null (или падал на несоответствии типа).
        {
            const avro::ValidSchema s = avro::compileJsonSchemaFromString(R"({
                "type":"record","name":"R2","fields":[
                    {"name":"note","type":["string","null"]}
                ]})");

            avro::GenericDatum d(s);
            avro::GenericRecord &rec = d.value<avro::GenericRecord>();
            const avro::NodePtr fieldNode = rec.schema()->leafAt(0);
            check(nullBranch(fieldNode) == 1, "[string,null]: null найден по схеме, а не по индексу 0");

            rec.fieldAt(0).selectBranch(nullBranch(fieldNode));
            avro::GenericDatum back = roundTrip(s, d);
            check(back.value<avro::GenericRecord>().fieldAt(0).type() == avro::AVRO_NULL,
                  "[string,null]: отсутствующее поле уходит как null");
        }

        // ---- 3. union с веткой-record ----------------------------------------
        {
            const avro::ValidSchema s = avro::compileJsonSchemaFromString(R"({
                "type":"record","name":"R3","fields":[
                    {"name":"organization","type":["null",
                        {"type":"record","name":"Org","fields":[
                            {"name":"id","type":"long"},
                            {"name":"title","type":["null","string"]}]}]}
                ]})");

            avro::GenericDatum d(s);
            avro::GenericRecord &rec = d.value<avro::GenericRecord>();
            const avro::NodePtr fieldNode = rec.schema()->leafAt(0);

            avro::GenericDatum &org = rec.fieldAt(0);
            org.selectBranch(branchOfType(fieldNode, avro::AVRO_RECORD));
            // Вложенная запись: узел схемы берём у самой записи — так же, как это
            // делает fillAvroFromJson после выбора ветки.
            avro::GenericRecord &orgRec = org.value<avro::GenericRecord>();
            orgRec.fieldAt(0).value<int64_t>() = 42;
            orgRec.fieldAt(1).selectBranch(branchOfType(orgRec.schema()->leafAt(1), avro::AVRO_STRING));
            orgRec.fieldAt(1).value<std::string>() = "Askona";

            avro::GenericDatum back = roundTrip(s, d);
            const avro::GenericDatum &f = back.value<avro::GenericRecord>().fieldAt(0);
            check(f.type() == avro::AVRO_RECORD, "union с record: выбрана ветка record");
            check(f.value<avro::GenericRecord>().fieldAt(0).value<int64_t>() == 42,
                  "union с record: вложенное поле round-trip");
            check(f.value<avro::GenericRecord>().fieldAt(1).value<std::string>() == "Askona",
                  "union с record: вложенный union round-trip");
        }

        // ---- 4. рекурсивная схема: ссылка на именованный тип (AVRO_SYMBOLIC) --
        {
            const avro::ValidSchema s = avro::compileJsonSchemaFromString(R"({
                "type":"record","name":"Node","fields":[
                    {"name":"name","type":"string"},
                    {"name":"child","type":["null","Node"]}
                ]})");

            avro::GenericDatum d(s);
            avro::GenericRecord &rec = d.value<avro::GenericRecord>();
            rec.fieldAt(0).value<std::string>() = "корень";

            const avro::NodePtr childNode = rec.schema()->leafAt(1);
            bool hasSymbolic = false;
            for (size_t i = 0; i < childNode->leaves(); ++i)
                if (childNode->leafAt(i)->type() == avro::AVRO_SYMBOLIC)
                    hasSymbolic = true;
            check(hasSymbolic, "рекурсивная схема: ветка union приходит как AVRO_SYMBOLIC");

            // Ветка-ссылка: выбираем её и заполняем как обычную запись —
            // GenericDatum сам резолвит символ (GenericDatum::init).
            size_t sym = 0;
            for (size_t i = 0; i < childNode->leaves(); ++i)
                if (childNode->leafAt(i)->type() == avro::AVRO_SYMBOLIC)
                    sym = i;
            rec.fieldAt(1).selectBranch(sym);
            check(rec.fieldAt(1).type() == avro::AVRO_RECORD,
                  "рекурсивная схема: символьная ветка развернулась в record");
            avro::GenericRecord &child = rec.fieldAt(1).value<avro::GenericRecord>();
            child.fieldAt(0).value<std::string>() = "потомок";
            child.fieldAt(1).selectBranch(nullBranch(child.schema()->leafAt(1)));

            avro::GenericDatum back = roundTrip(s, d);
            const avro::GenericRecord &br = back.value<avro::GenericRecord>();
            check(br.fieldAt(0).value<std::string>() == "корень", "рекурсивная схема: корень round-trip");
            check(br.fieldAt(1).value<avro::GenericRecord>().fieldAt(0).value<std::string>() == "потомок",
                  "рекурсивная схема: потомок round-trip");
        }
    }
    catch (const std::exception &ex)
    {
        std::cout << "  FAIL исключение: " << ex.what() << "\n";
        ++failures;
    }

    std::cout << (failures ? "ПРОВАЛЕНО проверок: " + std::to_string(failures) : "Все проверки прошли") << "\n";
    return failures ? 1 : 0;
}
