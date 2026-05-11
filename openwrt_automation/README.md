# OpenWrt MQTT Automation Scripts

Các script tự động hoá chạy trực tiếp trên router OpenWrt, sử dụng MQTT broker Mosquitto local.
Không phụ thuộc điện thoại — luôn hoạt động khi router còn bật.

## Yêu cầu

```bash
opkg update
opkg install mosquitto-client-nossl coreutils-nohup
```

## Cài đặt

1. Copy thư mục `scripts/` vào router:
```bash
scp -r scripts/ root@192.168.0.121:/root/iot-automation/
```

2. Cấp quyền thực thi:
```bash
chmod +x /root/iot-automation/*.sh
```

3. Chỉnh sửa cấu hình broker trong `/root/iot-automation/config.sh`

4. Thêm crontab (xem phần Crontab bên dưới)

## Cấu trúc file

| File | Mô tả |
|------|--------|
| `config.sh` | Cấu hình broker MQTT (host, port, user, pass) |
| `mqtt_cmd.sh` | Gửi lệnh MQTT tới thiết bị (relay/led ON/OFF) |
| `schedule.sh` | Chạy lệnh theo lịch (gọi từ crontab) |
| `condition_monitor.sh` | Giám sát sensor và tự động điều khiển khi điều kiện thỏa |
| `countdown.sh` | Hẹn giờ đếm ngược rồi thực hiện lệnh |
| `sunrise_sunset.sh` | Tính toán giờ mặt trời mọc/lặn và thực hiện lệnh |

## Sử dụng

### Gửi lệnh thủ công
```bash
# Bật relay của thiết bị "esp01"
/root/iot-automation/mqtt_cmd.sh esp01 relay true

# Tắt LED
/root/iot-automation/mqtt_cmd.sh esp01 led false
```

### Hẹn giờ đếm ngược
```bash
# Tắt relay sau 30 phút (1800 giây)
/root/iot-automation/countdown.sh 1800 esp01 relay false &
```

### Giám sát điều kiện (chạy nền)
```bash
# Khi nhiệt độ > 30°C → bật relay
nohup /root/iot-automation/condition_monitor.sh \
  aht10_01 temperature gt 30 esp01 relay true &
```

## Crontab

Thêm vào crontab (`crontab -e`):

```cron
# Bật relay lúc 6:00 sáng mỗi ngày
0 6 * * * /root/iot-automation/mqtt_cmd.sh esp01 relay true

# Tắt relay lúc 22:00 tối mỗi ngày
0 22 * * * /root/iot-automation/mqtt_cmd.sh esp01 relay false

# Bật LED lúc 18:00 thứ 2-6 (Mon-Fri)
0 18 * * 1-5 /root/iot-automation/mqtt_cmd.sh esp01 led true

# Tắt LED lúc 23:00 hàng ngày
0 23 * * * /root/iot-automation/mqtt_cmd.sh esp01 led false

# Sunrise/sunset automation (chạy mỗi ngày lúc 0:01)
1 0 * * * /root/iot-automation/sunrise_sunset.sh

# Khởi động lại condition monitor sau reboot
@reboot nohup /root/iot-automation/condition_monitor.sh aht10_01 temperature gt 30 esp01 relay true &
```

## Ghi chú

- Broker local: `192.168.0.121:1883`
- Broker qua internet: `adangdang.ddns.net:443` (router forward 443 → 1883)
- ESP8266 kết nối qua `adangdang.ddns.net:443`
- Các script trên OpenWrt dùng `127.0.0.1:1883` (local, nhanh hơn)
