content = open('src/ui/render_capability.cpp', 'r', encoding='utf-8').read()

old = '  VulkanInitializer vk_init;\n  if (!vk_init.initialize()) {'
new = '  VulkanInitializer* vk_ptr = VulkanInitializer::get();\n  VulkanInitializer& vk_init = *vk_ptr;\n  if (!vk_init.initialize()) {'

result = content.replace(old, new, 1)
print('Changed:', content != result)
open('src/ui/render_capability.cpp', 'w', encoding='utf-8').write(result)