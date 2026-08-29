# bulldozer-telemetry

HEPTA-SAT 搭載 OBC 向けの自動テレメトリ記録ファームウェアです。電源投入後に自動でセンサーデータを SD カードへ CSV 保存します。

## ビルド・書き込み

- ボード: Generic RP2350 (`rp2040:rp2040:generic_rp2350`)
- VS Code タスク「Arduino: Build (Generic RP2350)」または:

```bash
arduino-cli compile --fqbn rp2040:rp2040:generic_rp2350 --build-path build .
```

## 動作概要

- 電源 ON で自動記録開始（GPS Fix 待ちなし）
- 9軸 IMU (BNO055): 100 Hz
- BME280: 1 Hz
- GPS: GPGGA 受信ごと（モジュール出力周期、約 1 Hz）
- GPS NMEA 処理は core 1 で非ブロッキング実行

## ピンアサイン

| 機器 | 信号 | ピン | 備考 |
|------|------|------|------|
| BME280 | SDA | GP6 | Wire1 |
| BME280 | SCL | GP7 | Wire1 |
| BME280 | I2C アドレス | 0x76 / 0x77 | 自動検出 |
| GPS (GP-1818MK) | RX | GP13 | ボード搭載 |
| GPS (GP-1818MK) | TX | GP2 | ボード搭載 |
| 9軸 IMU (BNO055) | I2C | Wire (GP4/GP5) | ボード搭載 |
| SD カード | SPI | ボード搭載 | HeptaCdh 経由 |

## SD ファイル

起動ごとにセッション番号 `NNNN` が付与されます。

### `log_NNNN_imu.csv` (100 Hz)

| 列 | 単位 | 説明 |
|----|------|------|
| millis | ms | 起動後経過時間 |
| ax, ay, az | m/s² | 加速度 |
| gx, gy, gz | deg/s | 角速度 |
| mx, my, mz | µT | 地磁気 |

### `log_NNNN_bme.csv` (1 Hz)

| 列 | 単位 | 説明 |
|----|------|------|
| millis | ms | 起動後経過時間 |
| temp_c | °C | 温度 |
| hum_pct | % | 湿度 |
| press_hpa | hPa | 気圧 |

### `log_NNNN_gps.csv` (GPGGA 受信時)

| 列 | 単位 | 説明 |
|----|------|------|
| millis | ms | 起動後経過時間 |
| utc | hhmmss.sss | GPGGA UTC 時刻 |
| lat, lon | deg | 緯度・経度（南/西は負） |
| alt | m | 高度 |
| fix_quality | - | 0=未Fix, 1=GPS, 2=DGPS |
| sat_num | - | 使用衛星数 |
| hdop | - | 水平精度低下率 |
| rmc_utc | hhmmss.sss | GPRMC UTC 時刻 |
| velocity | m/s | 対地速度 |
| heading | deg | 進行方向（真北） |
| date | ddmmyy | UTC 日付 |

未 Fix 時も `fix_quality=0` の行が記録されます。

### `SYSTEM.log` (イベント時)

| 列 | 説明 |
|----|------|
| millis | 起動後経過時間 |
| event | イベント種別 |
| detail | 詳細メッセージ |

イベント種別:

| event | 意味 |
|-------|------|
| BOOT | セッション開始 |
| IMU_ERROR | 9軸センサ読み取り失敗 |
| BME_ERROR | BME280 読み取り失敗 |
| SD_ERROR | SD 書き込み失敗 |
| SD_RECOVERED | SD 再初期化成功 |

## ライブラリ

`src/` は [HEPTA-SAT-Library](https://github.com/HEPTA-SAT-TRAINING/HEPTA-SAT-Library) サブモジュールです。
