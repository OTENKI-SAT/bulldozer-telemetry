#include "src/HeptaSat.h"
#include "src/common/gps_async.h"
#include "src/common/gps_fix.h"
#include "src/drv/bme280_bosch.h"

HeptaCdh    cdh;
HeptaCom    com;
HeptaEps    eps;
HeptaSensor sensor;
Bme280      bme;

File imuFile;
File bmeFile;
File gpsFile;

static const uint32_t BME_INTERVAL_MS   = 1000;
static const uint32_t IMU_INTERVAL_MS = 10;
static const char    *kSystemLogFile    = "SYSTEM.log";

volatile bool initialized = false;

uint32_t lastBmeMs           = 0;
uint32_t lastImuMs           = 0;
uint32_t lastGpsLoggedMillis = 0;
uint16_t imuFlushCounter     = 0;
uint16_t sessionNumber       = 0;
bool     loggingReady        = false;
uint32_t lastImuErrorMs      = 0;
uint32_t lastBmeErrorMs      = 0;
uint32_t lastStatusMs          = 0;
uint16_t imuSampleCount        = 0;
float    lastBmeTemp           = 0.0f;
float    lastBmeHum            = 0.0f;
float    lastBmePress          = 0.0f;
bool     lastBmeOk             = false;
float    lastAx                = 0.0f;
float    lastAy                = 0.0f;
float    lastAz                = 0.0f;

static uint16_t next_session_number(void) {
  uint16_t maxSession = 0;

  for (uint16_t n = 1; n <= 9999; n++) {
    char path[24];
    snprintf(path, sizeof(path), "log_%04u_imu.csv", n);
    if (cdh.file_exists(path)) {
      maxSession = n;
    }
  }

  if (maxSession >= 9999) {
    return 9999;
  }
  return maxSession + 1;
}

static void log_system_event(const char *event, const char *detail) {
  File f = cdh.append_file(kSystemLogFile);
  if (!f) {
    return;
  }
  cdh.printf_file(f, "%lu,%s,%s\n", millis(), event, detail);
  f.close();
}

static bool write_row_ok(File &file, size_t written) {
  if (file && written > 0) {
    return true;
  }

  log_system_event("SD_ERROR", "write failed");
  if (cdh.sd_begin()) {
    log_system_event("SD_RECOVERED", "sd_begin ok");
  }
  return false;
}

static bool open_session_files(uint16_t session) {
  char imuPath[24];
  char bmePath[24];
  char gpsPath[24];

  snprintf(imuPath, sizeof(imuPath), "log_%04u_imu.csv", session);
  snprintf(bmePath, sizeof(bmePath), "log_%04u_bme.csv", session);
  snprintf(gpsPath, sizeof(gpsPath), "log_%04u_gps.csv", session);

  imuFile = cdh.create_file(imuPath);
  if (!imuFile) {
    return false;
  }
  cdh.printf_file(imuFile, "millis,ax,ay,az,gx,gy,gz,mx,my,mz\n");

  bmeFile = cdh.create_file(bmePath);
  if (!bmeFile) {
    imuFile.close();
    imuFile = File();
    return false;
  }
  cdh.printf_file(bmeFile, "millis,temp_c,hum_pct,press_hpa\n");

  gpsFile = cdh.create_file(gpsPath);
  if (!gpsFile) {
    imuFile.close();
    bmeFile.close();
    imuFile = File();
    bmeFile = File();
    return false;
  }
  cdh.printf_file(gpsFile,
                  "millis,utc,lat,lon,alt,fix_quality,sat_num,hdop,"
                  "rmc_utc,velocity,heading,date\n");

  sessionNumber       = session;
  loggingReady        = true;
  imuFlushCounter     = 0;
  lastBmeMs           = millis();
  lastImuMs           = millis();
  lastStatusMs        = millis();
  lastGpsLoggedMillis = 0;

  cdh.printf("Session %04u started: %s, %s, %s\n",
             session, imuPath, bmePath, gpsPath);
  return true;
}

static bool try_start_logging(void) {
  if (loggingReady) {
    return true;
  }
  if (!cdh.sd_is_available() && !cdh.sd_begin()) {
    return false;
  }
  return open_session_files(next_session_number());
}

static void write_imu_row(uint32_t now,
                          float ax, float ay, float az,
                          float gx, float gy, float gz,
                          float mx, float my, float mz) {
  if (!imuFile) {
    return;
  }

  size_t written = cdh.printf_file(imuFile,
                                   "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                                   now, ax, ay, az, gx, gy, gz, mx, my, mz);
  if (!write_row_ok(imuFile, written)) {
    return;
  }

  imuFlushCounter++;
  if (imuFlushCounter >= 100) {
    imuFile.flush();
    imuFlushCounter = 0;
  }
}

static void write_bme_row(uint32_t now, float temp, float hum, float press) {
  if (!bmeFile) {
    return;
  }

  size_t written = cdh.printf_file(bmeFile, "%lu,%.2f,%.2f,%.2f\n",
                                   now, temp, hum, press);
  if (write_row_ok(bmeFile, written)) {
    bmeFile.flush();
  }
}

static void write_gps_row(uint32_t now, const GpsFix &fix) {
  if (!gpsFile) {
    return;
  }

  size_t written = cdh.printf_file(gpsFile,
                                   "%lu,%.3f,%.7f,%.7f,%.2f,%u,%u,%.2f,%.3f,%.3f,%.3f,%s\n",
                                   now,
                                   fix.gga_utc,
                                   fix.lat, fix.lon, fix.alt,
                                   fix.fix_quality, fix.sat_num, fix.hdop,
                                   fix.rmc_utc, fix.velocity, fix.heading,
                                   fix.date);
  if (write_row_ok(gpsFile, written)) {
    gpsFile.flush();
  }
}

static void print_status(uint32_t now, const GpsFix &fix) {
  cdh.printf("[%lu] SD:%s S%04u IMU:%u/s acc=%.2f,%.2f,%.2f",
             now,
             loggingReady ? "OK" : "WAIT",
             sessionNumber,
             imuSampleCount,
             lastAx, lastAy, lastAz);

  if (lastBmeOk) {
    cdh.printf(" BME=%.1fC %.1f%% %.1fhPa",
               lastBmeTemp, lastBmeHum, lastBmePress);
  } else {
    cdh.print(" BME=ERR");
  }

  cdh.printf(" GPS=%s fix=%u sats=%u hdop=%.1f",
             fix.has_fix ? "FIX" : "NO",
             fix.fix_quality, fix.sat_num, fix.hdop);

  if (fix.has_fix) {
    cdh.printf(" lat=%.6f lon=%.6f alt=%.1fm vel=%.2fm/s",
               fix.lat, fix.lon, fix.alt, fix.velocity);
  }

  cdh.println("");
  imuSampleCount = 0;
}

void setup() {
  gps_async_init();

  cdh.begin();
  eps.init();
  sensor.begin();
  bme.begin();

  if (try_start_logging()) {
    log_system_event("BOOT", "session started");
    cdh.println("Telemetry logging started.");
  } else {
    cdh.println("SD not ready; will retry in loop.");
  }

  initialized = true;
}

void setup1() {
  while (!initialized) {
  }
}

void loop1() {
  sensor.gps_service();
}

void loop() {
  uint32_t now = millis();

  if (!loggingReady) {
    try_start_logging();
    if (!loggingReady) {
      if (now - lastStatusMs >= BME_INTERVAL_MS) {
        lastStatusMs = now;
        GpsFix fix;
        sensor.gps_get_latest(&fix);
        print_status(now, fix);
      }
      return;
    }
    log_system_event("BOOT", "session started");
  }

  if (now - lastImuMs >= IMU_INTERVAL_MS) {
    lastImuMs = now;

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    float mx = 0.0f, my = 0.0f, mz = 0.0f;

    bool accOk = sensor.get_acceleration(&ax, &ay, &az);
    bool gyroOk = sensor.get_gyro(&gx, &gy, &gz);
    bool magOk = sensor.get_magnetometer(&mx, &my, &mz);

    if (accOk && gyroOk && magOk) {
      lastAx = ax;
      lastAy = ay;
      lastAz = az;
      write_imu_row(now, ax, ay, az, gx, gy, gz, mx, my, mz);
      imuSampleCount++;
    } else if (now - lastImuErrorMs >= 1000) {
      lastImuErrorMs = now;
      log_system_event("IMU_ERROR", "read failed");
    }
  }

  if (now - lastBmeMs >= BME_INTERVAL_MS) {
    lastBmeMs = now;

    float temp = 0.0f, hum = 0.0f, press = 0.0f;
    if (bme.read(&temp, &hum, &press)) {
      lastBmeTemp  = temp;
      lastBmeHum   = hum;
      lastBmePress = press;
      lastBmeOk    = true;
      write_bme_row(now, temp, hum, press);
    } else if (now - lastBmeErrorMs >= 1000) {
      lastBmeOk = false;
      lastBmeErrorMs = now;
      log_system_event("BME_ERROR", "read failed");
    }
  }

  GpsFix fix;
  sensor.gps_get_latest(&fix);
  if (fix.fix_millis != 0 && fix.fix_millis != lastGpsLoggedMillis) {
    write_gps_row(now, fix);
    lastGpsLoggedMillis = fix.fix_millis;
  }

  if (now - lastStatusMs >= BME_INTERVAL_MS) {
    lastStatusMs = now;
    print_status(now, fix);
  }
}
