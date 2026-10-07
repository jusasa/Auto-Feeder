# 🐶 Smart Pet Feeder (스마트 사료 배급기)

> ESP-IDF 기반 저지터 하드웨어 PWM 제어 및 Home Assistant 표준(MQTT Discovery / Dual OTA)을 준수한 스마트 IoT 사료 배급 시스템

<!-- 동작 데모 GIF 또는 완성된 하드웨어 사진 -->
<!-- ![프로젝트 미리보기](docs/images/demo.gif) -->

---

## 📌 프로젝트 소개

- **진행 기간:** 2026.10 - 2026.MM
- **팀 구성:** 1인 프로젝트 (하드웨어 회로 설계, 펌웨어 개발, Docker 인프라 구축)
- **개발 환경:** ESP-IDF v6.x (C, FreeRTOS), VS Code Dev Container

장시간 외출 시 반려동물에게 정해진 시간에 정량의 사료를 안정적으로 배급하고, 사료 변질 및 모터 잼(Jam) 현상을 원격에서 관제할 수 있는 시스템입니다. 안정적인 무중단 24/7 가동을 목표로 하드웨어 전원 분리 설계와 상용 수준의 스마트홈 표준 프로토콜을 적용했습니다.

---

## ✨ 주요 기능

- **정밀 사료 토출 제어**: 하드웨어 타이머(LEDC) 14-bit PWM을 이용한 360° 연속 회전 서보 모터 구동 및 영점 보정
- **스마트홈 연동 (Home Assistant)**: MQTT Auto Discovery 규격을 준수하여 배급 버튼 및 상태 센서 자동 등록
- **Active-Low 상태 인디케이터**: 부팅 시 글리치(튀는 현상) 방지 및 MCU 싱크 구동을 고려한 상태 표시 LED


---

## 🛠 기술 스택

| 구분 | 기술 / 도구 |
| :--- | :--- |
| **Firmware / OS** | ESP-IDF (C), FreeRTOS, Kconfig (`menuconfig`) |
| **Hardware** | ESP32 DevKit, MG90S (360° Continuous Servo), Orange LED, 외부 5V 전원 회로 |
| **Protocol** | MQTT |
| **Server & Infra** | Ubuntu Server, Docker Compose, Mosquitto, Home Assistant, Nginx |
| **DevOps** | VS Code Dev Container, Git |

---

## 🔌 하드웨어 회로 및 핀 매핑 (Hardware Pinout)

> **전원 분리 설계**: 모터 기동 시 발생하는 서지 전류 및 역기전력으로 인한 MCU 브라운아웃(Brownout)을 방지하기 위해 ESP32 전원과 모터 5V 전원을 분리하고 접지(GND)를 공통 접지(Common GND)로 결선했습니다.



| 부품 | ESP32 핀 | 회로 구성 및 제어 방식 | 비고 |
| :--- | :--- | :--- | :--- |
| **MG90S 서보 모터** | GPIO 18 | 하드웨어 타이머 LEDC Ch0 (50Hz PWM) | 외부 5V 단독 전원 인가 |
| **상태 표시 LED (주황)** | GPIO 19 | Active-Low (3.3V VCC ➔ 220Ω 저항 ➔ LED ➔ GPIO 싱크) | 구동 전류 약 5.9mA |
| **모터 전원** | - | 외부 5V 전원 어댑터 직결 | 전원 충돌 방지 |

---

## 🏗 시스템 아키텍처 (System Architecture)

```text
                  [ Home Assistant ]
                                ▲             
                    (MQTT Discovery)
                                ▼            
                    [ Mosquitto Broker ]    
                    (Ubuntu / Docker)
                                ▲
            (Command: FEED)  │  (Status: ONLINE/DONE)
                                ▼
                    [ ESP32 Controller ] 
                    ├── LEDC PWM (50Hz) ────► [ MG90S 360° Servo ]
                    └── GPIO Sink (Active-Low)► [ Status LED ]
           