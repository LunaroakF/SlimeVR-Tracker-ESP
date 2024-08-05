/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2021 Eiren Rain & SlimeVR contributors

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include "sensors/bno080sensor.h"
#include "utils.h"
#include "GlobalVars.h"

void BNO080Sensor::motionSetup()
{
#ifdef DEBUG_SENSOR
    imu.enableDebugging(Serial);
#endif
    if(!imu.begin(addr, Wire, m_IntPin)) {
        m_Logger.fatal("Can't connect to %s at address 0x%02x", getIMUNameByType(sensorType), addr);
        ledManager.pattern(50, 50, 200);
        return;
    }

    m_Logger.info("Connected to %s on 0x%02x. "
                  "Info: SW Version Major: 0x%02x "
                  "SW Version Minor: 0x%02x "
                  "SW Part Number: 0x%02x "
                  "SW Build Number: 0x%02x "
                  "SW Version Patch: 0x%02x",
                  getIMUNameByType(sensorType),
                  addr,
                  imu.swMajor,
                  imu.swMinor,
                  imu.swPartNumber,
                  imu.swBuildNumber,
                  imu.swVersionPatch
                );

    this->imu.enableLinearAccelerometer(10);

#if USE_6_AXIS
    if ((sensorType == ImuID::BNO085 || sensorType == ImuID::BNO086) && BNO_USE_ARVR_STABILIZATION) {
        imu.enableARVRStabilizedGameRotationVector(10);
    } else {
        imu.enableGameRotationVector(10);
    }

    #if BNO_USE_MAGNETOMETER_CORRECTION
    imu.enableRotationVector(1000);
    #endif
#else
    if ((sensorType == ImuID::BNO085 || sensorType == ImuID::BNO086) && BNO_USE_ARVR_STABILIZATION) {
        imu.enableARVRStabilizedRotationVector(10);
    } else {
        imu.enableRotationVector(10);
    }
#endif

#if ENABLE_INSPECTION
    imu.enableRawGyro(10);
    imu.enableRawAccelerometer(10);
    imu.enableRawMagnetometer(10);
#endif
	// Calibration settings:
	// EXPERIMENTAL Enable periodic calibration save to permanent memory
	imu.saveCalibrationPeriodically(true);
	imu.requestCalibrationStatus();
	// EXPERIMENTAL Disable accelerometer calibration after 1 minute to prevent "stomping" bug
	if(sensorType == ImuID::BNO085) {
		// For BNO085, disable accel calibration
		globalTimer.in(60000, [](void *sensor) {((BNO080*) sensor)->sendCalibrateCommand(SH2_CAL_MAG | SH2_CAL_ON_TABLE); return true;}, &imu);
	} else if(sensorType == ImuID::BNO085) {
		// For BNO086, disable accel calibration
		// TODO: Find default flags for BNO086
		globalTimer.in(60000, [](void *sensor) {((BNO080*) sensor)->sendCalibrateCommand(SH2_CAL_MAG | SH2_CAL_ON_TABLE); return true;}, &imu);
	} else {
		globalTimer.in(60000, [](void *sensor) {((BNO080*) sensor)->requestCalibrationStatus(); return true;}, &imu);
	}
	//imu.sendCalibrateCommand(SH2_CAL_ACCEL | SH2_CAL_GYRO_IN_HAND | SH2_CAL_MAG | SH2_CAL_ON_TABLE | SH2_CAL_PLANAR);


    lastReset = 0;
    lastData = millis();
    working = true;
    configured = true;
}

void BNO080Sensor::motionLoop()
{
    //Look for reports from the IMU
    while (imu.dataAvailable())
    {
        hadData = true;
#if ENABLE_INSPECTION
        {
            int16_t rX = imu.getRawGyroX();
            int16_t rY = imu.getRawGyroY();
            int16_t rZ = imu.getRawGyroZ();
            uint8_t rA = imu.getGyroAccuracy();

            int16_t aX = imu.getRawAccelX();
            int16_t aY = imu.getRawAccelY();
            int16_t aZ = imu.getRawAccelZ();
            uint8_t aA = imu.getAccelAccuracy();

            int16_t mX = imu.getRawMagX();
            int16_t mY = imu.getRawMagY();
            int16_t mZ = imu.getRawMagZ();
            uint8_t mA = imu.getMagAccuracy();

            networkConnection.sendInspectionRawIMUData(sensorId, rX, rY, rZ, rA, aX, aY, aZ, aA, mX, mY, mZ, mA);
        }
#endif

        lastReset = 0;
        lastData = millis();

#if USE_6_AXIS
        if (imu.hasNewGameQuat()) // New quaternion if context
        {
            Quat nRotation;
            imu.getGameQuat(nRotation.x, nRotation.y, nRotation.z, nRotation.w, calibrationAccuracy);

            setFusedRotation(nRotation);
            // Leave new quaternion if context open, it's closed later

#else // USE_6_AXIS

        if (imu.hasNewQuat()) // New quaternion if context
        {
            Quat nRotation;
            imu.getQuat(nRotation.x, nRotation.y, nRotation.z, nRotation.w, magneticAccuracyEstimate, calibrationAccuracy);

            setFusedRotation(nRotation);
            // Leave new quaternion if context open, it's closed later
#endif // USE_6_AXIS

            // Continuation of the new quaternion if context, used for both 6 and 9 axis
#if SEND_ACCELERATION
            {
                uint8_t acc;
                Vector3 nAccel;
                imu.getLinAccel(nAccel.x, nAccel.y, nAccel.z, acc);
                setAcceleration(nAccel);
            }
#endif // SEND_ACCELERATION
        } // Closing new quaternion if context

#if USE_6_AXIS && BNO_USE_MAGNETOMETER_CORRECTION
        if (imu.hasNewMagQuat())
        {
            imu.getMagQuat(magQuaternion.x, magQuaternion.y, magQuaternion.z, magQuaternion.w, magneticAccuracyEstimate, magCalibrationAccuracy);
            magQuaternion *= sensorOffset;

    #if ENABLE_INSPECTION
            {
                networkConnection.sendInspectionCorrectionData(sensorId, quaternion);
            }
    #endif // ENABLE_INSPECTION

            newMagData = true;
        }
#endif // USE_6_AXIS && BNO_USE_MAGNETOMETER_CORRECTION

        if (imu.getTapDetected())
        {
            tap = imu.getTapDetector();
        }
        if (m_IntPin == 255 || imu.I2CTimedOut())
            break;
    }
	if(imu.hasNewCliabrationStatus()) {
		uint8_t calibrationResponseStatus;
		uint8_t accelCalEnabled;
		uint8_t gyroCalEnabled;
		uint8_t magCalEnabled;
		uint8_t planarAccelCalEnabled;
		uint8_t onTableCalEnabled;
		imu.getCalibrationStatus(calibrationResponseStatus, accelCalEnabled, gyroCalEnabled, magCalEnabled, planarAccelCalEnabled, onTableCalEnabled);
		m_Logger.info("BNO08X calibration satus received: Status: %d, Accel: %d, Gyro: %d, Mag: %d, Planar: %d, OnTable: %d",
                calibrationResponseStatus, accelCalEnabled, gyroCalEnabled, magCalEnabled, planarAccelCalEnabled, onTableCalEnabled);
		// Default calibration flags for BNO085:
		// Accel: 1, Gyro: 0, Mag: 1, Planar: 0, OnTable: 0 (OnTable can't be disabled)
	}
    if (lastData + 1000 < millis() && configured)
    {
        while(true) {
            BNO080Error error = imu.readError();
            if(error.error_source == 255)
                break;
            lastError = error;
            m_Logger.error("BNO08X error. Severity: %d, seq: %d, src: %d, err: %d, mod: %d, code: %d",
                error.severity, error.error_sequence_number, error.error_source, error.error, error.error_module, error.error_code);
        }
        statusManager.setStatus(SlimeVR::Status::IMU_ERROR, true);
        working = false;
        lastData = millis();
        uint8_t rr = imu.resetReason();
        if (rr != lastReset)
        {
            lastReset = rr;
            networkConnection.sendSensorError(this->sensorId, rr);
        }

        m_Logger.error("Sensor %d doesn't respond. Last reset reason:", sensorId, lastReset);
        m_Logger.error("Last error: %d, seq: %d, src: %d, err: %d, mod: %d, code: %d",
                lastError.severity, lastError.error_sequence_number, lastError.error_source, lastError.error, lastError.error_module, lastError.error_code);
    }
}

SensorStatus BNO080Sensor::getSensorState() {
    return lastReset > 0 ? SensorStatus::SENSOR_ERROR : isWorking() ? SensorStatus::SENSOR_OK : SensorStatus::SENSOR_OFFLINE;
}

void BNO080Sensor::sendData()
{
    if (newFusedRotation)
    {
        newFusedRotation = false;
        networkConnection.sendRotationData(sensorId, &fusedRotation, DATA_TYPE_NORMAL, calibrationAccuracy);

#ifdef DEBUG_SENSOR
        m_Logger.trace("Quaternion: %f, %f, %f, %f", UNPACK_QUATERNION(fusedRotation));
#endif

#if SEND_ACCELERATION
        if (newAcceleration)
        {
            newAcceleration = false;
            networkConnection.sendSensorAcceleration(this->sensorId, this->acceleration);
        }
#endif
    }

#if !USE_6_AXIS
        networkConnection.sendMagnetometerAccuracy(sensorId, magneticAccuracyEstimate);
#endif

#if USE_6_AXIS && BNO_USE_MAGNETOMETER_CORRECTION
    if (newMagData)
    {
        newMagData = false;
        networkConnection.sendRotationData(sensorId, &magQuaternion, DATA_TYPE_CORRECTION, magCalibrationAccuracy);
        networkConnection.sendMagnetometerAccuracy(sensorId, magneticAccuracyEstimate);
    }
#endif

    if (tap != 0)
    {
        networkConnection.sendSensorTap(sensorId, tap);
        tap = 0;
    }
}

void BNO080Sensor::startCalibration(int calibrationType)
{
    // BNO does automatic calibration,
    // it's always enabled except accelerometer
    // that is disabled 30 seconds after startup
}
