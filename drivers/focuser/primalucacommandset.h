/*
    Primaluca Labs Essato-Arco-Sesto Command Set
    For USB Control Specification Document Revision 3.3 published 2020.07.08

    Copyright (C) 2022 Jasem Mutlaq

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

    JM 2022.07.16: Major refactor to using json.h and update to Essato Arco
    Document protocol revision 3.3 (8th July 2022).
*/

#pragma once

#include <cstdint>
#include "json.h"

using json = nlohmann::json;

namespace PrimalucaLabs
{
typedef struct MotorRates
{
    // Rate values: 1-10
    uint32_t accRate = 0, runSpeed = 0, decRate = 0;
} MotorRates;

typedef struct MotorCurrents
{
    // Current values: 1-10
    uint32_t accCurrent = 0, runCurrent = 0, decCurrent = 0;
    // Hold current: 1-5
    uint32_t holdCurrent = 0;
} MotorCurrents;

typedef enum
{
    MOT_1,
    MOT_2,
    MOT_NONE
} MotorType;

typedef enum
{
    UNIT_STEPS,
    UNIT_DEGREES,
    UNIT_ARCSECS
} Units;

/*****************************************************************************************
 * Communicaton class handle the serial communication with SestoSenso2/Esatto/Arco
 * models according to the USB Protocol specifications document in JSON.
******************************************************************************************/
class Communication
{
    public:

        explicit Communication(const std::string &name, int port) : m_DeviceName(name), m_PortFD(port) {}
        const char *getDeviceName()
        {
            return m_DeviceName.c_str();
        }

        // Communication functions
        bool sendRequest(const json &command, json *response = nullptr);
        template <typename T = int32_t> bool genericRequest(const std::string &motor, const std::string &type, const json &command, T *response = nullptr);
        /**
         * @brief Get paramter from device.
         * @param type motor type, if MOT_NONE, then it's a generic device-wide get.
         * @param paramter paramter name (e.g.SN)
         * @param value where to store the queried value.
         * @return True if successful, false otherwise.
         * @example {"req":{"get":{"SN":""}}} --> {"res":{"get":{"SN":" ESATTO30001"}}
         * @example {"req":{"get": {"MOT1" :{"BKLASH": ""}}} --> {"res":{"get": {"MOT1" :{"BKLASH": 120}}}
         */
        template <typename T = int32_t> bool get(MotorType type, const std::string &parameter, T &value);

        /**
         * @brief getStringAsDouble Same as get, but it receives a string and scan it for double.
         * @param type motor type
         * @param parameter parameter name
         * @param value value to store the scanned double value.
         * @return True if successful, false otherwise.
         */
        bool getStringAsDouble(MotorType type, const std::string &parameter, double &value);

        /**
         * @brief Set JSON value
         * @param value json value to set
         * @return True if successful, false otherwise.
         * @note This is a generic set and not related to any motor.
         * @example {"req":{"set": {"ARCO": 1}}} --> {"res":{"set": {"ARCO": "done"}}}
         * @example {"req":{"set": {"MOT1" :{"BKLASH": 120}}} --> {"res":{"set": {"MOT1" :{"BKLASH": "done"}}}
         */
        bool set(MotorType type, const json &value);

        /**
         * @brief Execute a motor command.
         * @param command json command.
         * @return True if successful, false otherwise.
         * @example {"req":{"cmd":{"MOT1" :{"MOT_STOP":""}}}} --> {"res":{"cmd":{"MOT1" :{"MOT_STOP":"done"}}}}
         */
        template <typename T = int32_t> bool command(MotorType type, const json &jsonCommand);

    private:
        std::string m_DeviceName;
        int m_PortFD {-1};

        // Maximum buffer for sending/receving.
        static constexpr const int DRIVER_LEN {1024};
        static const char DRIVER_STOP_CHAR { 0xD };
        static const char DRIVER_TIMEOUT { 5 };
};

/*****************************************************************************************
 * Focuser class represent the common functionality between SestoSenso2 and Esatto models.
******************************************************************************************/
class Focuser
{
    public:
        Focuser(const std::string &name, int port);

        // Position
        bool getMaxPosition(uint32_t &position);
        bool isHallSensorDetected(bool &isDetected);
        bool getAbsolutePosition(uint32_t &position);

        // Motion
        bool getStatus(json &status);
        bool goAbsolutePosition(uint32_t position);
        bool stop();
        bool fastMoveOut();
        bool fastMoveIn();
        bool getCurrentSpeed(uint32_t &speed);
        bool isBusy();

        // Firmware
        bool getSerialNumber(std::string &response);
        bool getFirmwareVersion(std::string &response);

        // Sensors
        bool getMotorTemp(double &value);
        bool getExternalTemp(double &value);
        bool getVoltage12v(double &value);

    protected:
        std::unique_ptr<Communication> m_Communication;
};

/*****************************************************************************************
 * SestoSenso2 class
 * Adds presets and motor rates/current control in addition to calibration.
******************************************************************************************/
class SestoSenso2 : public Focuser
{
    public:
        SestoSenso2(const std::string &name, int port);

        // Presets
        bool applyMotorPreset(const std::string &name);
        bool setMotorUserPreset(uint32_t index, const MotorRates &rates, const MotorCurrents &currents);

        // Motor Settings
        bool getMotorSettings(struct MotorRates &rates, struct MotorCurrents &currents, bool &motorHoldActive);
        bool setMotorRates(const MotorRates &rates);
        bool setMotorCurrents(const MotorCurrents &currents);
        bool setMotorHold(bool hold);

        // Calibration
        bool initCalibration();
        bool storeAsMaxPosition();
        bool storeAsMinPosition();
        bool goOutToFindMaxPos();
};

/*****************************************************************************************
 * Esatto class
 * Just adds backlash support.
******************************************************************************************/
class Esatto : public Focuser
{
    public:
        Esatto(const std::string &name, int port);

        // Backlash
        bool setBacklash(uint32_t steps);
        bool getBacklash(uint32_t &steps);

        // Sensors
        bool getVoltageUSB(double &value);
};

/*****************************************************************************************
 * Arco class
 * GOTO/Sync/Stop and reverse support.
******************************************************************************************/
class Arco
{

    public:
        explicit Arco(const std::string &name, int port);

        // Motor Info
        bool getMotorInfo(json &info);
        // Is it detected and enabled?
        bool setEnabled(bool enabled);
        bool isEnabled();

        // Motion
        bool isBusy();
        bool getStatus(json &status);
        bool moveAbsolutePoition(Units unit, double value);
        bool stop();

        // Position
        bool getAbsolutePosition(Units unit, double &value);
        bool sync(Units unit, double value);

        // Calibration
        bool calibrate();
        bool isCalibrating();

        // Reverse
        bool reverse(bool enabled);
        bool isReversed();

        // Firmware
        bool getSerialNumber(std::string &response);
        bool getFirmwareVersion(std::string &response);

    private:
        std::unique_ptr<Communication> m_Communication;
};

}
