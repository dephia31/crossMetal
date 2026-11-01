# Test thiết bị thật & lấy log

Hướng dẫn từng bước để flash CrossMetal lên Metalio E-Ink4-Plus và lấy log gửi lại.

---

## Chuẩn bị

### Kiểm tra cáp trước tiên

Đây là nguyên nhân phổ biến nhất khiến "không thấy thiết bị": **cáp sạc không có
dây dữ liệu**. Cắm board vào, rồi chạy:

```bash
ls /dev/cu.* /dev/tty.usb* 2>/dev/null
system_profiler SPUSBDataType | grep -i -B2 -A6 '303a'
```

Kết quả mong đợi: một cổng như `/dev/cu.usbmodem1101` (trên macOS; trên Linux là
`/dev/ttyACM0`) và một khối USB có `303a` (VID của Espressif).

Nếu không thấy gì: đổi cáp, đổi cổng USB, thử lại. Đừng đi tiếp cho tới khi thấy
cổng serial.

Quyền truy cập: trên macOS không cần `sudo` và không cần nhóm `dialout` như
Linux. Nếu `idf.py` báo `Permission denied`, kiểm tra cổng có đang bị một chương
trình khác giữ (ví dụ `idf.py monitor`) rồi đóng nó lại.

---

## Bước 1 — Sao lưu (làm một lần, trước khi flash)

```bash
cd /Users/phanhuy/Docs/CrossMetal
. ~/esp/v6.1/export.sh

./scripts/backup_flash.sh /dev/cu.usbmodem1101
```

Thay `/dev/cu.usbmodem1101` bằng cổng thật bạn thấy ở bước kiểm tra cáp; tên cổng
thay đổi theo máy và theo lần cắm.

Mất vài phút, tạo file ~16 MB. **Đây là đường lùi duy nhất của bạn.** Giữ file đó.

Kiểm tra file hợp lệ:

```bash
ls -lh metalio-stock-*.bin
# phải đúng 16777216 bytes
```

---

## Bước 2 — Build

```bash
. ~/esp/v6.1/export.sh
idf.py --preview set-target esp32s31
idf.py --preview build
```

`--preview` bắt buộc với S31. Nếu quên, `idf.py` báo
`esp32s31 is still in preview`.

Kết quả mong đợi:

```
crossmetal.bin binary size 0x644c0 bytes. Smallest app partition is 0x500000 bytes.
Project build complete.
```

---

## Bước 3 — Bắt đầu bắt log TRƯỚC khi flash

Đây là điểm quan trọng về trình tự. Bạn cần log từ **dòng đầu tiên** sau khi
reset, nên hãy mở cửa sổ bắt log trước.

**Cửa sổ terminal 1** — bắt log:

```bash
cd /Users/phanhuy/Docs/CrossMetal
. ~/esp/v6.1/export.sh
./scripts/capture_boot_log.py /dev/cu.usbmodem1101 --seconds 60
```

Nó sẽ in log ra màn hình **đồng thời** ghi vào file
`bootlog-YYYYMMDD-HHMMSS.log`. Chờ nó báo đã mở cổng.

**Cửa sổ terminal 2** — flash:

```bash
cd /Users/phanhuy/Docs/CrossMetal
. ~/esp/v6.1/export.sh
idf.py --preview -p /dev/cu.usbmodem1101 flash
```

Sau khi flash xong, board tự reset và log sẽ chảy vào cửa sổ 1.

> Nếu cửa sổ 1 chưa bắt được gì: nhấn nút reset trên board, hoặc rút/cắm lại USB
> trong lúc script đang chạy. Script bắt đầu đọc từ lúc bạn khởi động nó, nên
> reset sau khi script chạy là cách chắc chắn nhất để không mất dòng đầu.

---

## Bước 4 — Đọc kết quả trên màn hình

Sau khi khởi động, firmware vẽ **pattern kiểm tra hình học**: nền trắng, viền đen
4 px đủ bốn cạnh, hai đường chéo, chữ thập ở tâm.

| Bạn thấy gì | Nghĩa là | Gửi tôi |
|:---|:---|:---|
| Viền sát cả 4 cạnh, chéo gặp đúng góc | **Hình học đúng** | Log + xác nhận |
| Xoay, soi gương, hoặc méo | Sai transform logic↔native | Log + **ảnh chụp màn hình** |
| Trắng trơn hoặc vẽ nửa chừng | Lỗi timing bus hoặc rail TPS | Log |
| Không có gì, `panel : FAIL` | PMIC không lên | Log |
| Bootloop (khởi động lặp lại) | Panic | Log |

**Ảnh chụp màn hình rất có giá trị** nếu pattern sai — nó cho biết sai kiểu gì
(xoay, lật, lệch) mà log không thể hiện được.

---

## Bước 5 — Gửi gì cho tôi

### Quan trọng nhất: file log

File `bootlog-*.log` trong thư mục dự án. Hoặc dán trực tiếp vào chat.

### Dòng tôi cần nhất

```
display needs 1663488 bytes of PSRAM (front+back+2x difference)
```

Dòng này cho biết máy bạn có đủ PSRAM hay không. `sdkconfig` của nhà sản xuất
**không khai báo dung lượng PSRAM**, nên đây là cách duy nhất để biết. Nếu log
báo `insufficient PSRAM`, đó là thông tin quyết định và tôi sẽ biết phải làm gì.

### Bảng tổng kết

```
--- bring-up summary ---
  ioexp : ok|FAIL
  tps   : ok|FAIL (I2C standby ACK only, not rails)
  rails : up|down (live PWR_GOOD)
  panel : ok|FAIL
  gauge : ok|FAIL
  chg   : ok|FAIL
  touch : ok|FAIL
  rtc   : ok|FAIL
  sd    : ok|FAIL
  battery: NN% NNNN mV charging|discharging|idle
```

### Nếu có panic

Log sẽ có khối kiểu:

```
Guru Meditation Error: Core 0 panic'ed (...)
Backtrace: 0x... 0x... 0x...
```

Gửi **nguyên khối đó**, kể cả backtrace. Tôi giải mã được địa chỉ thành tên hàm.

Để giải mã backtrace ngay trên máy bạn:

```bash
. ~/esp/v6.1/export.sh
idf.py --preview -p /dev/cu.usbmodem1101 monitor
# hoặc, nếu đã có file coredump trong flash:
idf.py coredump-info -p /dev/cu.usbmodem1101
```

Cách đơn giản nhất: chạy `idf.py monitor` khi board đang panic — nó tự động
giải mã backtrace.

---

## Bước 6 — Nếu cần khôi phục firmware gốc

```bash
./scripts/backup_flash.sh --restore /dev/cu.usbmodem1101 metalio-stock-<ngày>.bin
```

Có hỏi xác nhận trước khi ghi. Lệnh này ghi lại toàn bộ 16 MB, nên cả NVS, hiệu
chuẩn và hai slot OTA đều trở về nguyên trạng.

---

## Nếu không thấy cổng serial sau khi flash

Trình tự thử, từ khả năng cao nhất:

1. **Đổi cáp** — cáp sạc không có dây dữ liệu là nguyên nhân phổ biến nhất.
2. **Giữ nút BOOT rồi cắm USB, thả sau ~2 giây.**
   Tôi **chưa xác minh** điều này cho board Metalio (xem `docs/FLASHING_VI.md` mục 1d).
   Trên phần lớn board ESP32 thì đúng, nhưng tài liệu upstream không nói gì về
   quy trình khôi phục cho board này.
3. **Thử cổng USB khác** — firmware dùng UART0 và USB Serial/JTAG.
4. **Hỏi nhà sản xuất** nếu vẫn không được.

Để tránh rơi vào tình huống này: đừng `erase-flash`, và giữ bản sao lưu.

---

## Tóm tắt nhanh — dán vào terminal

```bash
cd /Users/phanhuy/Docs/CrossMetal
. ~/esp/v6.1/export.sh

# 1. sao lưu (chỉ lần đầu)
./scripts/backup_flash.sh /dev/cu.usbmodem1101

# 2. build
idf.py --preview set-target esp32s31
idf.py --preview build

# 3. bắt log ở cửa sổ này
./scripts/capture_boot_log.py /dev/cu.usbmodem1101 --seconds 60

# 4. ở cửa sổ khác: flash
idf.py --preview -p /dev/cu.usbmodem1101 flash
```

Sau đó gửi tôi file `bootlog-*.log`, và ảnh chụp màn hình nếu pattern hiển thị
sai.