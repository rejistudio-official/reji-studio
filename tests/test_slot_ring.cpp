// V8/I23 — GPU pool round-robin slot ilerlemesinin izole (GPU'suz) seam testi.
//
// I23'ün kök nedeni bileşenler-arası bir index uyuşmazlığıydı: bridge image-pool
// slot'u (execute_copy'ye giden image çiftini seçen) ile optimizer/widget'ın
// indexlemesi bağımsız sayaçlardı → drift. Düzeltme, TEK round-robin kaynağı
// (next_pool_slot) üretip slot'u zincir boyunca taşımak.
//
// DÜRÜSTLÜK NOTU: Asıl bug bileşenler-arası (bridge↔optimizer↔widget) eşleşmedir;
// tek-bileşen birim testi bunu TAM yakalayamaz. Bu test yalnız (1) round-robin
// ilerlemenin determinizmini ve (2) "üretilen slot sırası == tüketilen slot
// sırası" değişmezini — bridge'in üret-ilerlet döngüsünün saf modelinde —
// doğrular. execute_copy'nin gerçek GPU yolu inert (WGC aktif) olduğundan
// runtime'da hiç çalıştırılmadı.
#include "slot_ring.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using rj::pipeline::gpu::next_pool_slot;
using rj::constants::kGpuPoolSize;

// next_pool_slot [0, kGpuPoolSize) üzerinde sarar: 0→1→2→0 (POOL=3).
TEST(SlotRing, WrapsAroundPoolSize) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(kGpuPoolSize) * 4u; ++i) {
        EXPECT_EQ(s, i % static_cast<uint32_t>(kGpuPoolSize));
        s = next_pool_slot(s);
    }
}

// Sınırdan (POOL_SIZE-1) sonraki adım 0'a döner — off-by-one koruması.
TEST(SlotRing, LastSlotWrapsToZero) {
    const uint32_t last = static_cast<uint32_t>(kGpuPoolSize) - 1u;
    EXPECT_EQ(next_pool_slot(last), 0u);
}

// I23 değişmezi: bridge her frame'de KULLANDIĞI slot'u raporlar, SONRA ilerler.
// Tüketici (widget→execute_copy) raporlanan slot'u kullanır. Raporlanan sıra,
// gerçekte kullanılan (image çiftini seçen) sırayla birebir aynı olmalı — bu,
// eski "widget last_used_slot() okur ama bridge ayrı sayaçtan üretir" driftini
// dışlar. ExternalMemoryBridge::get_frame_images'ın üret-ilerlet döngüsünün
// saf modeli (ext_bridge_get_frame_images GPU'ya girer, burada dışlanır).
TEST(SlotRing, ReportedSlotMatchesConsumedSlot) {
    uint32_t bridge_slot = 0;  // ExternalMemoryBridge::get_frame_images static slot
    std::vector<uint32_t> reported;   // *out_slot ile çağırana bildirilen
    std::vector<uint32_t> consumed;   // image çiftini fiilen seçmek için kullanılan

    for (int frame = 0; frame < 7; ++frame) {
        consumed.push_back(bridge_slot);          // ext_bridge_get_frame_images(tex, slot, …)
        const uint32_t out_slot = bridge_slot;    // if (out_slot) *out_slot = slot;
        reported.push_back(out_slot);
        bridge_slot = next_pool_slot(bridge_slot);  // slot = next_pool_slot(slot);
    }

    EXPECT_EQ(reported, consumed);
    // Ve dizi deterministik round-robin: 0,1,2,0,1,2,0
    const std::vector<uint32_t> expected = {0, 1, 2, 0, 1, 2, 0};
    EXPECT_EQ(reported, expected);
}
