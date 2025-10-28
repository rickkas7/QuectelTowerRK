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

static Logger _log("app.tower");

QuectelTowerRK *QuectelTowerRK::_instance = nullptr;

QuectelTowerRK::QuectelTowerRK() : cellularSignalLastUpdate(0), thread(nullptr)
{
    os_queue_create(&commandQueue, sizeof(CommandCode), 1, nullptr);
    thread = new Thread("tracker_cellular", [this]() {QuectelTowerRK::threadFunction();}, OS_THREAD_PRIORITY_DEFAULT);
}

QuectelTowerRK::~QuectelTowerRK() {

}

int QuectelTowerRK::scanBlocking(TowerInfo &towerInfo, unsigned long timeoutMs) {
    bool done = false;

    unsigned long startMs = millis();

    int ret = scanWithCallback([&done, &towerInfo](TowerInfo tempTowerInfo) {
        done = true;
        towerInfo = tempTowerInfo;
    });

    if (ret == SYSTEM_ERROR_NONE) {
        while(!done) {
            if ((timeoutMs != 0) && (millis() - startMs >= timeoutMs)) {
                cancelScan();
                ret = SYSTEM_ERROR_TIMEOUT;
                break;
            }
            delay(1);
        }
    }
    return ret;
}

int QuectelTowerRK::scanWithCallback(std::function<void(TowerInfo towerInfo)> scanCallback) {
    int ret = startScan();
    if (ret == SYSTEM_ERROR_NONE) {
        this->scanCallback = scanCallback;
    }
    return ret;
}


int QuectelTowerRK::startScan() {
    auto event = CommandCode::Measure;
    CHECK_FALSE(os_queue_put(commandQueue, &event, 0, nullptr), SYSTEM_ERROR_BUSY);

    return SYSTEM_ERROR_NONE;
}

void QuectelTowerRK::cancelScan() {
    scanCallback = nullptr;
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
    auto ret = os_queue_take(commandQueue, &event, timeout, nullptr);
    if (ret) {
        event = CommandCode::None;
    }

    return event;
}

// a thread to capture cellular signal strength in a non-blocking fashion
void QuectelTowerRK::threadFunction()
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
                    cellularSignal = rssi;
                    cellularSignalLastUpdate = uptime;
                }
            } else {
                cellularSignalLastUpdate = 0;
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
    thread->cancel();
}

int QuectelTowerRK::getSignal(CellularSignal &signal, unsigned int max_age)
{
    const std::lock_guard<RecursiveMutex> lg(mutex);

    if(!cellularSignalLastUpdate || System.uptime() - cellularSignalLastUpdate > max_age)
    {
        return -ENODATA;
    }

    signal = cellularSignal;
    return 0;
}

unsigned int QuectelTowerRK::getSignalUpdate()
{
    return cellularSignalLastUpdate;
}

void QuectelTowerRK::getTowerInfo(TowerInfo &towerInfo) {
    WITH_LOCK(mutex) {
        towerInfo = savedTowerInfo;
    }
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
            &mcc, &mnc, &cellId, &lac, &signalPower);

    if (nitems < 7) {
        return SYSTEM_ERROR_NOT_ENOUGH_DATA;
    }

    rat = parseRadioAccessTechnology(ratStr);
    if (rat == RadioAccessTechnology::NONE) {
        return SYSTEM_ERROR_NOT_SUPPORTED;
    }

    return SYSTEM_ERROR_NONE;
}

String QuectelTowerRK::CellularServing::toString() const {
    return String::format("rat=%d, mcc=%d, mnc=%d, lac=%d, cid=%d, str=%d", 
        (int)rat, (int)mcc, (int)mnc, (int)lac, (int)cellId, (int)signalPower);
}


QuectelTowerRK::CellularServing &QuectelTowerRK::CellularServing::toJsonWriter(JSONWriter &writer, bool wrapInObject) {

    if (wrapInObject) {
        writer.beginObject();
    }

    writer.name("rat").value("lte");
    writer.name("mcc").value((unsigned)mcc);
    writer.name("mnc").value((unsigned)mnc);
    writer.name("lac").value((unsigned)lac);
    writer.name("cid").value((unsigned)cellId);
    writer.name("str").value(signalPower);

    if (wrapInObject) {
        writer.endObject();
    }

    return *this;
}

#ifdef SYSTEM_VERSION_v620
QuectelTowerRK::CellularServing &QuectelTowerRK::CellularServing::toVariant(Variant &obj) {

    obj.set("rat", Variant("lte"));
    obj.set("mcc", Variant((unsigned)mcc));
    obj.set("mnc", Variant((unsigned)mnc));
    obj.set("lac", Variant((unsigned)lac));
    obj.set("cid", Variant((unsigned)cellId));
    obj.set("str", Variant(signalPower));

    return *this;
}
#endif // SYSTEM_VERSION_v620


void QuectelTowerRK::CellularServing::clear() {
    rat = RadioAccessTechnology::NONE;
    mcc = 0;
    mnc = 0;
    cellId = 0;
    lac = 0;
    signalPower = 0;
}
bool QuectelTowerRK::CellularServing::isValid() const {
    return rat != RadioAccessTechnology::NONE;
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

String QuectelTowerRK::CellularNeighbor::toString() const {
    return String::format("nid=%d, ch=%d, str=%d", (int)neighborId, (int)earfcn, (int)signalPower);    
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


#ifdef SYSTEM_VERSION_v620
QuectelTowerRK::CellularNeighbor &QuectelTowerRK::CellularNeighbor::toVariant(Variant &obj) {

    obj.set("nid", Variant((unsigned)neighborId));
    obj.set("ch", Variant((unsigned)earfcn));
    obj.set("str", Variant(signalPower));

    return *this;
}
#endif // SYSTEM_VERSION_v620


void QuectelTowerRK::CellularNeighbor::clear() {
    rat = RadioAccessTechnology::NONE;
    earfcn = 0;
    neighborId = 0;
    signalQuality = 0;
    signalPower = 0;
    signalStrength = 0;
}

bool QuectelTowerRK::CellularNeighbor::isValid() const {
    return rat != RadioAccessTechnology::NONE;
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

void QuectelTowerRK::TowerInfo::log(const char *msg, LogLevel level) {
    _log.log(level, "%s: serving %s", msg, serving.toString().c_str());
    for(auto it = neighbors.begin(); it != neighbors.end(); ++it) {
        _log.log(level, " neighbor %s", (*it).toString().c_str());
    }
}




void QuectelTowerRK::TowerInfo::clear() {
    serving.clear();
    neighbors.clear();
}

QuectelTowerRK::TowerInfo &QuectelTowerRK::TowerInfo::toJsonWriter(JSONWriter &writer, int numToInclude) {
    int numAdded = 0;

    writer.beginArray();
    if (serving.rat != RadioAccessTechnology::NONE) {
        serving.toJsonWriter(writer);
        numAdded++;
    }
    for(auto it = neighbors.begin(); it != neighbors.end(); ++it) {
        if (numToInclude != 0 && numAdded >= numToInclude) {
            break;
        }
        (*it).toJsonWriter(writer);
        numAdded++;
    }

    writer.endArray();

    return *this;
}

#ifdef SYSTEM_VERSION_v620
QuectelTowerRK::TowerInfo &QuectelTowerRK::TowerInfo::toVariant(Variant &obj, int numToInclude) {
    int numAdded = 0;

    if (serving.rat != RadioAccessTechnology::NONE) {
        Variant obj2;
        serving.toVariant(obj2);
        obj.append(obj2);
        numAdded++;
    }
    for(auto it = neighbors.begin(); it != neighbors.end(); ++it) {
        if (numToInclude != 0 && numAdded >= numToInclude) {
            break;
        }
        Variant obj2;
        (*it).toVariant(obj2);
        obj.append(obj2);
        numAdded++;
    }

    return *this;
}
#endif // SYSTEM_VERSION_v620


bool QuectelTowerRK::TowerInfo::isValid() const {
    return serving.isValid();
}
