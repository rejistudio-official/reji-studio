#pragma once

#include <QOpenGLWidget>

#ifndef REJI_VULKAN_MOCK
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#else
using VkInstance = void*;
using VkDevice = void*;
using VkQueue = void*;
using VkSurfaceKHR = void*;
#endif

namespace reji::ui {

class QRhiWindowWrapper : public QOpenGLWidget {
  Q_OBJECT

 public:
  explicit QRhiWindowWrapper(QWidget* parent = nullptr);
  ~QRhiWindowWrapper();

  bool initialize_vulkan(VkInstance instance, VkDevice device, VkQueue queue);

  VkSurfaceKHR vulkan_surface() const { return vulkan_surface_; }

  void use_opengl_fallback();

  enum class RenderMode {
    kVulkan,
    kOpenGL,
  };
  RenderMode render_mode() const { return render_mode_; }

 protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

 private:
  VkInstance vulkan_instance_;
  VkDevice vulkan_device_;
  VkQueue vulkan_queue_;
  VkSurfaceKHR vulkan_surface_;
  RenderMode render_mode_;
};

}
