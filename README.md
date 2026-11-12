# Đèn LED WS2812 thông minh dùng ESP32-C3

Dự án cung cấp firmware điều khiển 8 LED RGB WS2812 trên ESP32-C3. Bạn có thể đổi hiệu ứng bằng nút nhấn, điều khiển đèn qua trình duyệt trong cùng mạng Wi-Fi hoặc dùng ứng dụng ESP RainMaker để điều khiển từ xa. Firmware được phát triển từ ví dụ đèn thông minh trong tài liệu IoT ESP32-C3 của Espressif.

## Tính năng

- Bật/tắt đèn, chỉnh độ sáng, độ bão hòa, sắc độ đầu/cuối và tốc độ hiệu ứng.
- Chọn một trong bốn chế độ `Static`, `Wave`, `Wave 2` và `Rainbow`.
- Đổi chế độ bằng nút nhấn ngay cả khi thiết bị chưa kết nối Wi-Fi. Các lần nhấn chuyển lần lượt qua bốn chế độ, trạng thái tắt và quay về `Static`.
- Cấu hình Wi-Fi qua Bluetooth LE với ESP RainMaker. Firmware cũng tích hợp cập nhật OTA và gửi số liệu chẩn đoán qua ESP Insights.

## Phần cứng

| Linh kiện | Kết nối theo firmware |
| --- | --- |
| Bo ESP32-C3 | Flash 4 MB theo cấu hình dự án |
| Dải hoặc mô-đun 8 LED WS2812 | DIN nối GPIO0, GND nối chung với ESP32-C3 |
| Nút đổi hiệu ứng | GPIO3 nối GND khi nhấn |
| Nút khôi phục cài đặt gốc | GPIO10 nối GND khi nhấn |

Cấp nguồn cho mô-đun LED theo thông số của mô-đun. Số lượng LED và các chân GPIO được khai báo trong [app_driver.c](Intergrated_project/main/app_driver.c).

## Sử dụng

1. Biên dịch và nạp firmware từ thư mục `Intergrated_project/` theo [hướng dẫn biên dịch](#biên-dịch-và-nạp-trên-windows).
2. Khi nạp lần đầu, mở ESP RainMaker trên điện thoại, bật Bluetooth và thêm thiết bị qua BLE. Tên thiết bị và mã xác thực PoP xuất hiện trong log serial ở dòng `Provisioning Started`. Bản firmware này in thông tin ghép nối ra log và đã tắt lệnh in mã QR cho BLE.
3. Chọn mạng Wi-Fi 2,4 GHz và hoàn tất ghép nối. Trong ESP RainMaker, mở thiết bị `Light` để điều khiển đèn.
4. Trên thiết bị cùng mạng Wi-Fi, mở `http://ESP-Night-Fury.local/`. Nếu mạng hoặc thiết bị khách không phân giải mDNS, dùng địa chỉ IP của ESP32-C3 trên cổng 80.
5. Để xóa cấu hình và ghép nối lại, nhấn nút GPIO10. LED nháy đỏ trong quá trình khôi phục và bo tự khởi động lại.

## Mã nguồn

| Đường dẫn | Nội dung |
| --- | --- |
| [Intergrated_project/main/](Intergrated_project/main/) | Firmware chính, điều khiển LED, hiệu ứng, web nội bộ, mDNS và quản lý năng lượng |
| [components/](components/) | Các thành phần nút nhấn, Wi-Fi, lưu trữ và driver LED dùng chung |
| [Intergrated_project/partitions.csv](Intergrated_project/partitions.csv) | Bảng phân vùng flash cho NVS, chẩn đoán và hai vùng ứng dụng OTA |
| [Intergrated_project/dependencies.lock](Intergrated_project/dependencies.lock) | Phiên bản các phụ thuộc được khóa cho ESP32-C3 |

## Môi trường biên dịch

- Dùng ESP-IDF **5.5.2**, phiên bản được ghi trong [dependencies.lock](Intergrated_project/dependencies.lock), và mở terminal đã kích hoạt môi trường ESP-IDF.
- Chuẩn bị mã nguồn ESP RainMaker cùng các Git submodule. [CMakeLists.txt](Intergrated_project/CMakeLists.txt) lấy các thành phần RainMaker và ESP Insights từ cây mã nguồn này. Dự án không khóa commit của kho ESP RainMaker bên ngoài. Bản checkout cần có các đường dẫn trong `EXTRA_COMPONENT_DIRS`.
- Dùng cấu hình ESP32-C3 và flash 4 MB đã có trong dự án. Khi dùng bản sao chưa có `sdkconfig`, ESP-IDF lấy cấu hình ban đầu từ `sdkconfig.defaults`.

## Biên dịch và nạp trên Windows

Dùng PowerShell đã kích hoạt ESP-IDF. Đầu tiên, mở thư mục gốc của bản checkout ESP RainMaker và chạy:

~~~powershell
# Chạy trong thư mục gốc của bản checkout ESP RainMaker
git submodule update --init --recursive
$env:RAIMAKER_PATH = (Get-Location).Path

~~~

Trong cùng cửa sổ PowerShell, chuyển đến thư mục gốc của repo dự án. Chạy các lệnh sau và thay COM5 bằng cổng của bo:

~~~powershell
Set-Location ./Intergrated_project
idf.py --version
idf.py build
idf.py -p COM5 flash monitor
~~~

Tên biến môi trường trong CMake là `RAIMAKER_PATH`. Nếu CMake báo thiếu thành phần, kiểm tra đường dẫn này, các submodule và các thư mục được liệt kê trong `EXTRA_COMPONENT_DIRS`. Thoát serial monitor bằng `Ctrl+]`.

## Các tệp chính

| Tệp | Vai trò |
| --- | --- |
| [app_main.c](Intergrated_project/main/app_main.c) | Khởi tạo firmware, đăng ký thiết bị `Light` với RainMaker, cập nhật OTA và gửi số liệu ESP Insights |
| [app_driver.c](Intergrated_project/main/app_driver.c) | Điều khiển WS2812 qua RMT, đọc nút đổi chế độ và xử lý nút khôi phục |
| [app_effects.c](Intergrated_project/main/app_effects.c) | Giữ trạng thái đèn và tạo hiệu ứng theo màu, độ sáng và tốc độ |
| [localcontrol.c](Intergrated_project/main/localcontrol.c) | Phục vụ giao diện web và API HTTP nội bộ |
| [mdns.c](Intergrated_project/main/mdns.c) | Công bố tên máy `ESP-Night-Fury.local` sau khi nhận IP |
| [app_pm.c](Intergrated_project/main/app_pm.c) | Cấu hình thay đổi tần số CPU và khóa tần số bus APB khi tính năng quản lý năng lượng được bật |
| [app_wifi.c](components/app_wifi/app_wifi.c) | Ghép nối BLE để cấp thông tin Wi-Fi và kết nối lại bằng cấu hình đã lưu |

## Điều khiển qua HTTP

Máy chủ HTTP trên ESP32-C3 phục vụ ba đường dẫn sau trong mạng nội bộ.

| Phương thức và đường dẫn | Mục đích |
| --- | --- |
| `GET /` | Mở giao diện điều khiển |
| `GET /state` | Đọc trạng thái đèn ở dạng JSON |
| `POST /set` | Gửi tham số điều khiển ở dạng JSON |

## Kiểm tra sau khi nạp

1. Quan sát log khởi động. Nếu chưa cấu hình Wi-Fi, tìm dòng `Provisioning Started` để lấy tên BLE và mã PoP.
2. Nhấn nút GPIO3 để xác nhận LED đổi hiệu ứng. Tiếp tục nhấn qua `Rainbow` để kiểm tra trạng thái tắt và quay lại `Static`.
3. Sau khi kết nối Wi-Fi, thử bật/tắt đèn và đổi độ sáng từ giao diện web và ứng dụng ESP RainMaker.
4. Khi cần kiểm tra quy trình ghép nối lại, nhấn GPIO10, quan sát LED nháy đỏ và xác nhận thiết bị khởi động lại vào chế độ cấu hình Wi-Fi.
