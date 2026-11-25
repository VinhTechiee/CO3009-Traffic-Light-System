# CO3009 Two-Road Traffic Light System

Firmware for the XNUCLEO-F103RB carrier and STM32F103RB MCU. See [pre-board validation](docs/pre-board-validation.md) for software results and [the board checklist](docs/board-demo-checklist.md) for calibration and demonstration.

## Build and test

Install STM32CubeF1 V1.8.7 and put STM32CubeCLT's ARM GNU tools, CMake, and Ninja on PATH. CMake uses the SDK under `%USERPROFILE%/STM32Cube/Repository/STM32Cube_FW_F1_V1.8.7` by default. Set `-DSTM32CubeF1_ROOT=<absolute SDK path>` for another installation.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-c-host-tests.ps1
cmake -S . -B build/firmware -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/firmware --clean-first
```

For the sanitizer run, use the host runner with `-WslSanitizers` and GCC with ASan/UBSan support in the default WSL distribution. CMake builds from the checked-in source, the vendor CMSIS startup and HAL, and the repository linker script. The application outputs are `build/firmware/co3009.elf`, `.hex`, `.bin`, and `.map`. The root-level HEX is an upstream historical upload.

The default image leaves the traffic GPIO pins as inputs, ignores button input, and leaves the LCD disabled while Automatic logic and DHT20 acquisition run. Calibrate the board and pass your measured header using `-DCO3009_PROFILE_HEADER=<absolute header path>` to enable the verified hardware. Build the separate diagnostic image and flash the measured-profile application over SWD using [the board checklist](docs/board-demo-checklist.md). The `.ioc` is the pin and peripheral configuration reference. Use CMake for the integrated firmware build.

## Operation

| Mode | Button actions |
| --- | --- |
| Automatic | BTN1 enters Manual and passes the active lamp pair. |
| Manual | BTN2 press advances one phase. BTN1 enters Tuning. |
| Tuning | BTN2 tap selects Red → Green → Yellow → Red. BTN3 increases and BTN4 decreases the selected duration by 1 s. Hold either to repeat. Hold BTN2 to save, or tap BTN1 to cancel. Both actions restart Automatic. |

Automatic starts at Road 1 Red and Road 2 Green. Defaults are Red 5 s, Green 3 s, and Yellow 2 s. Tuning accepts Red 2–40 s, Green 1–30 s, and Yellow 1–10 s. Red must cover Green plus Yellow. Any excess gives one timed all-red phase per complete cycle. Tuning stays all-red and saves in RAM until reset. Both roads use the same durations.

The LCD dedicates one row to mode. A two-row display shares the remaining row between the selected duration in Tuning and sensor data in other modes. Three or more rows allow separate timing and sensor rows. See [architecture](docs/architecture.md) for the timing, tasks, and recovery behavior. The detailed course handout and shield schematic were unavailable for this integration, so the button actions and limits still need comparison with the official specification.

## Phân công BTL (3 thành viên)

**Đề tài:** Two-Road Traffic Light System - XNUCLEO-F103RB  
**Deadline nộp báo cáo & source:** **04/12/2026** · **Demo:** tuần 15/16 (theo lịch thông báo)  
**GitHub:** [CO3009-Traffic-Light-System](https://github.com/VinhTechiee/CO3009-Traffic-Light-System) · Mỗi người dùng tài khoản riêng, commit phần mình.

## 1. Phân công chính

| Người | Phụ trách | File/module | Kết quả bàn giao |
|---|---|---|---|
| **A - Core & Auto** | O(1) Scheduler, software timer, FSM Automatic | `scheduler.*`, `software_timer.*`, `fsm_automatic.*` | Scheduler có `Add/Delete/Update/Dispatch`, hỗ trợ one-shot; test đúng chu kỳ; 2 đường tự động chuyển đèn an toàn |
| **B - Hardware & Manual** | Cấu hình STM32CubeIDE, GPIO/TIM2/I2C1, button, traffic light, FSM Manual | `.ioc`, `button.*`, `traffic_light.*`, `fsm_manual.*` | Build/flash được; nút chống dội; điều khiển đúng màu đèn; Manual hoạt động |
| **C - LCD & Tuning** | LCD, DHT20, FSM chỉnh thời gian, giao diện chuyển mode | `lcd.*`, `dht20.*`, `fsm_tuning.*`, `fsm_mode.*` *(đề xuất)* | Hiển thị nhiệt độ/độ ẩm; chỉnh thời gian bằng nút; LCD báo mode/giá trị |

**Cả 3 cùng làm:** thống nhất API, `global.h/.c`, sơ đồ FSM và quy tắc chuyển mode; tích hợp, kiểm thử, README, báo cáo và demo. Mỗi người tự viết phần báo cáo tương ứng.

## 2. Cách làm & mốc nội bộ

| Hạn | Việc cần xong |
|---|---|
| **13/11** | Tạo repo, thống nhất nhánh/tên hàm, interface giữa các module; B tạo project `.ioc` đúng **NUCLEO-F103RB + Serial Wire** |
| **20/11** | Mỗi người hoàn thành code phần mình và test được tối đa **không cần board**; chốt sơ đồ FSM + kịch bản demo |
| **21–25/11** | **Một buổi chung 3 người với board của thầy** (xếp bằng When2meet, thầy xác nhận) |
| **29/11** | Fix lỗi từ buổi test, hoàn thiện tích hợp, rà soát an toàn và edge cases |
| **03/12** | Chốt báo cáo PDF, README, commit/tag nộp bài; **04/12** nộp LMS |

## 3. Buổi làm board chung - bắt buộc cả 3 người

**Board do thầy giữ:** không giả định ai có thể mang board về. Trước buổi gặp, cần hỏi thầy **ngày/giờ, phòng, thời lượng và quyền sử dụng board**.

- **When2meet:** [https://www.when2meet.com/?39817490-cQzfv]. Đề xuất mở các khung ngày **21–25/11**; cả 3 điền giờ trống, rồi **xác nhận lại với thầy**. Dự kiến đặt **3–4 giờ**.
- **Trước buổi gặp:** A chuẩn bị firmware test scheduler + Automatic; B chuẩn bị `.ioc`/pin checklist + test GPIO/button/light; C chuẩn bị I2C scan + test LCD/DHT20/Tuning. Mỗi người mang laptop đã cài STM32CubeIDE, code đã pull/merge thử và checklist riêng.
- **Trong buổi gặp (thứ tự ưu tiên):** (1) flash nhiều lần, LED blink; (2) mapping D3–D10 và bảng 4 tổ hợp đèn, 4 nút; (3) I2C scan ra LCD + DHT20; (4) test Scheduler; (5) demo Automatic → Manual → Tuning; (6) ghi video/ảnh, log lỗi và phân người fix.
- **Sau buổi gặp:** đẩy code, lưu kết quả đo/pin mapping vào repo; thống nhất các lỗi phải sửa trước **29/11**.

## 4. Tiêu chí tích hợp (Definition of Done)

- `SCH_Update()` **O(1)**, one-shot chạy đúng 1 lần, task xóa/thêm không làm sai thời gian.
- **Không bao giờ cả hai đường cùng xanh**; chuyển mode có trạng thái đèn/timer/LCD xác định.
- FSM không gọi `HAL_GPIO_*` hoặc `HAL_I2C_*`; các module có `.h/.c` và API rõ ràng.
- Button không bị double-trigger; LCD hiển thị đúng; DHT20 được đọc định kỳ.
- Repo có README hướng dẫn build/flash/operate, đủ commit của 3 thành viên, tag cuối và báo cáo PDF theo template của môn.

> **Cần đối chiếu:** tài liệu hướng dẫn còn dẫn tới `Project_Group.docx` (đặc tả chi tiết). Khi có file này, chốt lại mapping nút, ràng buộc thời gian và hành vi từng mode trước khi code cuối.
