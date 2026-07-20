# TALİMAT: Otomatik Yeniden Yükleme (Hot-Reload) — Regresyon Araştırması

**Kaynak:** Kullanıcının canlı GUI testi — "Otomatik yeniden yükle" işaretliyken
`rules.json` düzenlenip kaydedildi (hem geçerli hem bozuk JSON denendi),
**hiçbir bildirim (başarı ya da hata) durum çubuğunda hiç görünmedi.**
Dosyanın gerçekten diskte değiştiği doğrulandı (`dir /a` ile saat damgası
teyit edildi) — yani izleme/reload mekanizması hiç tetiklenmemiş
görünüyor.

**Bu, `TALIMAT_KURALLARI_DUZENLE.md`'nin (tamamlanmış, arşivlenmiş) bir
regresyonu veya o talimatın Faz 3'te "kullanıcıda kalan" diye işaretlediği
kısmın ilk gerçek testinde ortaya çıkan bir bug olabilir.**

---

## Görevin Özü

Kod yazmadan önce, sırayla şunları doğrula (Faz 0):

1. **Checkbox durumu gerçekten okunuyor mu?** chk_auto_reload'ın
   işaretlenip "Tamam"a basıldığında QSettings'e yazıldığını, ve
   QFileSystemWatcher'ın gerçekten kurulduğunu (constructor'da veya
   onAutoReloadToggled slot'unda) find-references ile izle.
2. **Watcher hangi yolu izliyor?** İzlenen path'in gerçek rules.json
   dosya yoluyla (rulesFilePath()) birebir eşleştiğini doğrula — yol
   uyuşmazlığı (örn. farklı case, farklı ayırıcı, sembolik link) sessiz
   bir "hiç tetiklenmeme" sebebi olabilir.
3. **fileChanged sinyali gerçekten bir slot'a bağlı mı?** connect()
   çağrısını bul, doğru slot'a (reloadRulesNow() veya benzeri) gittiğini
   teyit et.
4. **Checkbox varsayılan durumu ve kalıcılık:** Ayarlar penceresi
   exec() ile tekrar tekrar açılıyor (cache'lenmiş dialog) — checkbox
   durumunun her açılışta doğru yüklenip watcher'ın buna göre kurulup
   kurulmadığını (ya da yalnızca ilk açılışta bir kez kurulup sonraki
   toggle'ların etkisiz kaldığını) izle. Bu, önceki turda "ilk kayıtta
   çalışır sonra susar" riski olarak bilinen bir alandı — burada tam
   tersi bir sessizlik (hiç çalışmama) olabilir.
5. Kullanıcının izlediği adımları tam olarak yeniden oku (bu talimatın
   sonunda) ve hangi adımda kopukluk olabileceğini kod üzerinden izle.

**Faz 0 çıktısı:** Kopukluğun tam olarak nerede olduğu (checkbox
kaydedilmiyor mu, watcher kurulmuyor mu, path yanlış mı, sinyal bağlı
değil mi, yoksa başka bir şey mi). Onaya sun — düzeltme küçükse aynı
turda uygulanabilir.

---

## Kullanıcının izlediği adımlar (tam, referans için)

1. Ayarlar → Self-Healing → "Otomatik yeniden yükle (dosya
   değiştiğinde)" kutusunu işaretledi.
2. "Kuralları Düzenle..." ile dosyayı açtı (harici editör, doğru açıldı
   — bu kısım çalışıyor).
3. high_cpu_reduce_bitrate kuralının cpu_load_pct > 80 kısmını
   cpu_load_pct > 79 yaptı, kaydetti.
4. Reji Studio'ya geçti — hiçbir bildirim görünmedi.
5. Not Defteri'nde dosyayı tekrar açtı — değişikliğin diskte kalıcı
   olduğunu gördü (dosya gerçekten yazıldı).
6. JSON'u bilerek bozdu, kaydetti.
7. Reji Studio'ya geçti — yine hiçbir bildirim (ne kırmızı hata ne
   başka bir şey) görünmedi.
8. dir /a "%USERPROFILE%\.reji" ile klasörü kontrol etti — yalnızca
   rules.json var (saat damgası düzenlemeyle uyumlu), rules.json.backup
   yok (bu normal — backup yalnızca İçe Aktar akışında oluşuyor,
   hot-reload akışında değil, bu bir sorun değil).

## Sabit Kurallar

- Kod yazmadan önce Faz 0'ı tamamla, bulguyu raporla.
- Eğer düzeltme küçükse (örn. tek satırlık bir bağlantı eksikliği),
  onay sonrası aynı turda uygulanabilir — CLAUDE.md Bölüm 8b'ye göre
  dal kararı ver.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık
  olsun.
