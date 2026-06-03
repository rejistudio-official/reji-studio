#include "shader_cache.h"
#include <fstream>
#include <filesystem>
#include <cstdio>
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
  WideCharToMultiByte(CP_UTF8, 0, appdata_path, -1, &appdata_str[0], size, nullptr, nullptr);

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
  return FNV1aHash::hash(shader_source);
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

    size_t spirv_size = file_size / sizeof(uint32_t);
    std::vector<uint32_t> spirv_binary(spirv_size);
    file.read(reinterpret_cast<char*>(spirv_binary.data()), file_size);

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
