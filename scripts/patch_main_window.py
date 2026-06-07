content = open('src/ui/main_window.cpp', 'r', encoding='utf-8').read()

# Include ekle
old_include = '#include "settings_dialog.h"'
new_include = '#include "settings_dialog.h"\n#include "../pipeline/gpu/vulkan_initializer.h"'
content = content.replace(old_include, new_include, 1)

# notify_vulkan_ready ekle
old = '        preview_widget_->selectRenderPath(pipeline_.display_vendor_id());'
new_code = old + '''
        // v0.5.1: Vulkan device handle late-binding
        {
            auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
            if (vk && vk->device()) {
                pipeline_.notify_vulkan_ready(vk->device(), vk->physical_device());
                fprintf(stderr, "[MainWindow] notify_vulkan_ready\\n");
                fflush(stderr);
            }
        }'''
content = content.replace(old, new_code, 1)

open('src/ui/main_window.cpp', 'w', encoding='utf-8').write(content)
print('Done')