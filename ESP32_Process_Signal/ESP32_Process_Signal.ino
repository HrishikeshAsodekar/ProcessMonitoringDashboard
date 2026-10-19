/*
  Process Performance Monitor - Wireless Signal Source (ESP32 / ESP8266)
  ------------------------------------------------------------------------
  Runs the step-response process models and PID controller directly on an ESP32.
  Streams live telemetry over WiFi via WebSockets directly to smartphones and laptops.
  
  Features:
  - Generates all 4 signals (Underdamped 2nd order, 1st order + dead time, Closed loop PID, Recorded scope)
  - Creates a WiFi Access Point ("Process-Monitor-ESP32") or connects to existing WiFi
  - Runs a WebSocket server on port 81 (ws://192.168.4.1:81 or local IP)
  - Also outputs to Serial at 115200 baud for dual USB and WiFi monitoring!
  
  Commands accepted via WebSocket or Serial:
      1 2 3 4   select signal
      S         start step response run
      R         stop / reset
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WebSocketsServer.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <WebSocketsServer.h>
#else
  #error "This sketch is designed for ESP32 or ESP8266 boards with WiFi."
#endif

#include <math.h>

// ---------------- WiFi Configuration ----------------
// Leave SSID empty to start in Access Point (hotspot) mode:
const char* AP_SSID = "Process-Monitor-ESP32";
const char* AP_PASS = "12345678"; // Min 8 chars, or set to "" for open network

// Or connect to your home / lab WiFi:
const char* STA_SSID = ""; // e.g. "MyHomeWiFi"
const char* STA_PASS = "";

WebSocketsServer webSocket = WebSocketsServer(81);

// ---------------- Process Simulation Settings ----------------
#define DEFAULT_SIGNAL 3
const float SETPOINT = 1.0;     // step size
const float TS       = 0.2;     // sample time (s)

// Signal 1: second order
const float Z1 = 0.3, WN1 = 1.2;
// Signal 2: first order + dead time
const float K2 = 1.0, TAU2 = 4.0, THETA2 = 1.0;
// Signal 3: plant 1/(s+1)^2 with PID
const float KP = 2.0, KI = 2.0, KD = 0.2, NF = 20.0;
const float DT_SIM = 0.005;     // internal integration step (s)

// Signal 4: recorded graph
const int N_PTS = 43;
const float TBL_T[] = { 0, 1.05, 1.23, 1.44, 1.73, 2, 2.5, 2.89, 3.21, 3.54, 3.8, 4.04, 4.33, 4.66, 4.98, 5.37, 5.79, 6.1, 6.4, 6.73, 7.14, 7.61, 8.05, 8.48, 8.84, 9.26, 9.9, 10.37, 10.8, 11.27, 11.85, 12.4, 12.97, 13.5, 14.03, 14.7, 15.3, 15.97, 17.1, 18, 19.1, 20, 30 };
const float TBL_Y[] = { 0, 0, 0.01, 0.07, 0.22, 0.5, 0.97, 1.277, 1.457, 1.524, 1.5, 1.424, 1.3, 1.14, 0.99, 0.877, 0.842, 0.866, 0.909, 0.974, 1.043, 1.084, 1.076, 1.043, 1.01, 0.976, 0.961, 0.974, 0.991, 1.01, 1.017, 1.009, 0.998, 0.992, 0.992, 0.998, 1.002, 1.004, 1, 0.998, 1, 1, 1 };

// ---------------- State ----------------
int signalId = DEFAULT_SIGNAL;
bool running = false;
unsigned int sampleIndex = 0, nSamples = 0;
unsigned long nextSampleMs = 0;
float py = 0.0, pv_dot = 0.0, pInteg = 0.0, pXf = 0.0;

float tEnd() {
  switch (signalId) {
    case 1: return 25.0;
    case 2: return 30.0;
    case 3: return 20.0;
    default: return 30.0;
  }
}

float sig1(float t) {
  float z = Z1, wn = WN1;
  float wd = wn * sqrt(1.0 - z * z);
  float phi = acos(z);
  return 1.0 - exp(-z * wn * t) / sqrt(1.0 - z * z) * sin(wd * t + phi);
}

float sig2(float t) {
  if (t < THETA2) return 0.0;
  return K2 * (1.0 - exp(-(t - THETA2) / TAU2));
}

float sig4(float t) {
  for (int i = 0; i < N_PTS - 1; i++) {
    if (t <= TBL_T[i + 1]) {
      return TBL_Y[i] + (TBL_Y[i + 1] - TBL_Y[i]) * (t - TBL_T[i]) / (TBL_T[i + 1] - TBL_T[i]);
    }
  }
  return TBL_Y[N_PTS - 1];
}

void stepPid() {
  int n = (int)(TS / DT_SIM + 0.5);
  for (int k = 0; k < n; k++) {
    float e = SETPOINT - py;
    pInteg += e * DT_SIM;
    pXf += DT_SIM * NF * (e - pXf);
    float u = KP * e + KI * pInteg + KD * NF * (e - pXf);
    float acc = u - 2.0 * pv_dot - py;
    py += DT_SIM * pv_dot;
    pv_dot += DT_SIM * acc;
  }
}

void sendLine(const String& line) {
  Serial.println(line);
  webSocket.broadcastTXT(line);
}

void sendMeta() {
  String meta = "META,NAME=";
  switch (signalId) {
    case 1: meta += "Underdamped second order,SYS=G(s)=1.44/(s^2+0.72s+1.44)"; break;
    case 2: meta += "Sluggish first order + dead time,SYS=G(s)=e^-1s/(4s+1)"; break;
    case 3: meta += "PID-controlled process,SYS=Plant 1/(s+1)^2 with PID (N=20)"; break;
    default: meta += "Recorded scope response,SYS=Recorded from Simulink Scope"; break;
  }
  meta += ",Ts=" + String(TS, 2);
  meta += ",SP=" + String(SETPOINT, 2);
  meta += ",Tend=" + String(tEnd(), 0);
  if (signalId == 3) {
    meta += ",Kp=" + String(KP, 2) + ",Ki=" + String(KI, 2) + ",Kd=" + String(KD, 2);
  } else {
    meta += ",Kp=-,Ki=-,Kd=-";
  }
  sendLine(meta);
}

void startRun() {
  sampleIndex = 0;
  nSamples = (unsigned int)(tEnd() / TS + 0.5);
  py = 0.0; pv_dot = 0.0; pInteg = 0.0; pXf = 0.0;
  sendMeta();
  running = true;
  nextSampleMs = millis();
}

void handleCommand(char c) {
  if (c >= '1' && c <= '4' && !running) signalId = c - '0';
  else if (c == 'S' || c == 's') startRun();
  else if (c == 'R' || c == 'r') { running = false; sendLine("READY"); }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[WS] Client #%u connected\n", num);
    webSocket.sendTXT(num, "READY");
  } else if (type == WStype_TEXT) {
    for (size_t i = 0; i < length; i++) handleCommand((char)payload[i]);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- ESP32 Process Performance Signal Source ---");

  if (strlen(STA_SSID) > 0) {
    Serial.printf("Connecting to WiFi: %s\n", STA_SSID);
    WiFi.begin(STA_SSID, STA_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi Connected!");
    Serial.print("ESP32 IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.printf("Starting Access Point: %s\n", AP_SSID);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP IP Address: "); Serial.println(WiFi.softAPIP());
  }

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");
  Serial.println("READY");
}

void loop() {
  webSocket.loop();

  while (Serial.available()) {
    handleCommand((char)Serial.read());
  }

  if (!running) return;

  unsigned long now = millis();
  if ((long)(now - nextSampleMs) < 0) return;
  nextSampleMs += (unsigned long)(TS * 1000.0);

  float t = sampleIndex * TS;
  float pv;
  switch (signalId) {
    case 1: pv = SETPOINT * sig1(t); break;
    case 2: pv = SETPOINT * sig2(t); break;
    case 3: pv = py; stepPid(); break;
    default: pv = SETPOINT * sig4(t); break;
  }

  String data = "DATA," + String(t, 2) + "," + String(SETPOINT, 3) + "," + String(pv, 4);
  sendLine(data);

  if (++sampleIndex > nSamples) {
    running = false;
    sendLine("END");
  }
}
