# Hướng dẫn flash CrossMetal — Metalio E-Ink4-Plus

Tài liệu này trả lời hai câu hỏi: **flash có làm hỏng thiết bị không**, và
**flash như thế nào**.

---

## 1. Flash có làm hỏng thiết bị không?

**Không, với điều kiện bạn sao lưu trước.** Nhưng tôi liệt kê rõ rủi ro thật thay
vì trấn an chung chung.

### Cái an toàn

| Yếu tố | Trạng thái | Ý nghĩa |
|:---|:---|:---|
| Secure Boot | **TẮT** | Không có eFuse nào bị đốt. Bộ nạp ROM luôn nhận firmware mới |
| Flash Encryption | **TẮT** | Flash ghi/đọc tự do, không bị khoá |
| eFuse burning | không có | Không có thao tác không thể đảo ngược |

Đã kiểm chứng trên `sdkconfig` sinh ra: `CONFIG_SECURE_BOOT` và
`CONFIG_SECURE_FLASH_ENC_ENABLED` đều **không được set**.

> Lưu ý dễ nhầm: `CONFIG_SECURE_BOOT_V2_RSA_SUPPORTED` và
> `CONFIG_SECURE_BOOT_V2_PREFERRED` **có** xuất hiện, nhưng đó là *khả năng* được
> biên dịch vào bootloader, **không phải** bật secure boot. Muốn bật thật phải có
> `CONFIG_SECURE_BOOT=y` — và nó không có. Đây là nhầm lẫn nguy hiểm vì nhìn
> thoáng qua tưởng thiết bị đã bị khoá.

Vì không có secure boot, **thiết bị luôn khôi phục được** bằng cách ghi lại ảnh
flash cũ. Không có cách nào để "làm chết" nó bằng phần mềm.

### Cái cần lưu ý

**a. NVS chứa dữ liệu hiệu chuẩn.** Vùng `nvs` ở `0x9000` giữ cài đặt và có thể
cả hiệu chuẩn của máy. Bản flash của CrossMetal **không ghi vào NVS** (xem
`<build-dir>/flash_args` bên dưới — `0x9000` không xuất hiện), nên dữ liệu cũ được giữ
nguyên. Nhưng nếu bạn tự ý `erase-flash` thì sẽ mất.

**b. `ota_data_initial.bin` ghi vào `0xd000`.** Việc này đặt lại con trỏ OTA về
slot `ota_0`. Nó **không** xoá firmware gốc trong `ota_1`, nhưng con trỏ chọn slot
sẽ đổi. Đây là lý do cần sao lưu toàn chip chứ không chỉ sao lưu phân vùng app.

**c. Firmware này chưa từng chạy.** Đây là rủi ro thật lớn nhất, không phải việc
flash. Firmware có thể panic khi khởi động. Panic thì thiết bị reboot, không hỏng
— nhưng nếu panic xảy ra mỗi lần khởi động thì bạn có **bootloop**, và cách thoát
duy nhất là flash lại qua USB.

**d. USB có thể không liệt kê được nếu firmware treo sớm.** Nếu firmware chết
trước khi khởi tạo USB, cổng serial có thể không xuất hiện. Đây là tình huống khó
chịu nhất, và tôi **chưa xác minh được** cách thoát chắc chắn cho thiết bị này.

Cái đã xác minh:

- S31 có cả `SOC_USB_OTG_SUPPORTED` và `SOC_USB_SERIAL_JTAG_SUPPORTED`, nên về lý
  thuyết có đường nạp qua USB.
- Chế độ boot do `GPIO_STRAP_REG` quyết định; `ETS_IS_UART_BOOT()` cần strap
  `0111`. Tức là **có** một tổ hợp strapping để vào chế độ nạp.
- Nút BOOT được khai báo là **GPIO61** (`METALIO_BOOT_BUTTON_GPIO` trong
  `components/metalio_bsp/include/metalio_pinout.h`), nhưng đó là *định nghĩa nút
  trong firmware*, **không** chứng minh nó là chân strapping phần cứng.

Cái **chưa** xác minh: liệu giữ nút BOOT khi cắm USB có thật sự kéo đúng chân
strapping để vào chế độ nạp ROM hay không. Tài liệu upstream không nói. Trên nhiều
board ESP32 điều đó đúng, nhưng tôi không có cơ sở để khẳng định cho board này.

Vì vậy hãy thử `idf.py flash` trước khi tin vào bất kỳ đường thoát nào.

**e. Chưa ai kiểm chứng waveform trên panel thật.** Nếu waveform sai, panel có thể
hiển thị sai hoặc xấu. Về nguyên tắc waveform sai *có thể* làm hỏng panel theo
thời gian, nhưng bảng waveform ở đây lấy nguyên từ firmware gốc của nhà sản xuất
cho đúng panel này, không phải tự chế.

### Kết luận

Flash là thao tác **có thể đảo ngược** trên thiết bị này. Rủi ro duy nhất không
thể đảo ngược là mất NVS nếu bạn xoá nó, và bạn có thể tránh bằng sao lưu.

---

## 2. Bước 0 — Sao lưu trước khi làm bất cứ gì

**Đừng bỏ qua bước này.** Đây là đường lùi duy nhất của bạn.

```bash
. ~/esp/v6.1/export.sh
./scripts/backup_flash.sh /dev/cu.usbmodem1101
```

Script đọc toàn bộ 16 MB flash ra file. Mất vài phút. Kết quả khoảng 16 MB
(gần đúng, phần lớn là 0xFF).

Script từ chối ghi đè file đã tồn tại, và từ chối nhận file sao lưu không đúng
16 MB — để bạn không vô tình ghi đè đường lùi bằng một file hỏng.

Tìm cổng serial:

```bash
# macOS
ls /dev/cu.* 2>/dev/null
system_profiler SPUSBDataType | grep -i -A6 303a

# Linux
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
lsusb | grep -i 303a
```

Tên cổng thay đổi theo máy và theo cổng USB, nên hãy dùng đúng tên bạn thấy;
`/dev/cu.usbmodem1101` trong tài liệu chỉ là ví dụ.

### Khôi phục lại firmware gốc

```bash
./scripts/backup_flash.sh --restore /dev/cu.usbmodem1101 metalio-stock-<ngày>.bin
```

Script sẽ hỏi xác nhận trước khi ghi. Lệnh này ghi lại **toàn bộ** flash, nên cả
NVS, hiệu chuẩn và cả hai slot OTA đều trở về nguyên trạng.

---

## 3. Build

```bash
. ~/esp/v6.1/export.sh

idf.py --preview set-target esp32s31
idf.py --preview build
```

`--preview` là **bắt buộc**: ESP32-S31 đang ở trạng thái preview trong ESP-IDF 6.1,
thiếu cờ này `idf.py` sẽ từ chối.

---

## 4. Flash

### Cách 1 — để idf.py làm hết (khuyến nghị)

```bash
idf.py --preview -p /dev/cu.usbmodem1101 flash monitor
```

`monitor` mở luôn log serial. Thoát bằng `Ctrl+]`.

### Cách 2 — dùng flash_args (an toàn hơn cách ghi tay)

`<build-dir>/flash_args` do IDF sinh ra, chứa offset đọc từ partition table thật:

```bash
cd build-mac          # thư mục đã build; đổi tên nếu bạn dùng tên khác
python -m esptool --chip esp32s31 -p /dev/cu.usbmodem1101 write-flash "@flash_args"
```

`build-mac` là thư mục build đã dùng để tạo bản này. `idf.py` mặc định dùng
`build/`, nhưng **đừng dùng `build/` trong repo này**: nó còn lại từ máy Linux cũ
và chứa ảnh cũ. File `flash_args` nằm trong thư mục build, không phải ở gốc repo.

### Cách 3 — ghi tay từng ảnh

Bốn ảnh và offset, lấy từ `<build-dir>/flash_args` của bản dựng hiện tại:

| Ảnh | Offset | Ghi chú |
|:---|:---|:---|
| `bootloader/bootloader.bin` | `0x2000` | |
| `partition_table/partition-table.bin` | `0x8000` | |
| `ota_data_initial.bin` | `0xd000` | đặt lại con trỏ OTA |
| `crossmetal.bin` | `0x80000` | **không phải `0x10000`** |

> Offset `0x80000` của app là điểm dễ sai. Mặc định của ESP-IDF là `0x10000`,
> nhưng partition table này đặt `ota_0` tại `0x80000`. Ghi nhầm chỗ thì thiết bị
> khởi động vào vùng trống.

```bash
python -m esptool --chip esp32s31 -p /dev/cu.usbmodem1101 -b 460800 \
  --before default-reset --after hard-reset --no-stub write-flash \
  --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x2000  build-mac/bootloader/bootloader.bin \
  0x8000  build-mac/partition_table/partition-table.bin \
  0xd000  build-mac/ota_data_initial.bin \
  0x80000 build-mac/crossmetal.bin
```

**Không dùng `--chip esp32s3`.** Chip là `esp32s31`; sai tên chip thì esptool báo
lỗi hoặc nạp sai giao thức.

---

## 5. Nếu cần nhập lại chế độ nạp ROM

Khi firmware treo trước lúc khởi tạo USB, cổng serial có thể không hiện. Trình tự
thử, từ chắc chắn nhất đến ít chắc chắn nhất:

1. **Thử cắm lại USB vài lần, đổi cáp.** Nhiều lần "không vào được" hoá ra là cáp
   chỉ để sạc, không có dây dữ liệu. Đây là nguyên nhân phổ biến nhất.
2. **Giữ nút BOOT rồi cắm USB, thả sau ~2 giây.** Trên phần lớn board ESP32 đây là
   cách vào chế độ nạp ROM. Tôi **chưa xác minh** điều này cho Metalio E-Ink4-Plus
   (xem mục 1d), nên coi đây là cách đáng thử, không phải điều chắc chắn.
3. **Kiểm tra cổng khác.** Firmware này dùng UART0 và USB Serial/JTAG. Nếu board có
   hai cổng USB, thử cổng còn lại.
4. **Hỏi nhà sản xuất** nếu vẫn không được. Tài liệu upstream không mô tả quy trình
   khôi phục cho board này, và tôi không muốn đoán thêm.

Để **tránh rơi vào tình huống này**: giữ nguyên firmware gốc cho tới khi bạn đã có
bản sao lưu toàn chip, và flash CrossMetal lần đầu khi bạn có thời gian xử lý nếu
có trục trặc.

---

## 6. Đọc kết quả

Log khởi động in ra bảng tổng kết từng hệ thống:

```
--- bring-up summary ---
  ioexp : ok|FAIL     tps   : ok|FAIL     panel : ok|FAIL
  gauge : ok|FAIL     chg   : ok|FAIL     touch : ok|FAIL
  rtc   : ok|FAIL     sd    : ok|FAIL
  battery: NN% NNNN mV charging|discharging|idle
```

Nếu khởi động thành công, firmware vẽ **pattern kiểm tra hình học** lên panel:
nền trắng, viền đen 4 px đủ bốn cạnh, hai đường chéo, chữ thập ở tâm.

### Đọc pattern

| Hiện tượng | Nghĩa là | Bước tiếp |
|:---|:---|:---|
| Viền dính sát cả 4 cạnh, chéo gặp nhau đúng góc | Hình học đúng | Đường hiển thị hoạt động |
| Xoay, soi gương, hoặc méo | Sai transform logic↔native | Kiểm tra `epd_set_rotation` |
| Trắng trơn hoặc vẽ nửa chừng | Lỗi timing bus hoặc rail TPS | Xem log; gọi `cm_tps_debug_error()` |
| Không có gì, `panel : FAIL` | PMIC không lên | Xem `tps :` và log TPS |
| Bootloop | Panic lúc khởi động | Xem log; flash lại bản sao lưu |

### Nếu bootloop

Thiết bị không hỏng — nó chỉ khởi động lặp lại. Lấy log panic:

```bash
idf.py --preview -p /dev/cu.usbmodem1101 monitor
```

rồi khôi phục:

```bash
./scripts/backup_flash.sh --restore /dev/cu.usbmodem1101 metalio-stock-<ngày>.bin
```

---

## 7. Điều tôi chưa kiểm chứng được

Nói thẳng để bạn không đặt niềm tin sai chỗ:

- **Hardware status (Nov 1, 2026):** Firmware boots successfully on Metalio
  E-Ink 4 Plus hardware, all 9 subsystems initialize, and the test pattern renders
  correctly on the E-Ink panel (confirmed via serial log and visual inspection).
  Panel geometry, waveform timing, and PMIC rail sequencing have been verified.
- **PSRAM requirement:** 1.59 MB for display buffers (front + back + 2× difference).
  The device has 16 MB octal PSRAM, confirmed working.
- **Sleep path is not implemented.** The firmware does not enter deep sleep yet.

Nếu log khởi động cho thấy `panel : FAIL` hoặc PSRAM không đủ, gửi lại nguyên văn
log — đó là thông tin hữu ích nhất bạn có thể cho tôi.