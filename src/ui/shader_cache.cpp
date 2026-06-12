#include "shader_cache.h"
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <shlobj.h>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace reji::ui {

ShaderCache::ShaderCache() {
  ensure_cache_dir();
}

ShaderCache::~ShaderCache() = default;

std::string ShaderCache::get_cache_dir() {
  WCHAR appdata_path[MAX_PATH];
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata_path))) {
    fprintf(stderr, "[ShaderCache] Failed to get APPDATA path\n");
    fflush(stderr);
    return "";
  }

  int size = WideCharToMultiByte(CP_UTF8, 0, appdata_path, -1, nullptr, 0, nullptr, nullptr);
  std::string appdata_str(size - 1, 0);
  WideCharToMultiByte(CP_UTF8, 0, appdata_path, -1, &appdata_str[0], size - 1, nullptr, nullptr);
  appdata_str[size - 1] = '\0';

  return appdata_str + "\\Reji\\shader_cache\\";
}

std::string ShaderCache::get_cache_path(uint64_t hash) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(16) << hash;
  return get_cache_dir() + ss.str() + ".spv";
}

bool ShaderCache::ensure_cache_dir() {
  std::string cache_dir = get_cache_dir();
  if (cache_dir.empty()) {
    return false;
  }

  try {
    fs::create_directories(cache_dir);
    return true;
  } catch (const fs::filesystem_error& e) {
    fprintf(stderr, "[ShaderCache] Failed to create directory: %s\n", e.what());
    fflush(stderr);
    return false;
  }
}

uint64_t ShaderCache::compute_hash(const std::string& shader_source) {
  // Bump CACHE_VERSION whenever the driver or shader pipeline changes
  // to prevent stale SPIR-V from being loaded after an update.
  constexpr uint32_t CACHE_VERSION = 2;

  auto fnv1a_uint32 = [](uint32_t val) -> uint64_t {
    uint64_t h = FNV1aHash::OFFSET_BASIS;
    for (int i = 0; i < 4; ++i) {
      h ^= static_cast<uint8_t>(val & 0xFF);
      h *= FNV1aHash::PRIME;
      val >>= 8;
    }
    return h;
  };

  uint64_t hash = FNV1aHash::hash(shader_source);
  hash ^= fnv1a_uint32(CACHE_VERSION);
  return hash;
}

bool ShaderCache::write_cache(uint64_t hash, const std::vector<uint32_t>& spirv_binary) {
  std::string cache_path = get_cache_path(hash);

  try {
    std::ofstream file(cache_path, std::ios::binary);
    if (!file) {
      fprintf(stderr, "[ShaderCache] Failed to open cache file for writing: %s\n", cache_path.c_str());
      fflush(stderr);
      return false;
    }

    file.write(reinterpret_cast<const char*>(spirv_binary.data()),
               spirv_binary.size() * sizeof(uint32_t));

    fprintf(stderr, "[ShaderCache] Cached shader (hash: %016llx)\n", hash);
    fflush(stderr);
    return true;
  } catch (const std::exception& e) {
    fprintf(stderr, "[ShaderCache] Write error: %s\n", e.what());
    fflush(stderr);
    return false;
  }
}

std::vector<uint32_t> ShaderCache::read_cache(uint64_t hash) {
  std::string cache_path = get_cache_path(hash);

  try {
    std::ifstream file(cache_path, std::ios::binary);
    if (!file) {
      fprintf(stderr, "[ShaderCache] Cache miss (hash: %016llx)\n", hash);
      fflush(stderr);
      return {};
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    constexpr uint32_t SPIRV_MAGIC = 0x07230203;

    if (file_size < 4 || file_size % 4 != 0) {
      fprintf(stderr, "[ShaderCache] Geçersiz SPIR-V boyutu\n");
      fflush(stderr);
      return {};
    }

    size_t spirv_size = file_size / sizeof(uint32_t);
    std::vector<uint32_t> spirv_binary(spirv_size);
    file.read(reinterpret_cast<char*>(spirv_binary.data()), file_size);

    uint32_t magic = 0;
    memcpy(&magic, spirv_binary.data(), sizeof(magic));
    if (magic != SPIRV_MAGIC) {
      fprintf(stderr, "[ShaderCache] SPIR-V magic uyuşmuyor\n");
      fflush(stderr);
      return {};
    }

    fprintf(stderr, "[ShaderCache] Cache hit (hash: %016llx)\n", hash);
    fflush(stderr);
    return spirv_binary;
  } catch (const std::exception& e) {
    fprintf(stderr, "[ShaderCache] Read error: %s\n", e.what());
    fflush(stderr);
    return {};
  }
}

std::vector<uint32_t> ShaderCache::get_shader(
    const std::string& shader_source,
    const std::string& shader_name) {

  uint64_t hash = compute_hash(shader_source);

  std::vector<uint32_t> cached = read_cache(hash);
  if (!cached.empty()) {
    return cached;
  }

  fprintf(stderr, "[ShaderCache] Shader '%s' not in cache, needs compilation\n", shader_name.c_str());
  fflush(stderr);
  return {};
}

bool ShaderCache::clear_cache() {
  std::string cache_dir = get_cache_dir();
  if (cache_dir.empty()) {
    return false;
  }

  try {
    for (const auto& entry : fs::directory_iterator(cache_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".spv") {
        fs::remove(entry.path());
        fprintf(stderr, "[ShaderCache] Deleted: %s\n", entry.path().filename().string().c_str());
      }
    }
    fflush(stderr);
    return true;
  } catch (const fs::filesystem_error& e) {
    fprintf(stderr, "[ShaderCache] Clear error: %s\n", e.what());
    fflush(stderr);
    return false;
  }
}

}
