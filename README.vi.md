# Zygisk-Il2CppDumper

Đây là dự án thử nghiệm cho Android/Zygisk nhằm kiểm tra metadata IL2CPP trong lúc ứng dụng chạy. Khả năng tương thích phụ thuộc vào phiên bản Unity/IL2CPP và môi trường Android; không bảo đảm hoạt động với mọi ứng dụng.

**Chỉ sử dụng khi được phép:** chỉ kiểm thử ứng dụng do bạn sở hữu hoặc có sự cho phép rõ ràng. Không dùng dự án để né anti-cheat, vượt kiểm soát truy cập hoặc trích xuất dữ liệu từ ứng dụng bên thứ ba khi chưa được phép.

Tài liệu tiếng Anh: [README.md](README.md).  
中文文档：[README.zh-CN.md](README.zh-CN.md)。

## Cấu trúc repo

- `module/src/main/cpp/`: mã nguồn native và phần liên quan đến IL2CPP.
- `module/build.gradle`, `build.gradle`, `gradle/`: cấu hình Android/Gradle.
- `.github/workflows/`: quy trình kiểm tra tự động.
- `scripts/configure_target.py`: xác thực package ID trước khi cập nhật cấu hình build.
- `tests/`: kiểm thử đơn vị cho việc xác thực cấu hình. Những bài kiểm thử này **không** chứng minh module native chạy được trên thiết bị hoặc tương thích với một ứng dụng cụ thể.

## Môi trường build

Sử dụng Android Studio với Android SDK/NDK phù hợp với cấu hình dự án. Các phiên bản đang được ghim là Android Gradle Plugin 7.4.2, Gradle 7.5 và NDK 25.2.9519653. Khi nâng phiên bản, hãy kiểm thử chúng theo bộ thay vì thay đổi riêng lẻ một thành phần.

Để build phục vụ kiểm thử được cấp phép, đặt `GamePackageName` trong `module/src/main/cpp/game.h` thành package của ứng dụng bạn sở hữu hoặc được phép kiểm thử, sau đó chạy Gradle task `:module:assembleRelease`. File ZIP của module được tạo trong thư mục `out/`.

Workflow GitHub Actions sẽ xác thực package ID trước khi ghi cấu hình. Build thành công không đồng nghĩa với việc bạn có quyền kiểm tra ứng dụng đã chọn.

## Kiểm thử tự động

Chạy tại thư mục gốc của repo:

```sh
python -m unittest discover -s tests -v
python -m compileall -q scripts tests
```

Các lệnh này chỉ kiểm tra quy tắc package name và hành vi cấu hình. Việc biên dịch native, thử trên thiết bị và xác nhận tương thích cần được đánh giá riêng trong môi trường kiểm thử được cấp phép.

## Báo lỗi

Hãy cung cấp phiên bản Android, ABI thiết bị, môi trường build, lỗi đầy đủ và các bước tái hiện tối thiểu. Trước khi gửi log hoặc tệp đính kèm, hãy xóa dữ liệu cá nhân, tệp ứng dụng độc quyền, bản dump metadata, khóa ký và thông tin bí mật khác. Xem [SECURITY.md](SECURITY.md) khi báo cáo vấn đề bảo mật.
