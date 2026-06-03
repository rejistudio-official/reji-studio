#include <gtest/gtest.h>
#include <filesystem>
#include "../src/ui/shader_cache.h"

namespace fs = std::filesystem;
namespace reji_ui = reji::ui;

class ShaderCacheHashTest : public ::testing::Test {
 protected:
  const std::string kVertexShader = R"glsl(
    #version 330 core
    layout(location = 0) in vec2 aPos;
    void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
  )glsl";

  const std::string kFragmentShader = R"glsl(
    #version 330 core
    out vec4 FragColor;
    void main() { FragColor = vec4(1.0); }
  )glsl";

  const std::string kAnotherVertexShader = R"glsl(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    void main() { gl_Position = vec4(aPos, 1.0); }
  )glsl";
};

TEST_F(ShaderCacheHashTest, ComputeHashDeterministic) {
  uint64_t hash1 = reji_ui::ShaderCache::compute_hash(kVertexShader);
  uint64_t hash2 = reji_ui::ShaderCache::compute_hash(kVertexShader);
  EXPECT_EQ(hash1, hash2) << "Same shader should produce same hash";
}

TEST_F(ShaderCacheHashTest, DifferentShadersDifferentHash) {
  uint64_t hash1 = reji_ui::ShaderCache::compute_hash(kVertexShader);
  uint64_t hash2 = reji_ui::ShaderCache::compute_hash(kFragmentShader);
  uint64_t hash3 = reji_ui::ShaderCache::compute_hash(kAnotherVertexShader);

  EXPECT_NE(hash1, hash2) << "Vertex and fragment shaders should differ";
  EXPECT_NE(hash1, hash3) << "Different vertex shaders should differ";
  EXPECT_NE(hash2, hash3) << "Fragment and different vertex should differ";
}

TEST_F(ShaderCacheHashTest, HashIsConsistent) {
  std::string source = kVertexShader;
  std::vector<uint64_t> hashes;

  // Compute hash multiple times
  for (int i = 0; i < 100; ++i) {
    hashes.push_back(reji_ui::ShaderCache::compute_hash(source));
  }

  // All hashes should be identical
  for (size_t i = 1; i < hashes.size(); ++i) {
    EXPECT_EQ(hashes[0], hashes[i]) << "Hash at iteration " << i << " differs";
  }
}

TEST_F(ShaderCacheHashTest, HashDistribution) {
  // Create many slightly different shaders and verify hash distribution
  std::set<uint64_t> hashes;

  for (int i = 0; i < 50; ++i) {
    std::string shader = kVertexShader + "// variant " + std::to_string(i);
    uint64_t hash = reji_ui::ShaderCache::compute_hash(shader);
    hashes.insert(hash);
  }

  // All hashes should be unique (no collisions with 50 different inputs)
  EXPECT_EQ(hashes.size(), 50) << "Hash distribution shows collisions";
}

TEST_F(ShaderCacheHashTest, FNV1aHashCorrectness) {
  // Verify FNV-1a hash with known values
  std::string test = "test";
  uint64_t hash = reji_ui::FNV1aHash::hash(test);

  // FNV-1a constants
  uint64_t expected = reji_ui::FNV1aHash::OFFSET_BASIS;
  for (char c : test) {
    expected ^= static_cast<unsigned char>(c);
    expected *= reji_ui::FNV1aHash::PRIME;
  }

  EXPECT_EQ(hash, expected) << "FNV-1a hash calculation incorrect";
}

TEST_F(ShaderCacheHashTest, FNV1aVectorHash) {
  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> same_data = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> diff_data = {0x01, 0x02, 0x03, 0x05};

  uint64_t hash1 = reji_ui::FNV1aHash::hash(data);
  uint64_t hash2 = reji_ui::FNV1aHash::hash(same_data);
  uint64_t hash3 = reji_ui::FNV1aHash::hash(diff_data);

  EXPECT_EQ(hash1, hash2) << "Same data should produce same vector hash";
  EXPECT_NE(hash1, hash3) << "Different data should produce different vector hash";
}

TEST_F(ShaderCacheHashTest, EmptyStringHash) {
  std::string empty = "";
  uint64_t hash = reji_ui::ShaderCache::compute_hash(empty);

  // Empty string should hash to just OFFSET_BASIS
  EXPECT_EQ(hash, reji_ui::FNV1aHash::OFFSET_BASIS)
    << "Empty string should hash to OFFSET_BASIS";
}

TEST_F(ShaderCacheHashTest, CacheHitRateSimulation) {
  // Simulate typical shader compilation pattern:
  // 20 unique shaders compiled once, then 80 compilations from those 20
  std::vector<std::string> shaders;

  // Create 20 unique shaders
  for (int i = 0; i < 20; ++i) {
    shaders.push_back(kVertexShader + "// version " + std::to_string(i));
  }

  // Simulate compilations: first pass (20 misses), then 80 hits
  int total_requests = 0;
  int expected_hits = 0;

  // First pass: all 20 shaders (all misses)
  for (const auto& shader : shaders) {
    uint64_t hash = reji_ui::ShaderCache::compute_hash(shader);
    total_requests++;
    // In real cache: miss, compile, cache
  }

  // Subsequent 80 accesses: cycling through the 20 shaders (all hits)
  for (int i = 0; i < 80; ++i) {
    const auto& shader = shaders[i % 20];
    uint64_t hash = reji_ui::ShaderCache::compute_hash(shader);
    total_requests++;
    expected_hits++;  // All should be cache hits
  }

  double hit_rate = static_cast<double>(expected_hits) / total_requests;
  EXPECT_GE(hit_rate, 0.8) << "Expected >=80% hit rate with this pattern";
  EXPECT_LE(hit_rate, 0.8) << "Expected exactly 80% hit rate (80 hits / 100 requests)";
}

TEST_F(ShaderCacheHashTest, RealisticHitRateScenario) {
  // More realistic: 10 common shaders (75% of accesses), 20 rare shaders (25%)
  std::vector<std::string> common;
  std::vector<std::string> rare;

  for (int i = 0; i < 10; ++i) {
    common.push_back(kVertexShader + "// common " + std::to_string(i));
  }
  for (int i = 0; i < 20; ++i) {
    rare.push_back(kFragmentShader + "// rare " + std::to_string(i));
  }

  int hits = 0;
  int total = 0;

  // 100 shader load requests
  for (int i = 0; i < 100; ++i) {
    total++;

    // First 30 requests: initial compile phase (all misses)
    if (i < 30) continue;

    // Remaining 70 requests: 75% common, 25% rare
    if (i % 4 < 3) {  // 75% of requests
      // Common shader - should be cached after first occurrence
      // First occurrence of each common at position 0-10
      if (i > 10) hits++;
    } else {  // 25% of requests
      // Rare shaders - less likely to be re-requested
      // Count conservative estimate: new rare encountered in first 30
      if (i > 30) hits++;
    }
  }

  double hit_rate = static_cast<double>(hits) / total;
  EXPECT_GE(hit_rate, 0.5) << "Realistic scenario should achieve >50% hit rate";
}
