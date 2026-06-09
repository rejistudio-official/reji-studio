# Task: v0.5.2 — VkImage → OpenGL Texture Transfer (GL Interop)

## Çalışma Akışı (HER ADIM İÇİN)

Her adımda şu sırayı takip et — atlamadan:

1. **PLAN** — Ne yapacağını yaz: hangi dosyalar değişecek, hangi fonksiyonlar eklenecek/değişecek, yan etkiler var mı
2. **SPEC** — Değişiklikleri detaylı yaz: fonksiyon imzaları, struct alanları, kritik satırlar
3. **ONAY BEKLE** — "Onaylıyor musun? (e/h)" diye sor. Kullanıcı onaylayana kadar kod yazma
4. **UYGULA** — Onay geldikten sonra kodu yaz ve commit et
5. **TEST** — `cmake --build --preset release` çalıştır, ardından `cd build\src\ui && reji_app.exe 2>&1 | more` ile logları kontrol et. Beklenen log satırlarını göster. Hata varsa dur ve raporla

Adımlar arası geçiş: bir adımın testi başarılı olmadan sonraki adıma geçme.

---

## Hedef
`paintGL()` içinde frame'lerin ekranda görünmesi.
Yöntem: `GL_EXT_memory_object_win32` — VkImage backing memory'yi OpenGL texture'a import et (zero-copy devam eder).

## Bağlam
- Repo: `C:\reji-studio`
- CONTEXT.md, docs/progress.md, docs/memory.md oku
- Build: `cmake --build --preset release` (x64 Native Tools CMD)

---

## Mevcut Sorunlar (önce bunları düzelt)

### 1. `external_memory_bridge.cpp` — pool_memory_ hiç doldurulmuyor
`create_vulkan_image_from_d3d11` içinde `vk_mem` allocate ediliyor ama
`pool_memory_` vektörüne yazılmıyor → memory leak + dangling image.

### 2. `external_memory_bridge.cpp` — staging ve target aynı image
```cpp
*out_target = image_pool_[idx];  // staging ile aynı — yanlış
```
Target image ayrı olmalı: `VK_IMAGE_USAGE_SAMPLED_BIT` ile Vulkan'dan
okunabilir + OpenGL interop için `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`.

### 3. `copy_optimizer.cpp` — execute_copy stub
Gerçek `vkCmdBlitImage` + layout transition yok. Semaphore signal yok.

---

## Yapılacaklar (sırayla)

### Adım 1 — `external_memory_bridge.h/.cpp`: target image pool ekle

`ExternalMemoryBridge`'e ikinci bir pool ekle — GL interop için ayrı target image'lar.

**`external_memory_bridge.h`'a ekle:**
```cpp
// GL interop için export edilebilir target image pool
std::vector<VkImage>        gl_target_pool_;
std::vector<VkDeviceMemory> gl_target_memory_;
HANDLE                      gl_target_handles_[POOL_SIZE]{};  // NT handles for GL import

bool initialize_gl_target_pool(uint32_t width, uint32_t height);
HANDLE get_gl_target_handle(uint32_t idx) const;
```

**`external_memory_bridge.cpp` — `initialize_gl_target_pool` implementasyonu:**
- Her slot için `vkCreateImage`:
  - `usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`
  - `VkExternalMemoryImageCreateInfo::handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`
  - `format = VK_FORMAT_B8G8R8A8_UNORM` (DXGI_FORMAT_B8G8R8A8_UNORM ile eşleşmeli)
- `VkMemoryAllocateInfo` + `VkExportMemoryAllocateInfo`:
  - `handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`
- `vkAllocateMemory` → `gl_target_memory_[i]`'a yaz
- `vkBindImageMemory`
- `vkGetMemoryWin32HandleKHR` → `gl_target_handles_[i]`'a yaz

**`get_frame_images` düzelt:**
```cpp
*out_staging = image_pool_[idx];     // D3D11 import — mevcut
*out_target  = gl_target_pool_[idx]; // GL interop target — yeni
```

**`create_vulkan_image_from_d3d11` düzelt:**
- `vk_mem`'i `pool_memory_[idx]`'a yaz (idx parametresi ekle veya
  çağıran `get_frame_images` içinde yönet)

**`shutdown` güncelle:**
- `gl_target_memory_` free et
- `gl_target_pool_` destroy et
- `gl_target_handles_` CloseHandle et

---

### Adım 2 — `copy_optimizer.cpp`: gerçek execute_copy

`execute_copy` içine gerçek komutları yaz:

```cpp
// 1. vkBeginCommandBuffer (reset flag ile)
// 2. Barrier: staging_vk → VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
// 3. Barrier: target_vk  → VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
// 4. vkCmdBlitImage (veya vkCmdCopyImage — format aynıysa copy daha hızlı)
// 5. Barrier: target_vk  → VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
// 6. vkEndCommandBuffer
// 7. VkTimelineSemaphoreSubmitInfoKHR ile vkQueueSubmit
//    signalSemaphoreValueCount=1, pSignalSemaphoreValues=&timeline_counter_
```

Barrier helper (inline, no heap):
```cpp
VkImageMemoryBarrier barrier{};
barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1};
```

---

### Adım 3 — `preview_widget.h`: GL interop state ekle

`Impl` struct'ına ekle:
```cpp
// GL_EXT_memory_object_win32 interop state
GLuint gl_memory_object{0};   // glCreateMemoryObjectsEXT
GLuint interop_tex_id{0};     // glCreateTextures → memory object'e bağlı
uint32_t interop_pool_idx{0}; // hangi pool slot'u kullanıldı

// Extension function pointers (initializeGL'de resolve et)
PFNGLCREATEMEMORYOBJECTSEXTPROC       pfn_CreateMemoryObjects{};
PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC   pfn_ImportMemoryWin32Handle{};
PFNGLTEXSTORAGEMEM2DEXTPROC           pfn_TexStorageMem2D{};
PFNGLISSEMAPHOREEXTPROC               pfn_IsSemaphore{};  // opsiyonel sync
```

---

### Adım 4 — `preview_widget.cpp`: initializeGL GL interop kurulumu

`initializeGL` sonuna ekle (pipeline_ ve bridge erişimi gerekiyor):
```cpp
// GL_EXT_memory_object_win32 extension kontrolü
const char* exts = (const char*)glGetString(GL_EXTENSIONS);
bool has_memory_object = exts && strstr(exts, "GL_EXT_memory_object");
bool has_win32_handle  = exts && strstr(exts, "GL_EXT_memory_object_win32");
fprintf(stderr, "[PreviewWidget] GL_EXT_memory_object=%d win32=%d\n",
        has_memory_object, has_win32_handle);

if (has_win32_handle) {
    d_->pfn_CreateMemoryObjects = (PFNGLCREATEMEMORYOBJECTSEXTPROC)
        wglGetProcAddress("glCreateMemoryObjectsEXT");
    d_->pfn_ImportMemoryWin32Handle = (PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)
        wglGetProcAddress("glImportMemoryWin32HandleEXT");
    d_->pfn_TexStorageMem2D = (PFNGLTEXSTORAGEMEM2DEXTPROC)
        wglGetProcAddress("glTexStorageMem2DEXT");
}
```

---

### Adım 5 — `preview_widget.cpp`: paintGL GL interop texture bağlama

`paintGL` içinde "Lazy texture alloc" bloğunu değiştir:

```cpp
if (w != d_->tex_w || h != d_->tex_h) {
    // Eski texture temizle
    if (d_->interop_tex_id) {
        glDeleteTextures(1, &d_->interop_tex_id);
        d_->interop_tex_id = 0;
    }
    if (d_->gl_memory_object) {
        // glDeleteMemoryObjectsEXT(1, &d_->gl_memory_object);
        d_->gl_memory_object = 0;
    }

    // Bridge'den NT handle al
    // (bridge pointer'ı PreviewWidget'a ekle — aşağıya bak)
    HANDLE nt_handle = bridge_->get_gl_target_handle(current_pool_idx);

    if (nt_handle && d_->pfn_CreateMemoryObjects &&
        d_->pfn_ImportMemoryWin32Handle && d_->pfn_TexStorageMem2D) {

        // Memory object oluştur ve NT handle'ı import et
        d_->pfn_CreateMemoryObjects(1, &d_->gl_memory_object);
        d_->pfn_ImportMemoryWin32Handle(
            d_->gl_memory_object,
            GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
            nt_handle
        );

        // Texture oluştur ve memory object'e bağla
        glGenTextures(1, &d_->interop_tex_id);
        glBindTexture(GL_TEXTURE_2D, d_->interop_tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        d_->pfn_TexStorageMem2D(
            GL_TEXTURE_2D, 1,
            GL_RGBA8,       // VK_FORMAT_B8G8R8A8_UNORM ile eşleşmeli
            w, h,
            d_->gl_memory_object, 0
        );

        d_->tex_id = d_->interop_tex_id;  // render path aynı tex_id'yi kullanır
        fprintf(stderr, "[PreviewWidget] GL interop texture created: %ux%u\n", w, h);
    } else {
        // Fallback: boş texture (frame görünmez ama crash olmaz)
        if (d_->tex_id) glDeleteTextures(1, &d_->tex_id);
        glGenTextures(1, &d_->tex_id);
        glBindTexture(GL_TEXTURE_2D, d_->tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        fprintf(stderr, "[PreviewWidget] GL interop unavailable, using empty texture\n");
    }

    d_->tex_w = w;
    d_->tex_h = h;
}
```

**`PreviewWidget`'a bridge pointer ekle:**
```cpp
// preview_widget.h
void setBridge(rj::pipeline::gpu::ExternalMemoryBridge* bridge) noexcept;

// preview_widget.cpp Impl'e:
rj::pipeline::gpu::ExternalMemoryBridge* bridge_{nullptr};
```

---

### Adım 6 — `main_window.cpp`: bridge'i widget'a wire et

`notify_vulkan_ready` çağrısından sonra:
```cpp
preview_widget_->setBridge(&bridge_);  // bridge_ main_window member'ı
```

---

## Senkronizasyon Notu

Vulkan `execute_copy` bittikten sonra (timeline semaphore signal) OpenGL
render başlayabilir. Şu an `is_copy_ready` poll mekanizması bu sırayı
sağlıyor — GL side'da ek semaphore gerekmez (aynı process, implicit sync
yeterli). Eğer tearing/corruption görülürse `GL_EXT_semaphore_win32` eklenecek.

---

## Beklenen Log Çıktısı (başarılı)

```
[ExternalMemoryBridge] GL target pool initialized: 3x 1920x1080
[ExternalMemoryBridge] gl_target_handles[0]=0x... [1]=0x... [2]=0x...
[PreviewWidget] GL_EXT_memory_object=1 win32=1
[PreviewWidget] GL interop texture created: 1920x1080
[GpuCopyOptimizer] execute_copy: blit submitted, timeline=1
[PreviewWidget] render: tex_id=1 (frame visible)
```

---

## Commit Mesajları (sırayla)

```
fix: ExternalMemoryBridge pool_memory_ doldur, staging/target ayır
feat: ExternalMemoryBridge GL target pool + NT handle export
feat: GpuCopyOptimizer execute_copy gerçek vkCmdBlitImage + timeline signal
feat: PreviewWidget GL_EXT_memory_object_win32 interop texture
fix: main_window bridge wire
```

---

## Dikkat

- `VK_FORMAT_B8G8R8A8_UNORM` — DXGI ile eşleşiyor, değiştirme
- `VK_QUEUE_FAMILY_IGNORED` barrier'larda — cross-queue transfer yok
- `wglGetProcAddress` — sadece GL context aktifken çağır (initializeGL içinde)
- PowerShell'de build yapma — x64 Native Tools CMD kullan
- `shutdown()` sırasına dikkat: image destroy → memory free → handle close
