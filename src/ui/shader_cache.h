#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace reji::ui {

class FNV1aHash {
 public:
  static constexpr uint64_t OFFSET_BASIS = 14695981039346656037ULL;
  static constexpr uint64_t PRIME = 1099511628211ULL;

  static uint64_t hash(const std::string& data) {
    uint64_t hash_value = OFFSET_BASIS;
    for (unsigned char c : data) {
      hash_value ^= c;
      hash_value *= PRIME;
    }
    return hash_value;
  }

  static uint64_t hash(const std::vector<uint8_t>& data) {
    uint64_t hash_value = OFFSET_BASIS;
    for (uint8_t byte : data) {
      hash_value ^= byte;
      hash_value *= PRIME;
    }
    return hash_value;
  }
};

class ShaderCache {
 public:
  ShaderCache();
  ~ShaderCache();

  std::vector<uint32_t> get_shader(
    const std::string& shader_source,
    const std::string& shader_name
  );

  bool write_cache(uint64_t hash, const std::vector<uint32_t>& spirv_binary);

  std::vector<uint32_t> read_cache(uint64_t hash);

  bool clear_cache();

  static uint64_t compute_hash(const std::string& shader_source);

 private:
  static std::string get_cache_dir();
  static std::string get_cache_path(uint64_t hash);
  static bool ensure_cache_dir();
};

}
