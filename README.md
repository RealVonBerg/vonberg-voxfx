# VonBerg Vox FX

Aranje için vokal efekt aracı (DJ tool tarzı). Techno, acid, trap için. (VST3, Windows, FL Studio)
İki şekilde çalışır: **canlı** (vokal kanalında, projeyle senkron) ve **yakala / sürükle** (efektli vokali playlist'e ses olarak bırak).

## 10 efekt
| Pad | Ne yapar | Ayar |
|---|---|---|
| STUTTER | Bastığın anı grid'e göre tekrarlar | 1/2 … 1/32, RAMP (1/4 → 1/64 hızlanan build) |
| REVERSE | Son vuruşu ters çalar | 1/2, 1/4, 1/8, 1 bar |
| TAPE STOP | Kaset gibi yavaşlayıp durur | 1/4, 1/2, 1 bar, 2 bar |
| GLITCH | Her 1/16'da rastgele kesme / tekrar / ters / oktav düşürme (çart curt) | miktar, varyasyon |
| PITCH | Perde kaydırma (-12 = demon vokal) | -12, -7, -5, +5, +7, +12 |
| FILTER | HP BUILD (yükselen), LP DROP (kapanan), ACID (1/8 wobble), PHONE | mod, rezonans |
| CRUSH | Bit + sample rate düşürme | miktar |
| GATE | Ritmik kesme | 1/8, 1/16, 1/32, 1/8T, 1/16T, derinlik |
| DELAY | Ping-pong yankı atışı, bırakınca devam eder | 1/4, 1/8D, 1/8, 1/16, feedback |
| REVERB | Reverb atışı, kuyruk kalır; FREEZE ile dondurma | boyut, freeze |

## Canlı kullanım
1. Plugin'i vokal kanalının mixer slotuna ekle.
2. **Pad'e basılı tut** → efekt o an devreye girer, bırakınca çıkar. **LATCH** açıksa tıklayınca açık kalır.
3. Her pad'in "On" parametresi var: FL'de kayıtta pad'lere basarsan **otomasyon** olarak kaydedilir
   ya da parametreye sağ tık → *Create automation clip* ile istediğin yere çizersin.
4. **SEQUENCER**: 10 efekt × 16 adım. Hücrelere tıkla / sürükleyerek boya. Uzunluk 1, 2 veya 4 bar.
   Hazır desenler (◀ ▶): Trap Stutter Fill, Techno Gate, Glitch Chop, Vocal Throw, Tape Stop Ending,
   Reverse + Stutter, Pitch Drop, Crush Hits, Build (HP + Stutter), Acid Chop. **RANDOM** yeni fikir üretir.

## Yakala / sürükle (print)
1. Vokal dosyasını üstteki alana **bırak** (veya OPEN) ya da **REC**'e bas: FL çalıyorsa bir sonraki bardan itibaren kaydeder,
   tekrar basınca durur (en fazla 30 sn).
2. Vokal; sequencer + açık (LATCH'li) pad'lerle işlenir. Ayar değiştikçe otomatik yeniden işlenir.
3. **REVERSE SWELL**: vokalin önüne gerçek reverse reverb ekler (1/2, 1 bar, 2 bar). **TAIL**: reverb/delay kuyruğu için ek süre.
4. **PREVIEW** ile dinle, dalga formunu **basılı tutup playlist'e sürükle** (WAV). Dosyalar `Belgeler\VonBerg Vox FX\` içine de kaydedilir.
   Swell açıksa klibi vokalin başladığı yerden swell uzunluğu kadar önce bırak (ekranda SWELL / VOCAL çizgisi görünür).

## Windows için derleme (GitHub)
1. Depoda **Actions** → "Build Windows VST3" her yüklemede kendiliğinden çalışır (~10 dk).
2. Bitince en altta **VonBerg-VoxFX-VST3-Windows** dosyasını indir.
3. Zip'teki `VonBerg Vox FX.vst3` klasörünü `C:\Program Files\Common Files\VST3\` içine kopyala.
4. FL Studio → Options → Manage plugins → Find plugins.

## Testler (Linux/Mac)
```
cmake -B build-test -DVOX_TESTS_ONLY=ON -DVOX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-test
./build-test/EngineTest_artefacts/Release/EngineTest        # her efektin DSP doğruluğu, click / NaN kontrolü
./build-test/PluginTest_artefacts/Release/PluginTest /tmp   # pad, latch, sequencer, dosya, REC, render, state
```
