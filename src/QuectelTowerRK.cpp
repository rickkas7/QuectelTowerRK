/*
 * Copyright (c) 2020 Particle Industries, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "QuectelTowerRK.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif


QuectelTowerRK *QuectelTowerRK::_instance = nullptr;

QuectelTowerRK::QuectelTowerRK() : _signal_update(0), _thread(nullptr)
{
    os_queue_create(&_commandQueue, sizeof(CommandCode), 1, nullptr);
    _thread = new Thread("tracker_cellular", [this]() {QuectelTowerRK::thread_f();}, OS_THREAD_PRIORITY_DEFAULT);
}

int QuectelTowerRK::scanBlocking(TowerInfo &towerInfo) {
    Mutex blockingMutex;
    blockingMutex.lock();

    int ret = scanWithCallback([&blockingMutex, &towerInfo](TowerInfo tempTowerInfo) {
        blockingMutex.unlock();
        towerInfo = tempTowerInfo;
    });

    if (ret == SYSTEM_ERROR_NONE) {
        blockingMutex.lock();
    }
    return ret;
}

int QuectelTowerRK::scanWithCallback(std::function<void(TowerInfo towerInfo)> scanCallback) {
    this->scanCallback = scanCallback;
    int ret = startScan();

    if (ret != SYSTEM_ERROR_NONE) {
        this->scanCallback = nullptr;
    }
    return ret;
}


int QuectelTowerRK::startScan() {
    auto event = CommandCode::Measure;
    CHECK_FALSE(os_queue_put(_commandQueue, &event, 0, nullptr), SYSTEM_ERROR_BUSY);

    return SYSTEM_ERROR_NONE;
}


int QuectelTowerRK::serving_cb(int type, const char* buf, int len, QuectelTowerRK* context) {
    if (type == TYPE_OK) {
        return RESP_OK;
    }

    context->receivedTowerInfo.parseServing(buf);
    return WAIT;
}

int QuectelTowerRK::neighbor_cb(int type, const char* buf, int len, QuectelTowerRK* context) {
    if (type == TYPE_OK) {
        return RESP_OK;
    }

    context->receivedTowerInfo.parseNeighbor(buf);

    return WAIT;
}


QuectelTowerRK::CommandCode QuectelTowerRK::waitOnEvent(system_tick_t timeout) {
    CommandCode event {CommandCode::None};
    auto ret = os_queue_take(_commandQueue, &event, timeout, nullptr);
    if (ret) {
        event = CommandCode::None;
    }

    return event;
}

// a thread to capture cellular signal strength in a non-blocking fashion
void QuectelTowerRK::thread_f()
{
    auto loop = true;
    while (loop) {
        // Look for requests and provide a loop delay
        auto event = waitOnEvent(PERIOD_SUCCESS_MS);

        if (Cellular.ready()) {
            // Grab the cellular strength on every loop iteration
            auto rssi = Cellular.RSSI();

            if (rssi.getStrengthValue() < 0) {
                auto uptime = System.uptime();
                WITH_LOCK(mutex) {
                    _signal = rssi;
                    _signal_update = uptime;
                }
            } else {
                _signal_update = 0;
            }
        }

        switch (event) {
            case CommandCode::None:
                // Do nothing
                break;

            case CommandCode::Exit:
                // Get out of main loop and join
                loop = false;
                break;

            case CommandCode::Measure: {
                // Access to this data will always be requested in advance.  We just need
                // to take inventory of what has been collected and data from the operation.

                if (!Cellular.ready()) {
                    WITH_LOCK(mutex) {
                        savedTowerInfo.clear();
                    }
                    // The cellular modem is not even ready (maybe not powered) so leave
                    break;
                }

                WITH_LOCK(mutex) {
                    receivedTowerInfo.clear();
                }

                Cellular.command(serving_cb, this, 10000, "AT+QENG=\"servingcell\"\r\n");
                Cellular.command(neighbor_cb, this, 10000, "AT+QENG=\"neighbourcell\"\r\n");

                WITH_LOCK(mutex) {
                    savedTowerInfo = receivedTowerInfo;
                }
                if (scanCallback) {
                    scanCallback(savedTowerInfo);
                }
                break;
            }

            default:
                break;
        }
    }

    // Kill the thread if we get here
    _thread->cancel();
}

int QuectelTowerRK::getSignal(CellularSignal &signal, unsigned int max_age)
{
    const std::lock_guard<RecursiveMutex> lg(mutex);

    if(!_signal_update || System.uptime() - _signal_update > max_age)
    {
        return -ENODATA;
    }

    signal = _signal;
    return 0;
}

unsigned int QuectelTowerRK::getSignalUpdate()
{
    return _signal_update;
}

// [static] 
QuectelTowerRK::RadioAccessTechnology QuectelTowerRK::parseRadioAccessTechnology(const char *str) {
    RadioAccessTechnology rat = RadioAccessTechnology::NONE;

    if (!strncmp(str, "CAT-M", 5) || !strncmp(str, "eMTC", 4)) {
        rat = RadioAccessTechnology::LTE_CAT_M1;
    }
    else if (!strncmp(str, "LTE", 3)) {
        rat = RadioAccessTechnology::LTE;
    }
    else if (!strncmp(str, "CAT-NB", 6)) {
        rat = RadioAccessTechnology::LTE_NB_IOT;
    }

    return rat;
}


int QuectelTowerRK::CellularServing::parse(const char *in) {
    char stateStr[16] = {};
    char ratStr[16] = {};

    clear();

    auto nitems = sscanf(in, " +QENG: \"servingcell\",\"%15[^\"]\",\"%15[^\"]\",\"%*15[^\"]\","
            "%u,%u,%lX,"
            "%*15[^,],%*15[^,],%*15[^,],%*15[^,],%*15[^,],%X,%d",
            stateStr, ratStr,
            &mcc, &mnc, &cellId, &tac, &signalPower);

    if (nitems < 7) {
        return SYSTEM_ERROR_NOT_ENOUGH_DATA;
    }

    rat = parseRadioAccessTechnology(ratStr);
    if (rat == RadioAccessTechnology::NONE) {
        return SYSTEM_ERROR_NOT_SUPPORTED;
    }

    return SYSTEM_ERROR_NONE;
}

QuectelTowerRK::CellularServing &QuectelTowerRK::CellularServing::toJsonWriter(JSONWriter &writer, bool wrapInObject) {

    if (wrapInObject) {
        writer.beginObject();
    }

    writer.name("rat").value("lte");
    writer.name("mcc").value((unsigned)mcc);
    writer.name("mnc").value((unsigned)mnc);
    writer.name("lac").value((unsigned)tac);
    writer.name("cid").value((unsigned)cellId);
    writer.name("str").value(signalPower);

    if (wrapInObject) {
        writer.endObject();
    }

    return *this;
}

void QuectelTowerRK::CellularServing::clear() {
    rat = RadioAccessTechnology::NONE;
    mcc = 0;
    mnc = 0;
    cellId = 0;
    tac = 0;
    signalPower = 0;
}


int QuectelTowerRK::CellularNeighbor::parse(const char *in) {
    char ratStr[16] = {0};

    clear();

    auto nitems = sscanf(in, " +QENG: \"neighbourcell %*15[^\"]\",\"%15[^\"]\",%lu,%lu,%d,%d,%d",
            ratStr,
            &earfcn, &neighborId, &signalQuality, &signalPower, &signalStrength);

    if (nitems < 6) {
        return SYSTEM_ERROR_NOT_ENOUGH_DATA;
    }

    rat = parseRadioAccessTechnology(ratStr);
    if (rat == RadioAccessTechnology::NONE) {
        return SYSTEM_ERROR_NOT_SUPPORTED;
    }

    return SYSTEM_ERROR_NONE;
}


QuectelTowerRK::CellularNeighbor &QuectelTowerRK::CellularNeighbor::toJsonWriter(JSONWriter &writer, bool wrapInObject) {

    if (wrapInObject) {
        writer.beginObject();
    }

    writer.name("nid").value((unsigned)neighborId);
    writer.name("ch").value((unsigned)earfcn);
    writer.name("str").value(signalPower);

    if (wrapInObject) {
        writer.endObject();
    }

    return *this;
}

void QuectelTowerRK::CellularNeighbor::clear() {
    rat = RadioAccessTechnology::NONE;
    earfcn = 0;
    neighborId = 0;
    signalQuality = 0;
    signalPower = 0;
    signalStrength = 0;
}

QuectelTowerRK::TowerInfo::TowerInfo() {

}
QuectelTowerRK::TowerInfo::~TowerInfo() {

}

QuectelTowerRK::TowerInfo::TowerInfo(const QuectelTowerRK::TowerInfo &other) {
    *this = other;
}

QuectelTowerRK::TowerInfo &QuectelTowerRK::TowerInfo::operator=(const QuectelTowerRK::TowerInfo &other) {
    serving = other.serving;

    neighbors.clear();  
    for(auto it = other.neighbors.begin(); it != other.neighbors.end(); ++it) {
        neighbors.push_back(*it);
    }

    return *this;
}

int QuectelTowerRK::TowerInfo::parseServing(const char *in) {
    return serving.parse(in);
}

int QuectelTowerRK::TowerInfo::parseNeighbor(const char *in) {
    CellularNeighbor neighbor;
    int ret = neighbor.parse(in);
    if (ret == SYSTEM_ERROR_NONE) {
        neighbors.push_back(neighbor);
    }
    return ret;
}


void QuectelTowerRK::TowerInfo::clear() {
    serving.clear();
    neighbors.clear();
}
