# PreviewWidget Render Path Seçimi — Tasarım Belgesi

**Tarih:** 2026-06-01  
**Durum:** Onaylandı  
**Kapsam:** `src/pipeline/`, `src/ui/preview_widget.*`, `src/ui/main_window.cpp`

---

## Amaç

PreviewWidget'ın GPU yükleme yolunu başlatma sırasında otomatik olarak seçmek:

- **NVIDIA (vendor_id = 0x10DE)** → `kNvDxInterop` path (şimdilik stub; PBO kodu çalışır)
- **Diğerleri** → `kPbo` path (ping-pong PBO, production implementasyonu)

Vendor bilgisi `DxgiCapturePipeline::gpu_scan()` üzerinden `rj::Pipeline::display_vendor_id()` ile alınır.

---

## Veri Akışı

```
DxgiCapturePipeline::gpu_scan_.entries[0].vendor_id
        ↓
rj::Pipeline::display_vendor_id()     [YENİ — pipeline.h + pipeline.cpp]
        ↓
MainWindow::MainWindow()
  pipeline_.init(pcfg) başarılı olursa:
  preview_widget_->selectRenderPath(pipeline_.display_vendor_id())
        ↓
PreviewWidget::selectRenderPath(vendor_id)   [YENİ]
  0x10DE → render_path_ = kNvDxInterop  (log: "NV_DX_INTEROP stub, PBO çalışır")
  diğer  → render_path_ = kPbo
```

---

## Değişen Dosyalar

| Dosya | Değişiklik |
|---|---|
| `src/pipeline/include/pipeline.h` | `display_vendor_id()` bildirimi |
| `src/pipeline/pipeline.cpp` | `display_vendor_id()` implementasyonu |
| `src/ui/preview_widget.h` | `RenderPath` enum, `selectRenderPath()`, `renderPath()` |
| `src/ui/preview_widget.cpp` | `Impl` PBO alanları, `selectRenderPath()`, `initializeGL()`, `paintGL()` |
| `src/ui/main_window.cpp` | `pipeline_.init()` sonrası `selectRenderPath()` çağrısı |

---

## `rj::Pipeline::display_vendor_id()`

```cpp
// pipeline.h
uint32_t display_vendor_id() const;

// pipeline.cpp — Pipeline::Impl::capture (unique_ptr<DxgiCapturePipeline>)
uint32_t Pipeline::display_vendor_id() const {
    if (!impl_ || !impl_->capture) return 0;
    const auto& scan = impl_->capture->gpu_scan();
    return scan.count > 0 ? scan.entries[0].vendor_id : 0;
}
```

`entries[0]` display adapter'ı temsil eder (find_display_adapter sırası). Init olmamışsa `0` döner.

---

## PreviewWidget Arayüzü

### preview_widget.h eklemeleri

```cpp
enum class RenderPath { kPbo, kNvDxInterop };

// vendor_id'ye göre render path seçer ve PBO'ları hazırlar.
// pipeline_.init() başarılı olduktan sonra bir kez çağrılır.
void selectRenderPath(uint32_t vendor_id);

// Mevcut path — log ve test için.
RenderPath renderPath() const;
```

### Impl eklemeleri

```cpp
RenderPath render_path { RenderPath::kPbo };
GLuint     pbo[2]      { 0, 0 };
int        pbo_idx     { 0 };    // write index; ^1 = read index
int        pbo_frame   { 0 };    // 0 = ilk frame guard aktif
size_t     pbo_size    { 0 };    // width * height * 4 (byteCount() kullanılmaz)
```

### `selectRenderPath()` davranışı

- `vendor_id == 0x10DE` → `render_path = kNvDxInterop`, stderr'e log yazar
- diğer → `render_path = kPbo`
- Her iki path de PBO alanlarını aynı şekilde başlatır; NvDxInterop stub olduğu için ayrışma yalnızca `render_path` field'ında görünür

---

## PBO Ping-Pong Implementasyonu

### `initializeGL()` değişikliği

PBO nesneleri `initializeGL()`'de oluşturulur:

```cpp
glGenBuffers(2, d_->pbo);
// İçerik lazy — boyut ilk frame'de belli olur
```

### `paintGL()` ping-pong akışı

```
frame_mutex kilitle, img al, frame_dirty kontrol et

pbo_size_now = img.width() * img.height() * 4

[Boyut değişimi kontrolü]
  pbo_size_now != d_->pbo_size  →
    her iki PBO'yu orphan et: glBufferData(size, nullptr, GL_STREAM_DRAW)
    pbo_size = pbo_size_now
    pbo_frame = 0          ← guard sıfırla
    tex_id yeniden oluştur (glDeleteTextures + glGenTextures)

write_idx = d_->pbo_idx
read_idx  = d_->pbo_idx ^ 1

[CPU → PBO DMA]
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo[write_idx])
  glBufferData(GL_PIXEL_UNPACK_BUFFER, pbo_size, img.constBits(), GL_STREAM_DRAW)
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0)

[PBO → GPU DMA — frame guard]
  if pbo_frame >= 1:
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo[read_idx])
    glTexSubImage2D(... offset = nullptr)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0)

d_->pbo_idx ^= 1
d_->pbo_frame++
frame_mutex bırak
```

**Frame 0 guard:** `pbo_frame < 1` iken read PBO'dan okuma yapılmaz. Boş PBO'dan okuma bozuk ilk kare üretir.

**Boyut değişimi:** `pbo_frame = 0` sıfırlanır, aksi takdirde guard çalışmaz ve orphan edilen PBO'dan okuma yapılır.

**pbo_size hesabı:** `width * height * 4` (sabit BGRA → RGBA8888 dönüşümü). `img.sizeInBytes()` / `byteCount()` Qt versiyonlarında farklı davranabilir — doğrudan hesap kullanılır.

### `~PreviewWidget()` değişikliği

```cpp
glDeleteBuffers(2, d_->pbo);
```

---

## MainWindow Değişikliği

```cpp
if (!pipeline_.init(pcfg)) {
    // mevcut hata yolu
} else {
    preview_widget_->selectRenderPath(pipeline_.display_vendor_id());
}
```

---

## NV_DX_INTEROP Stub Sözleşmesi

- `render_path_ == kNvDxInterop` set edilir
- `uploadFrame()` ve `paintGL()` PBO kodunu çalıştırır (değişiklik yok)
- `renderPath()` accessor'ı test veya ilerideki implementasyonun path'i kontrol etmesi için vardır
- Gerçek `wglDXRegisterObjectNV` implementasyonu bu tasarımın kapsamı dışında

---

## Test Kriterleri

- [ ] AMD iGPU sistemde `renderPath()` == `kPbo` döner
- [ ] NVIDIA sistemde `renderPath()` == `kNvDxInterop` döner, stderr'de log görünür
- [ ] İlk frame bozuk görünmez (guard çalışıyor)
- [ ] Çözünürlük değişiminde (boyut değişimi) preview kurtarılır
- [ ] `glDeleteBuffers` destructor'da çağrılır (leak yok)
