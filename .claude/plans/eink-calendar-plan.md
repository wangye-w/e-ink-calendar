# ESP32-S3 墨水屏日历 — 分阶段实现计划

## 项目现状

| 模块 | 状态 | 说明 |
|------|------|------|
| ESP-IDF v5.5.4 工程 | 已完成 | CMakeLists.txt, sdkconfig 就绪，目标芯片 ESP32-S3 |
| WiFi Station | 已完成 | `main/app_wifi.c`，事件驱动连接 |
| NTP 时间同步 | 已完成 | `main/app_sntp.c`，阿里云 NTP，CST-8 时区 |
| NVS Flash | 已完成 | `main/e-ink-calendar.c` 中初始化 |
| PSRAM | 未启用 | 帧缓冲区必须控制在片内 SRAM 可用范围内 |
| 墨水屏驱动 | 空 | `components/eink_driver/` |
| 日历逻辑 | 空 | `components/calendar_logic/` |
| UI 渲染 | 空 | `components/ui_render/` |

---

## 阶段 0：硬件确认（必须在任何编码前完成）

### 需要确认的信息

1. **屏幕型号/驱动IC**：常见 SSD1680、SSD1681、SSD1675、UC8151、IL0373 等。屏幕排线上通常有丝印
2. **分辨率**：如 296x128 (2.9寸)、400x300 (4.2寸)、800x480 (7.5寸)
3. **颜色**：单色黑白 / 三色黑白红 / 黑白黄？
4. **SPI 引脚连接**：
   - MOSI, SCLK, CS, DC, RST, BUSY 分别接哪个 GPIO
   - 是否使用 ESP32-S3 默认 FSPI 引脚（MOSI=GPIO11, SCLK=GPIO12, CS=GPIO10）
5. **供电**：3.3V？是否有独立电源控制引脚？
6. **其他外设**：是否有按键、LED、温湿度传感器？

### 风险

- **风险**：在无硬件反馈的情况下编写驱动，寄存器时序可能与实际屏幕不匹配
  - **缓解**：拿到屏幕数据手册，参照已有开源驱动（Waveshare 例程、GxEPD2 库）适配
- **风险**：不同批次屏幕可能使用不同驱动IC
  - **缓解**：拆开柔性PCB检查丝印，从数据手册确认

---

## 阶段 1：安全修复 —— WiFi 凭据管理（最先做）

### 问题

`main/app_wifi.c` 第 7-8 行硬编码了 WiFi SSID 和密码，且第 87-91 行日志明文打印凭据。严重安全隐患。

### 1.1 创建配置存储组件

**新建 `components/app_config/`**：

```
components/app_config/
├── CMakeLists.txt
├── include/
│   └── app_config.h
└── app_config.c
```

API 设计：

```c
esp_err_t app_config_init(void);
esp_err_t app_config_read_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t app_config_write_wifi(const char *ssid, const char *pass);
esp_err_t app_config_read_str(const char *key, char *out, size_t len);
esp_err_t app_config_write_str(const char *key, const char *val);
```

使用 NVS 命名空间 `"app_config"`，键名 `"wifi_ssid"`, `"wifi_pass"`, `"timezone"` 等。

### 1.2 修改 WiFi 模块

- 移除 `#define WIFI_SSID` 和 `#define WIFI_PASS`
- `wifi_init_sta()` 改为接受参数：`EventBits_t wifi_init_sta(const char *ssid, const char *password);`
- 移除日志中明文打印密码的行
- 新增参数校验：ssid 为空则立即返回错误

### 1.3 修改主入口

`main/e-ink-calendar.c`：
- NVS 初始化后调用 `app_config_init()`
- 通过 `app_config_read_wifi()` 获取凭据
- 凭据为空（首次启动）→ 打印提示，进入串口配置模式
- 凭据传给 `wifi_init_sta(ssid, pass)`

### 任务清单

- [ ] 创建 `components/app_config/` 组件（CMakeLists.txt + 头文件 + 实现）
- [ ] 修改 `app_wifi.c/h`：参数化 SSID/密码，删除明文日志
- [ ] 修改 `e-ink-calendar.c`：从 NVS 读取凭据
- [ ] 更新 `main/CMakeLists.txt` 添加 `app_config` 依赖
- [ ] 验证编译通过

---

## 阶段 2：墨水屏驱动 (`components/eink_driver`)

最关键的底层组件。封装 SPI 通信，提供帧缓冲写入、全刷/局刷、休眠/唤醒。

### 2.1 文件结构

```
components/eink_driver/
├── CMakeLists.txt
├── include/
│   └── eink_driver.h          # 公共 API
├── eink_driver.c              # SPI 通信 + 初始化 + 刷新逻辑
├── eink_hw_config.h           # 引脚和屏幕参数（用户按实际接线修改）
├── eink_cmd.h                 # 驱动IC命令码定义
└── ssd1680.c                  # SSD1680/SSD1681 初始化序列（按实际IC替换）
```

### 2.2 公共 API

```c
typedef struct {
    int sck_io;        // SPI 时钟
    int mosi_io;       // SPI 数据
    int cs_io;         // 片选
    int dc_io;         // 数据/命令选择
    int rst_io;        // 复位（-1 表示不用）
    int busy_io;       // 忙碌指示（输入）
    int width;         // 水平像素
    int height;        // 垂直像素
    int power_en_io;   // 屏幕电源控制（-1 表示不用）
} eink_config_t;

esp_err_t eink_init(const eink_config_t *config);
esp_err_t eink_full_refresh(const uint8_t *framebuffer, size_t len);
esp_err_t eink_partial_refresh(const uint8_t *framebuffer, size_t len,
                               int x, int y, int w, int h);
esp_err_t eink_clear(bool white);
esp_err_t eink_sleep(void);
esp_err_t eink_wake(void);
```

### 2.3 实现要点

- **SPI 初始化**：`spi_bus_initialize()` 使用 SPI2_HOST (FSPI)，模式 0 (CPOL=0, CPHA=0)，时钟约 20MHz，启用 DMA
- **DC 引脚**：GPIO 直接控制，低电平=命令，高电平=数据
- **BUSY 引脚**：GPIO 输入，发送刷新命令后轮询等待（典型 3-15 秒超时）
- **复位时序**：RST 拉低 10ms → 拉高 → 等 10ms → 等 BUSY 释放
- **初始化序列**：按数据手册实现（软件复位 → 驱动输出控制 → 数据入口模式 → 显示RAM窗口 → 边框波形等）
- **帧缓冲区格式**：1bpp 单色，每字节 8 像素（MSB 或 LSB 优先取决于控制器）

### 2.4 组件 CMakeLists

```cmake
idf_component_register(
    SRCS "eink_driver.c" "ssd1680.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES spi_flash driver
)
```

### 风险

- **风险**：SPI 时序问题导致花屏或无反应
  - **缓解**：用逻辑分析仪抓取 SPI 波形对比数据手册，逐步调整时钟频率
- **风险**：大屏幕帧缓冲超出 SRAM（7.5寸 800x480 = 48KB 帧缓冲）
  - **缓解**：实现分块渲染策略，或开启 PSRAM

---

## 阶段 3：日历逻辑 (`components/calendar_logic`)

纯计算组件，无硬件依赖，可在 PC 上独立编译和单元测试。

### 3.1 文件结构

```
components/calendar_logic/
├── CMakeLists.txt
├── include/
│   └── lunar_calendar.h        # 公共 API
├── calendar_types.h            # 数据结构定义
├── lunar_calendar.c            # 公历↔农历转换核心
├── lunar_data_tables.c         # 农历数据表（1900-2100）
├── solar_terms.c               # 二十四节气计算
└── holidays.c                  # 节假日判定
```

### 3.2 核心数据结构

```c
typedef struct {
    int year, month, day;
    int week_day;          // 0=周日
} solar_date_t;

typedef struct {
    int year, month, day;
    bool is_leap_month;
    char month_str[8];    // "正月" "腊月"
    char day_str[8];      // "初一" "十五"
    const char *year_name; // 天干地支 "甲辰"
    const char *zodiac;    // 生肖 "龙"
} lunar_date_t;

typedef struct {
    const char *name;     // "立春"
    int solar_month, solar_day;
} solar_term_t;

typedef struct {
    const char *name;     // "春节"
    int type;             // 公历节日/农历节日/节气/周末
} holiday_info_t;

typedef struct {
    solar_date_t solar;
    lunar_date_t lunar;
    solar_term_t *solar_term;   // NULL=无
    holiday_info_t *holiday;    // NULL=无
    bool is_today;
    bool is_current_month;
} calendar_day_t;

typedef struct {
    int year, month;
    int num_weeks;              // 5 或 6
    calendar_day_t days[42];    // 6周 x 7天
} calendar_month_t;
```

### 3.3 公共 API

```c
void lunar_calendar_init(void);
void solar_to_lunar(const solar_date_t *solar, lunar_date_t *lunar);
void lunar_to_solar(const lunar_date_t *lunar, solar_date_t *solar);
void get_calendar_month(int year, int month, calendar_month_t *out);
const solar_term_t *get_solar_term(const solar_date_t *date);
const holiday_info_t *get_holiday(const solar_date_t *date, const lunar_date_t *lunar);
int get_month_solar_terms(int year, int month, solar_term_t terms[], int max_terms);
```

### 3.4 农历算法

**查表法**（推荐嵌入式场景）：

- 农历数据表覆盖 1900-2100 年，每条 4 字节编码全年信息：
  - bit[0:3]：闰月月份（0=无闰月）
  - bit[4:15]：12/13 个月的大小月（1=30天大月，0=29天小月）
  - bit[16:19]：闰月大小
  - bit[20:31]：春节公历日期偏移
- 200 年 × 4 字节 = 800 字节，极小内存占用
- 二分查找年份，O(log n)

**节气计算**（简化公式法）：

```
D = 0.2422 * (year - 1900) + base_offset[term_index] - floor((year - 1900) / 4)
```

24 个节气各有基准偏移值，精度约 ±1 天，对日历显示足够。

### 3.5 节假日

- 公历固定节日：顺序检索数组（国庆、元旦、劳动节等 20-30 个）
- 农历节日：正月初一=春节、五月初五=端午、八月十五=中秋等
- 特殊日期：母亲节（5月第2个周日）等需计算逻辑

### 风险

- **风险**：农历转换在边界年份（1900年前、2100年后）失效
  - **缓解**：加年份范围检查，超出返回错误
- **风险**：节气简化公式某些年份偏差超过一天
  - **缓解**：对比权威天文年历做 200 年验证，必要时插值修正

---

## 阶段 4：UI 渲染 (`components/ui_render`)

将 `calendar_month_t` 渲染为墨水屏帧缓冲区。负责布局、文字（含中文点阵字体）、图形绘制。

### 4.1 文件结构

```
components/ui_render/
├── CMakeLists.txt
├── include/
│   └── ui_render.h
├── ui_render.c              # 主渲染逻辑
├── draw_utils.c/h           # 底层绘图（像素/线/矩形/文字）
├── font_data.c/h            # 点阵字库数据
└── layouts/
    ├── month_view.c/h       # 月视图布局
```

### 4.2 公共 API

```c
esp_err_t ui_render_init(int width, int height);
uint8_t *ui_create_framebuffer(void);
size_t ui_get_framebuffer_size(void);
void ui_clear_framebuffer(uint8_t *fb);
void ui_render_calendar_month(const calendar_month_t *month, uint8_t *fb);
void ui_render_status_bar(const char *time_str, const char *date_str,
                           bool wifi_ok, uint8_t *fb);
```

### 4.3 月视图布局

```
┌──────────────────────────────────────────┐
│      2026 年 5 月              WiFi ✓ 10:30 │  ← 状态栏 (~16px)
├─────┬─────┬─────┬─────┬─────┬─────┬─────┤
│  一  │  二  │  三  │  四  │  五  │  六  │  日  │  ← 星期头 (~24px)
├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ 27  │ 28  │ 29  │ 30  │  1  │  2  │  3  │  ← 公历日期(大字)
│十二 │十三 │十四 │初一 │初二 │初三 │初四 │  ← 农历日期(小字)
├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ ...                         (共 5-6 行)    │
├──────────────────────────────────────────┤
│ 本月节气: 立夏 5/5  小满 5/21              │  ← 节气栏 (~20px)
└──────────────────────────────────────────┘
```

今日日期：反色显示（黑底白字）或加粗边框
非本月日期：显示更淡（虚线或细体）
节日/节气日：特殊标记

### 4.4 中文字体

取模约 200-300 个所需汉字为 16x16 点阵字库：

- 数字 0-9、年月日时分秒星期一至日
- 农历月份：正二三四五六七八九十冬腊
- 农历日期：初一至三十
- 二十四节气名（约 30 个不重复字符）
- 天干地支（22 字）、生肖（12 字）
- 常用节日名：春节元宵清明端午中秋国庆等（~30 字）
- 辅助文字：今天本月刷新同步WiFi等（~10 字）

字体格式：

```c
typedef struct {
    const char *utf8_str;       // UTF-8 字符串（通常 3 字节）
    const uint8_t bitmap[32];   // 16x16 点阵，行优先
} cjk_glyph_t;

const cjk_glyph_t *cjk_font_lookup(const char *utf8_str);
```

**生成方式**：用 Python + PIL 从宋体/黑体提取点阵，输出 C 数组嵌入 `font_data.c`。

### 4.5 绘图工具函数

```c
void draw_pixel(uint8_t *fb, int x, int y, bool black);
void draw_hline(uint8_t *fb, int x, int y, int len, bool black);
void draw_vline(uint8_t *fb, int x, int y, int len, bool black);
void draw_rect(uint8_t *fb, int x, int y, int w, int h, bool black);
void fill_rect(uint8_t *fb, int x, int y, int w, int h, bool black);
void draw_char(uint8_t *fb, int x, int y, char ch, const font_t *font, bool black);
void draw_string(uint8_t *fb, int x, int y, const char *str, const font_t *font, bool black);
void draw_cjk_string(uint8_t *fb, int x, int y, const char *utf8_str,
                     const cjk_font_t *font, bool black);
```

### 风险

- **风险**：缺少所需汉字导致显示空白或乱码
  - **缓解**：维护字符清单 `charset_required.txt`，脚本验证所有字符已生成
- **风险**：大屏幕帧缓冲超出 SRAM
  - **缓解**：实现分块渲染，或建议开启 PSRAM

---

## 阶段 5：主应用集成与电源管理

### 5.1 主程序流程

`main/e-ink-calendar.c` 完整流程：

```
app_main()
  ├── nvs_flash_init()
  ├── app_config_init()
  ├── app_config_read_wifi() → 获取凭据
  │     └── 首次启动 → 串口配置模式
  ├── wifi_init_sta(ssid, pass) → 连 WiFi（超时 15s）
  │     └── 失败 → 使用 RTC 时间继续
  ├── app_sntp_init() → NTP 对时
  │     └── 失败 + RTC 有效 → 用 RTC
  │     └── 失败 + RTC 无效 → 显示错误，sleep
  ├── 对比 NVS 中上次刷新日期
  │     ├── 日期相同 → 不刷新，直接 sleep
  │     └── 日期不同 → 继续
  ├── get_calendar_month() → 计算月视图
  ├── ui_create_framebuffer()
  ├── ui_render_calendar_month()
  ├── ui_render_status_bar()
  ├── eink_init() → eink_wake() → eink_full_refresh() → eink_sleep()
  ├── 保存本次更新日期到 NVS
  └── esp_deep_sleep_start() → 深度睡眠
```

### 5.2 电源管理策略

```c
// 计算下次唤醒时间
uint64_t calculate_next_wakeup(void);
void enter_deep_sleep(uint64_t sleep_us);
```

**唤醒策略**：
- 白天 (06:00-23:00)：每小时整点刷新一次
- 午夜 (00:00)：刷新一次更新日期
- 夜间 (23:00-06:00)：不刷新，节约电量
- 按键唤醒（可选）：GPIO 外部中断触发强制刷新

**预期功耗**：
- 唤醒态 ~80mA × 10 秒 × 每小时一次
- 深度睡眠 ~10μA
- 2000mAh 电池：约 2-3 个月续航

### 5.3 新建文件

- `main/power_mgr.c` / `main/power_mgr.h`：电源管理逻辑

### 5.4 更新 main/CMakeLists.txt

```cmake
idf_component_register(
    SRCS "e-ink-calendar.c" "app_sntp.c" "app_wifi.c" "power_mgr.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi nvs_flash app_config
    REQUIRES eink_driver calendar_logic ui_render
)
```

---

## 阶段 6：测试策略

### 6.1 PC 端单元测试（日历逻辑）

`components/calendar_logic/` 是纯计算，可在 PC 上用 CMake + CTest 测试：

- `test_solar_to_lunar_known_dates`：50+ 对照验证公历→农历精度
- `test_lunar_to_solar_round_trip`：公历→农历→公历往返一致性
- `test_leap_month_handling`：闰月年份正确处理（2023年闰二月等）
- `test_solar_term_accuracy`：2020-2050 年节气数据对比
- `test_holiday_detection`：各类节日判定逻辑
- `test_calendar_month_boundary`：月初/月末/跨年边界

### 6.2 ESP32 集成测试

- SPI 通信测试：发送命令到屏幕，读取 BUSY 状态
- 棋盘格图案验证显示正确性
- 全流程：NVS → WiFi → NTP → 日历计算 → 渲染 → 屏幕刷新

### 6.3 功耗测试

- 万用表测量唤醒态电流和深度睡眠电流
- 计算实际电池续航

---

## 阶段 7：可选增强功能

- **SmartConfig 配网**：使用 ESP-Touch 手机 App 配网，避免串口输入
- **OTA 固件更新**：HTTP 下载新固件，墨水屏显示进度
- **天气显示**：HTTP 请求天气 API，在日历角落显示天气图标
- **多页切换**：按键翻页查看不同月份

---

## 文件变更汇总

| 操作 | 文件路径 | 所属阶段 |
|------|---------|---------|
| 新建 | `components/app_config/CMakeLists.txt` | 阶段1 |
| 新建 | `components/app_config/include/app_config.h` | 阶段1 |
| 新建 | `components/app_config/app_config.c` | 阶段1 |
| 新建 | `components/eink_driver/CMakeLists.txt` | 阶段2 |
| 新建 | `components/eink_driver/include/eink_driver.h` | 阶段2 |
| 新建 | `components/eink_driver/eink_driver.c` | 阶段2 |
| 新建 | `components/eink_driver/eink_hw_config.h` | 阶段2 |
| 新建 | `components/eink_driver/eink_cmd.h` | 阶段2 |
| 新建 | `components/eink_driver/ssd1680.c` | 阶段2 |
| 新建 | `components/calendar_logic/CMakeLists.txt` | 阶段3 |
| 新建 | `components/calendar_logic/include/lunar_calendar.h` | 阶段3 |
| 新建 | `components/calendar_logic/calendar_types.h` | 阶段3 |
| 新建 | `components/calendar_logic/lunar_calendar.c` | 阶段3 |
| 新建 | `components/calendar_logic/lunar_data_tables.c` | 阶段3 |
| 新建 | `components/calendar_logic/solar_terms.c` | 阶段3 |
| 新建 | `components/calendar_logic/holidays.c` | 阶段3 |
| 新建 | `components/ui_render/CMakeLists.txt` | 阶段4 |
| 新建 | `components/ui_render/include/ui_render.h` | 阶段4 |
| 新建 | `components/ui_render/ui_render.c` | 阶段4 |
| 新建 | `components/ui_render/draw_utils.c` | 阶段4 |
| 新建 | `components/ui_render/draw_utils.h` | 阶段4 |
| 新建 | `components/ui_render/font_data.c` | 阶段4 |
| 新建 | `components/ui_render/font_data.h` | 阶段4 |
| 新建 | `components/ui_render/layouts/month_view.c` | 阶段4 |
| 新建 | `components/ui_render/layouts/month_view.h` | 阶段4 |
| 新建 | `main/power_mgr.c` | 阶段5 |
| 新建 | `main/power_mgr.h` | 阶段5 |
| 新建 | `sdkconfig.defaults` | 阶段1 |
| 新建 | `tools/generate_font.py` | 阶段4 |
| 新建 | `charset_required.txt` | 阶段4 |
| 修改 | `main/app_wifi.c` — 移除硬编码凭据，参数化 | 阶段1 |
| 修改 | `main/app_wifi.h` — 更新函数签名 | 阶段1 |
| 修改 | `main/e-ink-calendar.c` — 完整主流程 | 阶段5 |
| 修改 | `main/CMakeLists.txt` — 添加组件依赖 | 阶段1,5 |

---

## 执行顺序与依赖关系

```
阶段 0: 硬件确认 ←──────────── 最高优先级，决定后续所有方向
    │
阶段 1: 安全修复（WiFi凭据） ←── 必须最先编码，无外部依赖
    │
    ├── 阶段 2: 墨水屏驱动 ←── 依赖阶段0硬件确认结果
    │                       可与阶段3/4并行
    ├── 阶段 3: 日历逻辑 ←── 纯计算，PC可开发测试
    │                       可与阶段2/4并行
    └── 阶段 4: UI 渲染 ←── 依赖阶段3的数据结构
                            可与阶段2并行
    │
阶段 5: 主应用集成 ←────────── 依赖阶段1-4全部完成
    │
阶段 6: 测试 ←────────────── 与各阶段同步，不单独滞后
    │
阶段 7: 可选增强 ←────────── 主功能稳定后追加
```

**建议**：阶段 1 完成后，阶段 2、3、4 可以并行推进——日历逻辑在 PC 上开发测试，驱动等硬件确认后对着数据手册写，UI 渲染写好后用模拟数据验证布局。

---

## 成功标准

- [ ] WiFi 凭据从 NVS 读取，不在源码中硬编码
- [ ] 开机自动连接 WiFi、NTP 对时、显示当月日历
- [ ] 墨水屏正确显示月视图（公历+农历+节气+节日）
- [ ] 今日日期有明显标记（反色/加粗边框）
- [ ] 屏幕刷新后 ESP32 进入 deep sleep，按间隔自动唤醒
- [ ] 日期变化后才刷新，断电重启不重复刷同一天
- [ ] 农历转换 1900-2100 年误差 < 1 天
- [ ] 电池供电可持续运行 1 个月以上

---

## 待用户确认

1. 墨水屏的具体型号、分辨率、驱动IC是什么？
2. 是三色屏（黑白红）还是单色屏（黑白）？
3. 需要用电池供电吗？电池容量？
4. 需要显示天气、待办等其他信息吗？
5. 是否需要物理按键交互（翻页、强制刷新）？
