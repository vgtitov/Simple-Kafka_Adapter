#ifndef KAFKA_UTILS_H
#define KAFKA_UTILS_H

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

// String utilities
std::string base64Encode(const uint8_t* data, size_t len);
// Decode a strictly-valid base64 string into raw bytes. Returns false (and
// leaves out untouched) if the input is not pure, correctly-padded base64 —
// this lets callers distinguish a base64-encoded payload from raw binary.
bool tryBase64Decode(const std::string& input, std::vector<char>& out);

// How to treat a byte string that may or may not be base64-encoded.
// FORK: base64-default-heuristic — upstream's IsBase64 is a plain bool=false
// with no guessing; our Auto default must keep the old heuristic so existing
// 1C calls that never pass the parameter don't change behaviour. Permanent
// divergence, see docs/FORK_DIVERGENCE.md#base64-default-heuristic.
enum class Base64Hint
{
	Auto,     // caller did not say -> guess via tryBase64Decode, fall back to raw bytes on mismatch
	ForceYes, // caller says "this is base64" -> decode, fail loudly if it is not valid base64
	ForceNo   // caller says "this is raw" -> never attempt to decode
};

// Resolves `input` into the bytes that should actually be parsed, honouring an
// explicit caller hint instead of always guessing. `Auto` keeps the historical
// heuristic behaviour (safe default, but a "closed box": a raw payload made
// entirely of base64-alphabet bytes would be mistaken for base64). `ForceYes`/
// `ForceNo` give the caller full control and never guess.
// Returns false only for `ForceYes` on genuinely invalid base64 (errorOut is
// set, out is left untouched); every other case returns true.
bool resolveBase64Input(const std::string& input, Base64Hint hint, std::vector<char>& out,
                         bool& wasBase64Decoded, std::string& errorOut);
bool isValidUtf8(const char* data, size_t len);

// Date/time utilities
std::string currentDateTime();
std::string currentDateTime(const char* format);
intmax_t getTimeStamp();

//================================== Input Validation ==========================================

// URL validation for Schema Registry
bool isValidUrl(const std::string& url);

// JSON validation
bool isValidJson(const std::string& json, std::string& errorMsg);

// Topic name validation (Kafka topic naming rules)
bool isValidTopicName(const std::string& topicName, std::string& errorMsg);

// Broker address validation (host:port format)
bool isValidBrokerAddress(const std::string& address, std::string& errorMsg);

// Broker list validation (comma-separated list of host:port)
bool isValidBrokerList(const std::string& brokerList, std::string& errorMsg);

// Partition number validation
bool isValidPartition(int32_t partition, std::string& errorMsg);

// Replication factor validation
bool isValidReplicationFactor(int32_t replicationFactor, std::string& errorMsg);

// Consumer group ID validation
bool isValidConsumerGroupId(const std::string& groupId, std::string& errorMsg);

#endif // KAFKA_UTILS_H
