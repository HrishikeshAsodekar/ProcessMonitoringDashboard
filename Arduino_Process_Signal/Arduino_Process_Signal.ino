/*
  Process Performance Monitor - signal source sketch (Arduino UNO)
  ------------------------------------------------------------------
  College project: the board plays the role of the process (and
  controller) and streams a step response over USB serial. The
  dashboard plots it and calculates the performance values.

  Serial: 115200 baud
  Commands (sent automatically by the dashboard, or type them in the
  Serial Monitor, line ending = "No line ending"):
      1 2 3 4   select the signal
      S         start a run with the selected signal
      R         stop / reset

  Output lines:
      META,NAME=...,SYS=...,Ts=0.2,SP=1.0,Tend=20,Kp=..,Ki=..,Kd=..
      DATA,time_s,setpoint,process_value
      END

  SIGNALS (every one can be rebuilt in MATLAB Simulink - see below)
  1  Underdamped second-order response
       Simulink: Step -> Transfer Fcn [1.44] / [1 0.72 1.44] -> Scope
       (wn = 1.2 rad/s, zeta = 0.3, K = 1)
  2  Sluggish first-order process with dead time
       Simulink: Step -> Transfer Fcn [1] / [4 1] -> Transport Delay 1 s
       -> Scope   (K = 1, tau = 4 s, dead time = 1 s)
  3  PID-controlled process (closed loop)  <-- recommended
       Simulink: Step -> Sum (+-) -> PID Controller (parallel, P=2, I=2,
       D=0.2, filter N=20) -> Transfer Fcn [1] / [1 2 1] -> Scope,
       with the plant output fed back to the Sum block
  4  Recorded response read from the original Scope screenshot
*/
#include <avr/pgmspace.h>
#include <math.h>

// ---------------- Settings ----------------
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

// Signal 4: recorded graph (time, value) kept in flash
const int N_PTS = 43;
const float TBL_T[] PROGMEM = { 0, 1.05, 1.23, 1.44, 1.73, 2, 2.5, 2.89, 3.21, 3.54, 3.8, 4.04, 4.33, 4.66, 4.98, 5.37, 5.79, 6.1, 6.4, 6.73, 7.14, 7.61, 8.05, 8.48, 8.84, 9.26, 9.9, 10.37, 10.8, 11.27, 11.85, 12.4, 12.97, 13.5, 14.03, 14.7, 15.3, 15.97, 17.1, 18, 19.1, 20, 30 };
const float TBL_Y[] PROGMEM = { 0, 0, 0.01, 0.07, 0.22, 0.5, 0.97, 1.277, 1.457, 1.524, 1.5, 1.424, 1.3, 1.14, 0.99, 0.877, 0.842, 0.866, 0.909, 0.974, 1.043, 1.084, 1.076, 1.043, 1.01, 0.976, 0.961, 0.974, 0.991, 1.01, 1.017, 1.009, 0.998, 0.992, 0.992, 0.998, 1.002, 1.004, 1, 0.998, 1, 1, 1 };

// ---------------- State ----------------
int  signalId = DEFAULT_SIGNAL;
bool running = false;
unsigned int sampleIndex = 0, nSamples = 0;
unsigned long nextSampleMs = 0;
float py = 0.0, pv_dot = 0.0, pInteg = 0.0, pXf = 0.0;   // signal 3 states

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
    float t1 = pgm_read_float(&TBL_T[i + 1]);
    if (t <= t1) {
      float t0 = pgm_read_float(&TBL_T[i]);
      float y0 = pgm_read_float(&TBL_Y[i]);
      float y1 = pgm_read_float(&TBL_Y[i + 1]);
      return y0 + (y1 - y0) * (t - t0) / (t1 - t0);
    }
  }
  return pgm_read_float(&TBL_Y[N_PTS - 1]);
}

// Signal 3: advance the closed loop by one sample time
void stepPid() {
  int n = (int)(TS / DT_SIM + 0.5);
  for (int k = 0; k < n; k++) {
    float e = SETPOINT - py;
    pInteg += e * DT_SIM;
    pXf += DT_SIM * NF * (e - pXf);
    float u = KP * e + KI * pInteg + KD * NF * (e - pXf);
    float acc = u - 2.0 * pv_dot - py;          // y'' + 2y' + y = u
    py += DT_SIM * pv_dot;
    pv_dot += DT_SIM * acc;
  }
}

void sendMeta() {
  Serial.print(F("META,NAME="));
  switch (signalId) {
    case 1: Serial.print(F("Underdamped second order,SYS=G(s)=1.44/(s^2+0.72s+1.44)")); break;
    case 2: Serial.print(F("Sluggish first order + dead time,SYS=G(s)=e^-1s/(4s+1)")); break;
    case 3: Serial.print(F("PID-controlled process,SYS=Plant 1/(s+1)^2 with PID (N=20)")); break;
    default: Serial.print(F("Recorded scope response,SYS=Recorded from Simulink Scope")); break;
  }
  Serial.print(F(",Ts="));   Serial.print(TS, 2);
  Serial.print(F(",SP="));   Serial.print(SETPOINT, 2);
  Serial.print(F(",Tend=")); Serial.print(tEnd(), 0);
  if (signalId == 3) {
    Serial.print(F(",Kp=")); Serial.print(KP, 2);
    Serial.print(F(",Ki=")); Serial.print(KI, 2);
    Serial.print(F(",Kd=")); Serial.println(KD, 2);
  } else {
    Serial.println(F(",Kp=-,Ki=-,Kd=-"));
  }
}

void startRun() {
  sampleIndex = 0;
  nSamples = (unsigned int)(tEnd() / TS + 0.5);
  py = 0.0; pv_dot = 0.0; pInteg = 0.0; pXf = 0.0;
  sendMeta();
  running = true;
  nextSampleMs = millis();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("READY"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '4' && !running) signalId = c - '0';
    else if (c == 'S' || c == 's') startRun();
    else if (c == 'R' || c == 'r') { running = false; Serial.println(F("READY")); }
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

  Serial.print(F("DATA,"));
  Serial.print(t, 2);        Serial.print(',');
  Serial.print(SETPOINT, 3); Serial.print(',');
  Serial.println(pv, 4);

  if (++sampleIndex > nSamples) {
    running = false;
    Serial.println(F("END"));
  }
}
