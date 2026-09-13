# Profile Baseline

四个 Profile（`MINIMAL`、`OFFLINE`、`LOCAL`、`IOT`）的构建与裁剪基线，也是最小无业务应用。它不演示业务功能，只回答两个问题：

1. 每个 Profile 实际链接了什么、裁掉了什么；
2. 只引用 Esp32Base 时，各 Profile 的固件 Flash 和静态 RAM 是多少。

一个 PlatformIO 工程用 env 覆盖整个矩阵，env 命名为 `<chip>_<profile>`：

- 芯片：`esp32`、`esp32s3`、`esp32c3`（Core 2.0.16）；
- Profile：`minimal`、`offline`、`local`、`iot`；
- Core 3.3.8 代表性环境：`esp32_<profile>_arduino3`。

`src/main.cpp` 顶部的编译期断言校验四个 Profile 的能力契约；`deps_*.cpp` 是 LDF 关闭时各 Profile 的显式内置库锚点，不是业务代码。

构建与裁剪检查：

```sh
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline \
  -e esp32_minimal -e esp32_offline -e esp32_local -e esp32_iot
python3 scripts/check_trim_symbols.py
```

MQTT 是 IOT 相对 LOCAL 的唯一能力增量；评估 MQTT 体积时对比同芯片 `esp32_local` 与 `esp32_iot` 两个 ELF 的 RAM/Flash 差值，而不是只看 IOT 绝对值。

烧录命令见 [../FLASHING.md](../FLASHING.md)。
