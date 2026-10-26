# Zygisk-Il2CppDumper (v3.0.0 Dynamic Edition)

Runtime IL2CPP Dumper module cho **Magisk (Zygisk)**, **KernelSU (ZygiskNext)** và **APatch (ZygiskNext)**.
Trích xuất toàn bộ metadata, C# headers, string literals và native exports trực tiếp từ bộ nhớ RAM runtime, hỗ trợ các game được bảo vệ/mã hóa metadata.

---

## 🌟 Tính Năng Nổi Bật (v3.0.0)

1. **Dynamic Runtime Offset Extraction (Không Hardcode)**:
   - Tự động trích xuất toàn bộ method RVA (`Class$$Method = 0x<RVA>`) và field offset (`Class::Field = 0x<Offset>`) trực tiếp từ cấu trúc runtime IL2CPP.
   - **Không bị lỗi khi game update**: Khi game cập nhật phiên bản mới, chỉ cần mở game để dumper tự động tạo ra file offset mới với địa chỉ chính xác 100%.
2. **Zero OOM / Streaming Buffered I/O**:
   - Sử dụng bộ đệm I/O 64KB (`pubsetbuf`) ghi trực tiếp xuống bộ nhớ máy, không bao giờ bị tràn RAM (OOM) kể cả với game lớn có hàng chục nghìn class.
3. **Native ELF Export Dumper (`libanogs.so`, `libil2cpp.so`, `libunity.so`)**:
   - Tự động duyệt dynamic symbol table (`DT_HASH` / `DT_GNU_HASH`) của các thư viện ELF nạp trong tiến trình, xuất danh sách native symbols vào `native_exports.txt`.
4. **RAM Decrypted String Literals**:
   - Tự động quét và giải mã chuỗi string literal từ RAM (`stringliteral.json`).
5. **Toast Thông Báo Trực Quan**:
   - Hiện Android Toast khi bắt đầu dump và khi hoàn thành, không cần cắm cáp xem logcat.

---

## 📦 Các File Kết Quả Xuất Ra (`/sdcard/Bo_dump/`)

Sau khi game tải xong tài nguyên, dumper sẽ tự động ghi toàn bộ file vào thư mục `/sdcard/Bo_dump/`:

| Tên File | Mô Tả |
|---|---|
| `dump.cs` | Toàn bộ cấu trúc C# giả lập (class, struct, enum, field offset, method signature, RVA, VA). |
| `script.json` | Script chứa địa chỉ hàm và string để nạp vào **IDA Pro** hoặc **Ghidra** qua plugin Il2CppDumper. |
| `stringliteral.json` | Danh sách chuỗi ký tự (String Literals) trích xuất trực tiếp từ header metadata trong RAM. |
| `offset.txt` | Danh sách phẳng của toàn bộ method và field, định dạng chuẩn để người dùng mở ra tìm kiếm (Ctrl+F) hàm mình cần. |
| `native_exports.txt` | Toàn bộ export symbol từ `libanogs.so` (`ANOGS_...`), `libil2cpp.so`, `libunity.so`. |

---

## 🎮 Cách Đổi Tên Gói Ứng Dụng Cần Dump (Target Package)

Mặc định dumper đã tích hợp sẵn các gói:
- `com.dts.freefireth`
- `com.dts.freefiremax`
- `com.dts.freefirevn`
- `com.dts.freefire`

Nếu bạn muốn dump game hoặc ứng dụng khác, hãy dùng một trong các cách sau:

### Cách 1: Đổi qua file `/sdcard/Bo_dump/target.txt` (Khuyên dùng - Không cần gõ lệnh dài)
Mở bất kỳ trình quản lý tệp nào (ZArchiver, MT Manager, ES File Explorer...), tạo hoặc sửa file:
```text
/sdcard/Bo_dump/target.txt
```
Điền package name cần dump, ví dụ:
```text
com.miHoYo.GenshinImpact
```
> **Mẹo:** Nếu bạn điền ký tự `*` vào file `target.txt`, module sẽ dump bất kỳ game Unity nào được khởi chạy!

Hoặc chạy nhanh qua lệnh terminal (Termux / ADB Shell):
```bash
echo "com.your.game" > /sdcard/Bo_dump/target.txt
```

### Cách 2: Đổi qua file `/data/local/tmp/il2cpp_target.txt`
```bash
su -c "echo com.your.game > /data/local/tmp/il2cpp_target.txt"
```

### Cách 3: Đổi qua Android System Property (Không cần tạo file)
```bash
su -c "setprop persist.il2cppdumper.package com.your.game"
```

---

## ⚙️ Công Tắc Bật / Tắt Module (Master Switch)

Nếu bạn muốn tạm thời tắt dumper mà không cần gỡ module trong Magisk/KernelSU:

- **Tắt dumper:**
  ```bash
  su -c "touch /data/local/tmp/il2cppdumper_disabled"
  # Hoặc:
  su -c "setprop persist.il2cppdumper.enabled 0"
  ```
- **Bật lại dumper:**
  ```bash
  su -c "rm /data/local/tmp/il2cppdumper_disabled"
  # Hoặc:
  su -c "setprop persist.il2cppdumper.enabled 1"
  ```

---

## 🚀 Hướng Dẫn Sử Dụng Chi Tiết

### 1. Yêu cầu thiết bị
- Thiết bị Android đã Root bằng **Magisk (v24+)**, **KernelSU**, hoặc **APatch**.
- Đã bật Zygisk (trên Magisk) hoặc cài module **ZygiskNext** (trên KernelSU / APatch).

### 2. Cài đặt
1. Tải file `zygisk-il2cppdumper-v3.0.0-release.zip` từ mục [Releases](https://github.com/miyu-chan520/Bo-il2cpp-dumper/releases).
2. Mở Magisk / KernelSU / APatch, chọn **Cài đặt từ bộ nhớ** (Install from storage), chọn file zip vừa tải và flash.
3. Khởi động lại thiết bị (Reboot).

### 3. Thực hiện Dump
1. Mở game bạn muốn dump.
2. Đợi game tải qua màn hình Splash Screen (dumper sẽ tự động nhận diện `libil2cpp.so` và gắn thread vào IL2CPP runtime).
3. Màn hình sẽ hiện Toast:
   ```text
   [Il2CppDumper] Dumping in progress, please wait...
   ```
4. Khi hoàn thành, màn hình xuất hiện Toast:
   ```text
   [Il2CppDumper] SUCCESS! All dumps saved to /sdcard/Bo_dump/
   ```
5. Truy cập `/sdcard/Bo_dump/` để lấy toàn bộ dữ liệu dump.

---

## 🔍 Hướng Dẫn Tra Cứu Hàm Trong `offset.txt`

Mở file `/sdcard/Bo_dump/offset.txt` bằng bất kỳ trình soạn thảo văn bản nào và nhấn Tìm kiếm (`Ctrl + F`):
- Để tìm hàm của một Class: gõ `ClassName$$` (ví dụ: `Player$$TakeDamage`, `GameFacade$$`)
- Để tìm thuộc tính/field của một Class: gõ `ClassName::` (ví dụ: `Player::m_IsFiring`, `MatchGame::`)
- Toàn bộ offset hiển thị dưới dạng RVA (relative to base) và VA (địa chỉ ảo runtime hiện tại).
