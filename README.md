# Mini Table Saw

Native C ESP32 firmware and a separate GTK 4 Windows diagnostic application.

- `main.c`: nonblocking console input and firmware loop (kept at repository root).
- `core/`: ESP-IDF component with motor, Hall and line protocol modules.
- `debug_ui/`: independent C11 CMake project, GTK window and Win32 serial worker.

## One-command build and run (PowerShell)

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\debug.ps1
```

Builds the GTK UI, then opens it if the build succeeds.
The ESP32 runs the firmware previously flashed onto it;
use `idf.py -p COM3 flash` to update it separately.
Use `-NoRun` to build without opening the UI.

Defaults to MSYS2 UCRT64 at `C:\msys64\ucrt64\bin`.
Override this with `-UcrtBin` when needed. ESP-IDF is not required for this script.
The script works from any directory when invoked by its full path, stops on
errors, and restores the terminal environment after launching the UI.

In the UI, Connect performs a one-time ESP health check and releases the port.
A successful check enables Build, which builds the firmware project including
`core/` through `firmware.ps1`. A successful build enables Run, which uploads
that build to the selected port and resets the ESP32 to start it.
Build/upload output and subsequent serial telemetry appear in DEBUG OUTPUT.
The serial monitor releases the port before another check, build or upload.

## Build

Firmware: in an ESP-IDF terminal at the repository root:

```
idf.py build
idf.py -p COM3 flash monitor
```

Windows GUI: in MSYS2 UCRT64 with GCC, CMake, Ninja and GTK4 installed:

```
cmake -S debug_ui -B debug_ui/build -G Ninja
cmake --build debug_ui/build
./debug_ui/build/minitablesaw_debug.exe
```

Run with UCRT64/bin on PATH for GTK DLLs. Close the serial monitor before connecting
(the COM port is exclusive). Select a detected port or type its name; restart the
application to refresh the detected list.

## Protocol and operation

115200 baud, 8N1, no flow control; ASCII commands terminated by LF (CRLF accepted).
The GUI opens the port on a worker thread and retries HELLO during ESP32 startup.
HELLO stops the motor, selects Hall mode and establishes a session. PING every
400 ms maintains it; the firmware stops and invalidates the session after 2 s
without PING. PWM n (integer 0..100) explicitly starts/applies output. STOP stops;
BYE stops and ends the session. Reconnection always requires a new explicit Start.
Changing the slider alone never starts the motor. Closing/disconnecting sends
STOP and BYE; the firmware timeout also covers cable loss or a crashed GUI.
GUI transport loss/absent telemetry disables controls and resets the slider.

Firmware reports `STATE PWM=0 HALL=1 MODE=HALL` at least every 500 ms and upon
Hall changes/commands. ERR replies indicate invalid commands or timeout.
Oversized input lines are discarded, never executed as partial commands.
Legacy manual commands remain: m (motor mode), h (Hall mode and stop), s (stop),
0..100 (PWM in motor mode outside a GUI session). Manual mode has no heartbeat;
use s/h to stop. GUI PWM commands require a live HELLO/PING session.

Hardware pins are unchanged: R_EN 32, RPWM 33, L_EN 26, LPWM 25; Hall input 27.
PWM is 20 kHz, 10 bits, one direction. Enables remain low at initialization and
zero speed. Hall input uses the external 3.3 V pull-up. No RPM measurement yet;
polling Hall state is diagnostic and cannot count fast pulses reliably.

## Bench validation

Test without a saw blade. First confirm boot and connection leave PWM at zero;
check Hall HIGH/LOW using the sensor. Apply a low PWM explicitly, test STOP,
disconnect and close, then unplug USB while running and verify timeout stop.
Try invalid/oversized commands and reconnect; none should start the motor.
Physical emergency power cutoff remains necessary; software stop is not a
replacement. Hardware and serial end-to-end behavior need bench verification.

Host-only protocol checks (MSYS2 UCRT64, from repository root):

```
gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -Icore tests/protocol_test.c core/protocol.c -o debug_ui/build/protocol_test.exe
./debug_ui/build/protocol_test.exe
```

These mock motor/Hall/time to check legacy commands, invalid input, session
requirements, STOP/BYE, and heartbeat expiry including a command arriving late.
They do not drive hardware or substitute for bench tests.
