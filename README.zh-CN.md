# AirScope

[English](README.md) | **简体中文**

AirScope 将 Waveshare ESP32-S3-Touch-LCD-1.69 打造成一台独立、可配置的
2.4 GHz Wi-Fi 实验接入点。项目由 ESP-IDF 固件、设备端 LVGL 状态界面以及
内嵌的 Preact 管理应用组成。

该项目用于开展可重复的无线配置与兼容性实验，无需依赖特定品牌的商用路由器。

## 功能特性

- 在中国 2.4 GHz 信道 1 至 13 上独立运行 SoftAP
- 通过信道切换公告（CSA）实时切换信道
- 持久化并严格校验 AP 与无线参数配置
- 支持开放、WPA2、WPA3、过渡模式和专家安全配置
- 提供本地 HTTPS 管理 API 和浏览器管理界面
- 支持浏览器会话和可撤销的自动化令牌
- 提供支持触摸操作的设备状态和初始化配置界面
- 在内存中记录运行事件，并通过 USB 串口输出诊断信息
- 支持启动时长按 GPIO40 五秒执行物理恢复

首个版本不提供上游 Wi-Fi、路由、NAT、强制门户、OTA、企业级认证、WPS、
抓包或 CSI 功能。

## 硬件与工具链

- Waveshare ESP32-S3-Touch-LCD-1.69
- ESP-IDF 6.0.2
- Node.js 22 或更高版本
- npm

ESP-IDF 依赖声明在各组件清单中，并通过 `dependencies.lock` 锁定版本。

## 构建

首先加载 ESP-IDF 开发环境：

```bash
source /path/to/esp-idf/export.sh
```

安装浏览器管理应用依赖，并生成固件内嵌的压缩资源：

```bash
cd web
npm ci
npm run build
cd ..
```

构建固件：

```bash
idf.py set-target esp32s3
idf.py build
```

烧录开发板并打开串口监视器：

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

请将 `/dev/ttyACM0` 替换为开发板实际使用的串口。按 `Ctrl+]` 退出监视器。

## 首次使用

1. 启动开发板并等待初始化配置界面出现。
2. 连接设备显示的 `AirScope-<MAC-last-6>` Wi-Fi 网络。
3. 打开 `https://192.168.4.1`。
4. 使用设备屏幕上显示的管理凭据登录。

默认 AP 为开放网络，并从信道 1 启动。管理网络使用 `192.168.4.1/24`。

如需恢复默认 AP 配置并轮换管理凭据，请在设备启动期间长按 GPIO40 五秒。

## Web 开发

在本地运行管理界面：

```bash
cd web
npm ci
npm run dev
```

运行浏览器测试：

```bash
cd web
npm run e2e
```

浏览器构建会将压缩资源写入 `components/airscope_api/web/`。这些生成文件需要
纳入版本管理，因为固件会直接嵌入它们。

## 仓库结构

```text
components/             ESP-IDF 组件
  airscope_api/         HTTPS API 和内嵌 Web 资源
  airscope_auth/        凭据、会话和自动化令牌
  airscope_board/       Waveshare 开发板适配层
  airscope_config/      配置模型和持久化
  airscope_display/     LVGL 状态界面
  airscope_events/      运行事件环形缓冲区
  airscope_wifi/        SoftAP 和信道控制
main/                   固件入口
web/                    Preact 管理应用
docs/                   系统设计和架构决策
```

系统设计详见 [docs/design-v1.md](docs/design-v1.md)，项目领域术语详见
[CONTEXT.md](CONTEXT.md)。

## 安全说明

- AirScope 是实验室工具。旧式安全模式只能用于隔离的测试环境。
- 设备使用本地生成的 HTTPS 身份，浏览器可能需要手动接受其证书。
- 不要提交导出的凭据、私钥、自动化令牌或本地环境文件。

## 许可证

项目暂未选择开源许可证。在添加许可证文件之前，所有权利由仓库所有者保留。
