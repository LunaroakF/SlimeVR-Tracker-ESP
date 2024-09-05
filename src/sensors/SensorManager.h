/*
	SlimeVR Code is placed under the MIT license
	Copyright (c) 2022 TheDevMinerTV

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

#ifndef SLIMEVR_SENSORMANAGER
#define SLIMEVR_SENSORMANAGER

#include <i2cscan.h>
#include "globals.h"
#include "sensor.h"
#include "EmptySensor.h"
#include "ErroneousSensor.h"
#include "logging/Logger.h"
#include "sensorinterface/I2CWireSensorInterface.h"
#include "sensorinterface/DirectPinInterface.h"

#include <memory>

#include "EmptySensor.h"
#include "ErroneousSensor.h"
#include "globals.h"
#include "logging/Logger.h"
#include "sensor.h"

namespace SlimeVR {
namespace Sensors {
class SensorManager {
public:
	SensorManager()
		: m_Logger(SlimeVR::Logging::Logger("SensorManager")) {}
	void setup();
	void postSetup();

	void update();

	std::vector<std::unique_ptr<Sensor>>& getSensors() { return m_Sensors; };
	ImuID getSensorType(size_t id) {
		if (id < m_Sensors.size()) {
			return m_Sensors[id]->getSensorType();
		}
		return ImuID::Unknown;
	}

private:
	SlimeVR::Logging::Logger m_Logger;

	std::vector<std::unique_ptr<Sensor>> m_Sensors;

	template <typename ImuType>
	std::unique_ptr<Sensor> buildSensor(
		uint8_t sensorID, uint8_t addrSuppl, float rotation, SensorInterface* sensorInterface,
		bool optional = false, PinInterface* intPin = nullptr, int extraParam = 0)
	{
		const uint8_t address = ImuType::Address + addrSuppl;
		m_Logger.trace("Building IMU with: id=%d,\n\
						address=0x%02X, rotation=%f,\n\
						interface=%s, int=%s, extraParam=%d, optional=%d",
						sensorID, address, rotation,
						sensorInterface, intPin, extraParam, optional);

		// Now start detecting and building the IMU
		std::unique_ptr<Sensor> sensor;

		// Clear and reset I2C bus for each sensor upon startup
		sensorInterface->init();
		sensorInterface->swapIn();

		if (I2CSCAN::hasDevOnBus(address)) {
			m_Logger.trace("Sensor %d found at address 0x%02X", sensorID + 1, address);
		} else {
			if (!optional) {
				m_Logger.error(
					"Mandatory sensor %d not found at address 0x%02X",
					sensorID + 1,
					address
				);
				sensor = std::make_unique<ErroneousSensor>(sensorID, ImuType::TypeID);
			} else {
				m_Logger.debug(
					"Optional sensor %d not found at address 0x%02X",
					sensorID + 1,
					address
				);
				sensor = std::make_unique<EmptySensor>(sensorID);
			}
			return sensor;
		}

        sensor = std::make_unique<ImuType>(sensorID, addrSuppl, rotation, sensorInterface, intPin, extraParam);

		sensor->motionSetup();
		return sensor;
	}

	uint32_t m_LastBundleSentAtMicros = micros();
};
}  // namespace Sensors
}  // namespace SlimeVR

#endif  // SLIMEVR_SENSORFACTORY_H_
