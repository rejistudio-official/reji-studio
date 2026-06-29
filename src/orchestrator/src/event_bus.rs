//! Event Bus — Reji Studio'nun merkezi mesajlaşma sistemi.
//! Tüm katmanlar bu bus üzerinden iletişim kurar.

use tokio::sync::broadcast;

/// Medya pipeline'dan gelen olaylar
#[derive(Debug, Clone)]
pub enum MediaEvent {
    /// Kaynak bağlantısı koptu
    SourceDisconnected { source_id: u32 },
    /// Kaynak yeniden bağlandı
    SourceReconnected  { source_id: u32 },
    /// Kare düştü
    FrameDropped       { count: u32 },
    /// Encode hatası
    EncodeError        { code: i32 },
}

/// Sistem kaynak olayları
#[derive(Debug, Clone)]
pub enum SystemEvent {
    /// CPU kullanımı (0.0 - 1.0)
    CpuUsage    { ratio: f32 },
    /// GPU kullanımı (0.0 - 1.0)
    GpuUsage    { ratio: f32 },
    /// Bellek kullanımı (0.0 - 1.0)
    MemUsage    { ratio: f32 },
    /// Disk dolmak üzere
    DiskWarning { free_mb: u64 },
    /// Ağ istatistikleri
    NetworkStats { rtt_ms: u32, loss_pct: f32 },
}

/// Kullanıcı eylemleri
#[derive(Debug, Clone)]
pub enum UserEvent {
    /// Sahne değişimi isteği
    SceneSwitch    { scene_id: u32 },
    /// Makro tetiklendi
    MacroTriggered { macro_id: u32 },
    /// Yayın başlat
    StreamStart,
    /// Yayın durdur
    StreamStop,
}

/// Self-healing aksiyonları
#[derive(Debug, Clone)]
pub enum HealingEvent {
    /// Fallback sahneye geç
    ActivateFallback { reason: String },
    /// Plugin bypass moda al
    BypassPlugin     { plugin_id: u32 },
    /// Bitrate düşür
    ReduceBitrate    { target_kbps: u32 },
    /// Preview FPS kısıt
    ReducePreviewFps { target_fps: u32 },
    /// Codec hafiflet
    LightenCodec,
    /// Normal moda dön
    RestoreNormal,
}

/// Event Bus yapısı — tüm kanal türlerini barındırır
pub struct EventBus {
    pub media:   broadcast::Sender<MediaEvent>,
    pub system:  broadcast::Sender<SystemEvent>,
    pub user:    broadcast::Sender<UserEvent>,
    pub healing: broadcast::Sender<HealingEvent>,
}

impl EventBus {
    /// Yeni bir EventBus oluştur
    pub fn new() -> Self {
        let (media,   _) = broadcast::channel(256);
        let (system,  _) = broadcast::channel(256);
        let (user,    _) = broadcast::channel(64);
        let (healing, _) = broadcast::channel(64);
        Self { media, system, user, healing }
    }

    /// MediaEvent gönder
    pub fn send_media(&self, event: MediaEvent) {
        let _ = self.media.send(event);
    }

    /// SystemEvent gönder
    pub fn send_system(&self, event: SystemEvent) {
        let _ = self.system.send(event);
    }

    /// UserEvent gönder
    pub fn send_user(&self, event: UserEvent) {
        let _ = self.user.send(event);
    }

    /// HealingEvent gönder
    pub fn send_healing(&self, event: HealingEvent) {
        let _ = self.healing.send(event);
    }
}

impl Default for EventBus {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_event_bus_send_receive() {
        let bus = EventBus::new();
        let mut rx = bus.media.subscribe();

        bus.send_media(MediaEvent::FrameDropped { count: 3 });

        let event = rx.recv().await.unwrap();
        match event {
            MediaEvent::FrameDropped { count } => assert_eq!(count, 3),
            _ => panic!("Yanlis event"),
        }
    }

    #[tokio::test]
    async fn test_healing_event() {
        let bus = EventBus::new();
        let mut rx = bus.healing.subscribe();

        bus.send_healing(HealingEvent::ReduceBitrate { target_kbps: 4000 });

        let event = rx.recv().await.unwrap();
        match event {
            HealingEvent::ReduceBitrate { target_kbps } => assert_eq!(target_kbps, 4000),
            _ => panic!("Yanlis event"),
        }
    }
}