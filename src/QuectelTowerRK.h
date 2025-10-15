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

/*
 * Repository: https://github.com/rickkas7/QuectelTowerRK
 * License: Apache 2.0
 * 
 * This code is a modified version of tracker_cellular.cpp and tracker_cellular.h files out of the 
 * tracker_edge repository: https://github.com/particle-iot/tracker-edge
 */

#pragma once

#include "Particle.h"

#include <vector>

/**
 * @brief Class to grab cellular modem and tower information from Quectel cellular modems on Particle devices
 *
 * This includes:
 * - Tracker (Tracker One, Monitor One, Tracker SoM)
 * - M-SoM (M-SoM, Muon)
 * - b5som variants of B-SoM (B504e, B524, B523)
 */
class QuectelTowerRK {
public:
    // delay between checking cell strength when no errors detected
    static constexpr system_tick_t PERIOD_SUCCESS_MS {1000};

    // delay between checking cell strength when errors detected
    // longer than success to minimize thrashing on the cell interface which could
    // delay recovery in Device-OS
    static constexpr system_tick_t PERIOD_ERROR_MS {10000};

    // cell updates need to be at least this often or flagged as an error
    static constexpr unsigned int DEFAULT_MAX_AGE_SEC {10};

    // Only have enough space for so many neighbor towers
    static constexpr size_t  MAX_NEIGHBORS {4};

    // Maximum amount of time, in milliseconds, that a tower scan should take
    static constexpr system_tick_t SCAN_DELAY {500 + 500};

    /**
     * @brief Commands to instruct cellular thread
     *
     */
    enum class CommandCode {
        None,                   /**< Do nothing */
        Measure,                /**< Perform cellular scan */
        Exit,                   /**< Exit from thread */
    };

    /**
     * @brief Type of radio used in modem to tower communications
     *
     */
    enum class RadioAccessTechnology {
        NONE = -1,
        LTE = 7,
        LTE_CAT_M1 = 8,
        LTE_NB_IOT = 9
    };

    /**
     * @brief Information identifying the serving tower
     *
     */
    class CellularServing {
    public:
        RadioAccessTechnology rat {RadioAccessTechnology::NONE};
        unsigned int mcc {0};       // 0-999
        unsigned int mnc {0};       // 0-999
        uint32_t cellId {0};        // 28-bits
        unsigned int tac {0};       // 16-bits
        int signalPower {0};

        CellularServing &toJsonWriter(JSONWriter &writer, bool wrapInObject = true);

        void clear();

        int parse(const char *in);

    };

    /**
     * @brief Information identifying a neighboring tower
     *
     */
    class CellularNeighbor {
    public:
        RadioAccessTechnology rat {RadioAccessTechnology::NONE};
        uint32_t earfcn {0};        // 28-bits
        uint32_t neighborId {0};    // 0-503
        int signalQuality {0};
        int signalPower {0};
        int signalStrength {0};

        CellularNeighbor &toJsonWriter(JSONWriter &writer, bool wrapInObject = true);

        void clear();

        int parse(const char *in);
    };

    class TowerInfo {
    public:
        TowerInfo();
        virtual ~TowerInfo();

        TowerInfo(const TowerInfo &other);
        TowerInfo &operator=(const TowerInfo &other);

        void clear();

        int parseServing(const char *in);
        int parseNeighbor(const char *in);

        CellularServing serving;
        std::vector<CellularNeighbor> neighbors;
    };

    int scanBlocking(TowerInfo &towerInfo);

    int scanWithCallback(std::function<void(TowerInfo towerInfo)> scanCallback);

    /**
     * @brief Start scan for cellular towers
     *
     * @retval SYSTEM_ERROR_NONE Success
     * @retval SYSTEM_ERROR_BUSY Cannot start a new scan
     * 
     */
    int startScan();

    /**
     * @brief Get the cellular signal strength
     *
     * @param[out] signal Object with signal strength values
     * @param[in] max_age How old a measurement can be to be valid
     * @retval 0 Success
     * @retval -ENODATA Measurement is old
     */
    int getSignal(CellularSignal &signal, unsigned int max_age=DEFAULT_MAX_AGE_SEC);

    /**
     * @brief Get the signal strength age
     *
     * @return unsigned int Age in seconds
     */
    unsigned int getSignalUpdate();

    /**
     * @brief Get the serving tower information
     *
     * @param[out] serving The serving tower information
     * @retval SYSTEM_ERROR_NONE Success
     */
    int getServingTower(CellularServing& serving);

    /**
     * @brief Get the neighbor towers information
     *
     * @param[out] neigbors The neighbor towers information
     * @retval SYSTEM_ERROR_NONE Success
     */
    int getNeighborTowers(Vector<CellularNeighbor>& neigbors);

    /**
     * @brief Lock object
     *
     */
    inline void lock() {mutex.lock();}

    /**
     * @brief Unlock object
     *
     */
    inline void unlock() {mutex.unlock();}

    static RadioAccessTechnology parseRadioAccessTechnology(const char *str);

    /**
     * @brief Singleton class instance access for QuectelTowerRK
     *
     * @return QuectelTowerRK&
     */
    static QuectelTowerRK &instance()
    {
        if(!_instance)
        {
            _instance = new QuectelTowerRK();
        }
        return *_instance;
    }

private:
    QuectelTowerRK();

    CellularSignal _signal;
    unsigned int _signal_update;

    TowerInfo receivedTowerInfo;
    TowerInfo savedTowerInfo;

    RecursiveMutex mutex;
    os_queue_t _commandQueue;
    Thread * _thread;

    static int serving_cb(int type, const char* buf, int len, QuectelTowerRK* context);
    static int neighbor_cb(int type, const char* buf, int len, QuectelTowerRK* context);
    CommandCode waitOnEvent(system_tick_t timeout);
    void thread_f();

    std::function<void(TowerInfo towerInfo)> scanCallback = nullptr;

    static QuectelTowerRK *_instance;
};
