#include "qrhi_window_wrapper.h"
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#ifndef REJI_VULKAN_MOCK
#include <vulkan/vulkan_win32.h>
#endif
#endif

namespace reji::ui {

QRhiWindowWrapper::QRhiWindowWrapper(QWidget* parent)
    : QOpenGLWidget(parent),
      vulkan_instance_(nullptr),
      vulkan_device_(nullptr),
      vulkan_queue_(nullptr),
      vulkan_surface_(nullptr),
      render_mode_(RenderMode::kOpenGL) {}

bool QRhiWindowWrapper::initialize_vulkan(VkInstance instance, VkDevice device, VkQueue queue) {
#ifdef REJI_VULKAN_MOCK
  fprintf(stderr, "[QRhiWindowWrapper] Mock mode: skipping Vulkan surface creation\n");
  fflush(stderr);
  render_mode_ = RenderMode::kOpenGL;
  return false;
#else
  vulkan_instance_ = instance;
  vulkan_device_ = device;
  vulkan_queue_ = queue;

#ifdef _WIN32
  VkWin32SurfaceCreateInfoKHR surface_info{};
  surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surface_info.hwnd = reinterpret_cast<HWND>(winId());
  surface_info.hinstance = GetModuleHandle(nullptr);

  VkResult result = vkCreateWin32SurfaceKHR(vulkan_instance_, &surface_info, nullptr, &vulkan_surface_);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[QRhiWindowWrapper] vkCreateWin32SurfaceKHR failed: %d\n", result);
    fflush(stderr);
    return false;
  }

  render_mode_ = RenderMode::kVulkan;
  fprintf(stderr, "[QRhiWindowWrapper] Vulkan surface created\n");
  fflush(stderr);
  return true;
#else
  fprintf(stderr, "[QRhiWindowWrapper] Vulkan not supported on this platform\n");
  fflush(stderr);
  return false;
#endif
#endif
}

void QRhiWindowWrapper::use_opengl_fallback() {
  render_mode_ = RenderMode::kOpenGL;
  fprintf(stderr, "[QRhiWindowWrapper] Switched to OpenGL fallback\n");
  fflush(stderr);
}

void QRhiWindowWrapper::initializeGL() {
  fprintf(stderr, "[QRhiWindowWrapper] initializeGL called\n");
  fflush(stderr);
}

void QRhiWindowWrapper::paintGL() {
  if (render_mode_ == RenderMode::kVulkan) {
    fprintf(stderr, "[QRhiWindowWrapper] Rendering with Vulkan\n");
  } else {
    fprintf(stderr, "[QRhiWindowWrapper] Rendering with OpenGL\n");
  }
  fflush(stderr);
}

void QRhiWindowWrapper::resizeGL(int w, int h) {
  fprintf(stderr, "[QRhiWindowWrapper] Resized to %dx%d\n", w, h);
  fflush(stderr);
}

QRhiWindowWrapper::~QRhiWindowWrapper() {
#ifndef REJI_VULKAN_MOCK
  if (vulkan_surface_ && vulkan_instance_) {
    vkDestroySurfaceKHR(vulkan_instance_, vulkan_surface_, nullptr);
  }
#endif
}

}
