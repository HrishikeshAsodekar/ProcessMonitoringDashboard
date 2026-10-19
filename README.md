# Process Performance Monitor - Mobile & Web Deployment Guide

A real-time step-response analysis dashboard engineered for seamless monitoring across **smartphones, tablets, and laptops**.

---

## 📱 How Smartphone Monitoring Works

When deployed to the web (e.g., GitHub Pages, Vercel, Netlify, or a local server), smartphones can monitor and control the process through **5 versatile connection modes**:

```
                       ┌────────────────────────────────────────────────────────┐
                       │               DEPLOYED WEB DASHBOARD                   │
                       │           (GitHub Pages / Vercel / Cloud)              │
                       └───────────────────▲────────────────────────────────────┘
                                           │
             ┌─────────────────────────────┼─────────────────────────────┐
             │                             │                             │
   [1. Cloud QR Pairing]          [2. WiFi Bridge]              [3. USB OTG]
             │                             │                             │
    Laptop with Arduino           PC running bridge.py          Android Phone with
   shares live telemetry         broadcasts to local WiFi        USB-C OTG cable
     via secure cloud             (ws://192.168.x.x:8765)      direct to Arduino
             │                             │                             │
             └───────────────────────┬─────┴─────────────────────────────┘
                                     │
                             ┌───────▼────────┐
                             │  SMARTPHONE    │
                             │  iOS / Android │
                             └────────────────┘
```

---

## 🚀 Connection Methods for Smartphones

### Method 1: Cloud / Remote Phone Pairing (Recommended · Zero Install)
*Best for: Arduino connected to a laptop while you walk around the lab or workshop monitoring from your phone.*

1. Open the deployed website on your laptop (e.g., `https://your-domain.com`).
2. Connect your Arduino board via USB and click **Connect Device → USB Serial**.
3. Click the **📱 Pair Phone** button on the toolbar or modal.
4. A pairing modal will display a **QR Code** and a **4-letter Room Code** (e.g., `P8K2`).
5. Open your smartphone camera, point it at the QR code, and tap the link!
6. Your smartphone opens the dashboard and instantly begins streaming the process in real-time.
7. **Two-Way Control**: You can tap **Start**, **Stop**, and change **Signals** directly from your phone to control the Arduino!

---

### Method 2: Local WiFi Bridge (`bridge.py` or `bridge.js`)
*Best for: Local area networks, university labs, or private subnets.*

1. Connect your Arduino board to your computer via USB.
2. In your terminal, run:
   ```bash
   python bridge.py
   ```
   *(Or using Node.js: `node bridge.js`)*
3. The bridge script will auto-detect your Arduino COM port, start the server, and print your local network address:
   ```
   ===============================================================
   🚀 PROCESS PERFORMANCE MONITOR - SMARTPHONE & NETWORK BRIDGE
   ===============================================================
   📡 Hardware:   Arduino connected on COM3 (115200 baud)
   🌐 Local IP:   192.168.1.50
   📱 Web URL:    http://192.168.1.50:8765
   ⚡ WebSocket:  ws://192.168.1.50:8765
   ===============================================================
   ```
4. On your smartphone (connected to the same WiFi or your laptop's hotspot), open your mobile browser and visit:
   `http://192.168.1.50:8765`
5. The dashboard loads instantly with direct, ultra-low-latency WebSocket streaming!

---

### Method 3: Direct USB OTG to Smartphone (Android)
*Best for: Direct plug-and-play monitoring with no PC or WiFi network needed.*

1. Connect your Arduino UNO to your Android smartphone using a standard **USB-C to USB-A OTG adapter**.
2. Open the deployed website in **Google Chrome** on your Android phone.
3. Tap **⚡ Connect Device → USB Serial / OTG**.
4. Chrome will display the native Android USB device prompt: tap your Arduino board and press **Connect**.
5. Telemetry streams directly over the USB cable at 115200 baud!

---

### Method 4: Standalone ESP32 Wireless Process Generator
*Best for: Completely wireless field testing without any laptop.*

1. Open `ESP32_Process_Signal/ESP32_Process_Signal.ino` in the Arduino IDE.
2. Flash it to any standard ESP32 or ESP8266 board ($3 microcontroller).
3. The ESP32 creates its own WiFi hotspot:
   - **SSID**: `Process-Monitor-ESP32`
   - **Password**: `12345678`
4. Connect your smartphone to the `Process-Monitor-ESP32` WiFi network.
5. In the deployed dashboard, tap **⚡ Connect Device → WiFi / WebSocket**, select the **ESP32 AP (`ws://192.168.4.1:81`)** preset, and tap **Connect**!

---

### Method 5: Interactive Simulation / Demo Mode
*Best for: Teaching, offline demonstrations, and algorithm verification on the go.*

- Tap **🧪 Run Demo** directly on the dashboard.
- Features optional real-world effects:
  - **Sluggish Process**: 1.5× slower system response
  - **Measurement Noise**: Realistic bounded sensor noise (±0.01)
  - **Load Disturbance**: Sudden load disturbance step at $t = 12\text{ s}$ that decays

---

## 📱 Mobile-First Features & Enhancements

- **Touch Crosshair HUD**: Drag your finger across the oscilloscope curve to inspect exact sample values $(t, SP, PV, e)$ with a vertical guide and pulsating target marker.
- **Quick KPI Summary Strip**: Key metrics (Rise Time, Peak / Overshoot, Settling Time, Steady-State Error) are prominently displayed directly beneath the oscilloscope, eliminating the need to scroll on small smartphone screens.
- **Adaptive Mobile Tabs**: Clean segmented switcher (`[ 📊 All Metrics ]`, `[ ⚙️ System Inputs ]`, `[ 📋 Live Log ]`) on smartphones, automatically expanding to the full multi-column layout on desktop monitors.
- **Fullscreen Oscilloscope**: Tap the **⛶ Fullscreen** button or rotate your phone to landscape mode to maximize the oscilloscope for high-resolution waveform inspection.
- **Screen Wake Lock**: Automatically prevents the phone screen from dimming or locking during an active 30-second run.
- **Haptic Feedback**: Subtle vibration cues on button taps, peak overshoot crossings, and run completion.
- **Audio Completion Tone**: Gentle synthesized chime when the process reaches settling or finishes (with 🔊/🔇 toggle).
- **Mobile Share API**: One-tap CSV sharing directly to WhatsApp, AirDrop, Gmail, or Files.
- **PWA Installation**: Add to Home Screen on iOS and Android for a fullscreen native-app experience.

---

## 🌐 Deploying to the Web

### GitHub Pages (1 Minute)
1. Push this folder to a GitHub repository.
2. Go to repository **Settings → Pages**.
3. Under **Build and deployment**, select **Source: Deploy from a branch** and choose `main` / `/ (root)`.
4. Click **Save**. Your dashboard is live on HTTPS in seconds!

### Vercel / Netlify
- Drag and drop this folder into the Vercel or Netlify web console, or connect your Git repository.
- No build steps or configuration needed (zero-config static deployment).

---

## 📊 Process Signals & Metrics Reference

| Signal | System Name | Transfer Function | $T_{\text{end}}$ | Controller |
| :--- | :--- | :--- | :---: | :---: |
| **1** | Underdamped 2nd order | $G(s) = \frac{1.44}{s^2 + 0.72s + 1.44}$ | $25\text{ s}$ | Open Loop |
| **2** | Sluggish 1st order + dead time | $G(s) = \frac{e^{-s}}{4s + 1}$ | $30\text{ s}$ | Open Loop |
| **3** | PID-controlled process *(Recommended)* | Plant $\frac{1}{(s+1)^2}$ with parallel PID | $20\text{ s}$ | $K_p=2, K_i=2, K_d=0.2, N=20$ |
| **4** | Recorded scope response | Simulink oscilloscope benchmark | $30\text{ s}$ | Benchmark |

### Calculated Metrics Formulas
- **Rise Time ($T_r$)**: $t_{90\%} - t_{10\%}$ with linear interpolation.
- **Peak Time ($T_p$) & Overshoot ($M_p$)**: $M_p = \frac{PV_{\max} - SP}{SP} \times 100\%$.
- **Settling Time ($T_s$)**: Time after which the process value remains strictly within $SP \pm \text{band}\%$.
- **Steady-State Error ($e_{ss}$)**: $SP - \text{mean of last } 5\% \text{ of samples}$.
- **Error Integrals**:
  - $\text{IAE} = \int |e(t)|\,dt$
  - $\text{ISE} = \int e^2(t)\,dt$
  - $\text{ITAE} = \int t \cdot |e(t)|\,dt$
