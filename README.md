# Smart Feeder with Monitoring System using Raspberry Pi and ESP32 for Rescued Dogs at Tails of Freedom Animal Haven

An automated, smart, and scalable **Animal Feeder System** featuring real-time monitoring, weight-based feed level management, multi-device schedule distribution, and ESP32-CAM visual feedback verification.

---

## 📹 System Demonstration Videos

* 🎬 **Hardware & Operational Demo:** [Watch Video Demonstration](video/Thesis_demo.mp4)
* 💻 **Web Interface & Dashboard Demo:** [Watch UI Overview](video/Thesis_UI.mp4)

---

## 📌 Key System Features

* **Real-Time Automated Dispensing:** Configure custom feeding schedules (dates, times, and feed amounts in grams) via an interactive web dashboard.
* **Weight Sensing & Feed Level Monitoring:** Integrated load cell sensor continuously tracks container capacity and alerts users when food levels are low.
* **Dual-Stage Camera Feed Verification:** Microcontroller synchronization triggers automated snapshots at key intervals:
  * **During Feeding:** Captures snapshots during dispensing to verify animal presence.
  * **After Feeding:** Captures post-feeding snapshots to monitor food consumption and bowl status.
* **Interactive Analytics Dashboard:** Real-time data visualization displaying weekly feeding statistics and active module statuses.
* **PDF Report Generation:** Export feeding history logs and camera monitoring activity directly into downloadable PDF reports.
* **Multi-Module Support:** Centralized management system capable of controlling multiple feeder modules and cameras simultaneously.
* **Timezone Synchronization:** Automatic timestamp normalization for accurate event logging and scheduled dispatches.

---

## 🏗️ System Overview

The system consists of three main components working in harmony:

1. **Central Management Server & Web Dashboard:** Provides the user interface for schedule configuration, live camera monitoring, analytics, and history tracking.
2. **Dispenser Module (ESP32):** Manages servo-driven feed release, monitors food weight levels, and requests schedule execution from the central backend.
3. **Camera Monitoring Unit (ESP32-CAM):** Captures high-resolution snapshots before/during/after feeding events and wirelessly transmits image data to the central server.

---

## 📦 Deployment Specifications & Dependencies

### `Procfile` (Cloud Deployment Command)
```text
web: gunicorn app:app
```

### `requirements.txt` (Python Dependencies)
```text
Flask==3.0.0
flask-cors
pytz
python-dotenv
gunicorn
```
