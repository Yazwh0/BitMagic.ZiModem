// Definitions for the global instances the vendored sketch expects at file scope, the
// same way a real Arduino core provides a single `ESP`, `WiFi`, `SPIFFS`, etc. instance.
// One definition per global here as each is introduced; declarations live next to each
// class in zimodem_hal/arduino/*.h.

#include "zimodem_hal/arduino/ESP.h"
#include "zimodem_hal/arduino/HardwareSerial.h"
#include "zimodem_hal/arduino/WiFi.h"

ESPClass ESP;
WiFiClass WiFi;
HardwareSerialCompat zimodem_hal_serial;
