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
    /**
     * @brief Delay between checking cell strength when no errors detected
     */
    static constexpr system_tick_t PERIOD_SUCCESS_MS {1000};

    /**
     * @brief Delay between checking cell strength when errors detected
     * 
     * Longer than success to minimize thrashing on the cell interface which could
     * delay recovery in Device-OS
     */
    static constexpr system_tick_t PERIOD_ERROR_MS {10000};

    /**
     * @brief Cell updates need to be at least this often or flagged as an error
     */
    static constexpr unsigned int DEFAULT_MAX_AGE_SEC {10};

    /**
     * @brief Commands to instruct cellular thread, used internally
     */
    enum class CommandCode {
        None,                   /**< Do nothing */
        Measure,                /**< Perform cellular scan */
        Exit,                   /**< Exit from thread */
    };

    /**
     * @brief Type of radio used in modem to tower communications
     */
    enum class RadioAccessTechnology {
        NONE = -1, //!< Not set or not known
        LTE = 7, //!< LTE Cat 1
        LTE_CAT_M1 = 8, //!< LTE Cat M1
        LTE_NB_IOT = 9 //!< LET Cat NB1 (NBIoT)
    };

    /**
     * @brief Information identifying the serving tower
     * 
     * This class is contained within the TowerInfo class.
     */
    class CellularServing {
    public:
        RadioAccessTechnology rat {RadioAccessTechnology::NONE}; //!< Radio access technology, also used as a validity flag
        unsigned int mcc {0};       //!< Mobile Country Code 0-999
        unsigned int mnc {0};       //!< Mobile Network Code 0-999
        uint32_t cellId {0};        //!< Cell identifier 28-bits
        unsigned int lac {0};       //!< Location area code 16-bits
        int signalPower {0};        //!< Signal power

        
        /**
         * @brief Convert this object to a readable string
         * 
         * @return String 
         */
        String toString() const;

        /**
         * @brief Convert this object to JSON
         * 
         * @param writer JSONWriter to write the data to
         * @param wrapInObject true to wrap the data with writer.beginObject() and writer.endObject(). Default = true.
         * @return CellularServing& 
         */
        CellularServing &toJsonWriter(JSONWriter &writer, bool wrapInObject = true);

#ifdef SYSTEM_VERSION_v620
        /**
         * @brief Save this data in a Variant object. Requires Device OS 6.2.0 or later.
         * 
         * @param obj Variant object to add to
         * @return CellularServing& 
         */
        CellularServing &toVariant(Variant &obj);
#endif // SYSTEM_VERSION_v620

        /**
         * @brief Clear the object to default values
         */
        void clear();

        /**
         * @brief Parse the results of an AT+QENG serving cell request
         * 
         * @param in 
         * @return int 
         */
        int parse(const char *in);

        /**
         * @brief Returns true if the object appears to contain valid data.
         * 
         * @return true 
         * @return false 
         * 
         * This just checks the RAT to make sure it's not NONE.
         */
        bool isValid() const;
    };

    /**
     * @brief Information identifying a neighboring tower
     *
     */
    class CellularNeighbor {
    public:
        RadioAccessTechnology rat {RadioAccessTechnology::NONE}; //!< Radio access technology, also used as a validity flag
        uint32_t earfcn {0};        //!< EARFCN 28-bits
        uint32_t neighborId {0};    //!< neighbor ID 0-503
        int signalQuality {0};      //!< Signal quality
        int signalPower {0};        //!< Signal power
        int signalStrength {0};     //!< Signal strength

        /**
         * @brief Convert this object to a readable string
         * 
         * @return String 
         */
        String toString() const;

        /**
         * @brief Convert this object to JSON
         * 
         * @param writer JSONWriter to write the data to
         * @param wrapInObject true to wrap the data with writer.beginObject() and writer.endObject(). Default = true.
         * @return CellularServing& 
         */
        CellularNeighbor &toJsonWriter(JSONWriter &writer, bool wrapInObject = true);

#ifdef SYSTEM_VERSION_v620
        /**
         * @brief Save this data in a Variant object. Requires Device OS 6.2.0 or later.
         * 
         * @param obj Variant object to add to
         * @return CellularNeighbor& 
         */
        CellularNeighbor &toVariant(Variant &obj);
#endif // SYSTEM_VERSION_v620

        /**
         * @brief Clear the object to default values
         */
        void clear();

        /**
         * @brief Parse the results of an AT+QENG neighbor cells request
         * 
         * @param in 
         * @return int 
         */
        int parse(const char *in);

        /**
         * @brief Returns true if the object appears to contain valid data.
         * 
         * @return true 
         * @return false 
         * 
         * This just checks the RAT to make sure it's not NONE.
         */
        bool isValid() const;
    };

    /**
     * @brief Container for serving tower and neighbor tower information
     */
    class TowerInfo {
    public:
        TowerInfo(); //!< Default constructor
        virtual ~TowerInfo(); //!< Destructor

        /**
         * @brief You can construct an object as a copy of another object. This is a deep copy.
         * 
         * @param other 
         * 
         * After copying, changes to the other object have no effect on this object. Also the other object
         * can be deleted, if desired.
         */
        TowerInfo(const TowerInfo &other);

        /**
         * @brief Copy another object into this object. This is a deep copy.
         * 
         * @param other 
         * 
         * The previous contents of this object are cleared first; this does not merge data.
         */
        TowerInfo &operator=(const TowerInfo &other);

        /**
         * @brief Clear the object to default values with no neighbors
         */
        void clear();

        /**
         * @brief Parse the results of an AT+QENG serving cell request
         * 
         * @param in 
         * @return int 
         */
        int parseServing(const char *in);

        /**
         * @brief Parse the results of an AT+QENG neighbor cells request
         * 
         * @param in 
         * @return int 
         */
        int parseNeighbor(const char *in);

        /**
         * @brief Log the information to the debugging log
         * 
         * @param msg A message to write before the serving cell
         * @param level Logging level. Default is LOG_LEVEL_TRACE. LOG_LEVEL_INFO is another common option.
         */
        void log(const char *msg, LogLevel level = LOG_LEVEL_TRACE);

        /**
         * @brief Add the serving and neighbor towers to the writer in an array
         * 
         * @param writer 
         * @param numToInclude Number of towers to add, or 0 for all
         * @return TowerInfo& 
         */
        TowerInfo &toJsonWriter(JSONWriter &writer, int numToInclude = 0);

#ifdef SYSTEM_VERSION_v620
        /**
         * @brief Add the serving and neighbor towers to the Variant in an array. Requires Device OS 6.2.0 or later.
         * 
         * @param obj Variant array to add to
         * @param numToInclude Number of towers to add, or 0 for all
         * @return TowerInfo& 
         */
        TowerInfo &toVariant(Variant &obj, int numToInclude = 0);
#endif // SYSTEM_VERSION_v620

        /**
         * @brief Returns true if the object appears to contain valid data.
         * 
         * @return true 
         * @return false 
         * 
         * This just checks the serving cell RAT to make sure it's not NONE.
         */
        bool isValid() const;

        /**
         * @brief The serving cell. This member is public.
         */
        CellularServing serving;

        /**
         * @brief Vector of neighbor cells. This member is public.
         * 
         * You can use vector members on this, like size(), at(), and iter() to work with the results.
         */
        std::vector<CellularNeighbor> neighbors;
    };

    /**
     * @brief Scan for towers, blocking.
     * 
     * @param towerInfo Filled in with serving tower and neighboring tower information.
     * @param timeoutMs How long to wait in milliseconds for a response (0 = wait forever). Default is 10 seconds.
     * @return int 
     * 
     * This call can take as little as 20 milliseconds, but may take up to a few seconds if connected to cellular.
     * It can block for longer if not connected to cellular as it will wait until connected.
     */
    int scanBlocking(TowerInfo &towerInfo, unsigned long timeoutMs = 10000);

    /**
     * @brief Asynchronous scan for cellular towers with callback function
     * 
     * @param scanCallback Callback function to call when complete
     * @retval SYSTEM_ERROR_NONE Success
     * @retval SYSTEM_ERROR_BUSY Cannot start a new scan
     * 
     * The callback function is only called if the return value is SYSTEM_ERROR_NONE. It is called from
     * a separate worker thread.
     * 
     * Callback function prototype for a C++ function or lambda:
     * 
     * void callback(TowerInfo towerInfo)
     * 
     * This call can take as little as 20 milliseconds, but may take up to a few seconds if connected to cellular.
     * It can take for longer if not connected to cellular as it will wait until connected.
     */
    int scanWithCallback(std::function<void(TowerInfo towerInfo)> scanCallback);

    /**
     * @brief Start scan for cellular towers
     *
     * @retval SYSTEM_ERROR_NONE Success
     * @retval SYSTEM_ERROR_BUSY Cannot start a new scan
     * 
     * This is a low-level function; you'd typically use scanBlocking() or scanWithCallback().
     */
    int startScan();

    /**
     * @brief This method makes sure the callback is not called 
     * 
     * It's used internally by scanBlocking but if you are using scanWithCallback you can also
     * use this to cancel the pending callback. 
     */
    void cancelScan();

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
     * @brief Get the most recently retrieved tower information
     * 
     * @param towerInfo 
     * 
     * This returns the last saved value and does not scan again. See scanBlocking and scanWithCallback.
     */
    void getTowerInfo(TowerInfo &towerInfo);

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

    /**
     * @brief Parse a AT+QENG RAT string to convert it to a RadioAccessTechnology value (an int).
     * 
     * @param str 
     * @return RadioAccessTechnology 
     */
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
    /**
     * @brief The constructor is private because the class is a singleton
     * 
     * Use QuectelTowerRK::instance() to instantiate the singleton.
     */
    QuectelTowerRK();

    /**
     * @brief The destructor is private because the class is a singleton and cannot be deleted
     */
    virtual ~QuectelTowerRK();

    /**
     * This class is a singleton and cannot be copied
     */
    QuectelTowerRK(const QuectelTowerRK&) = delete;

    /**
     * This class is a singleton and cannot be copied
     */
    QuectelTowerRK& operator=(const QuectelTowerRK&) = delete;

    CellularSignal cellularSignal; //!< Last result from Cellular.RSSI()
    unsigned int cellularSignalLastUpdate; //!< Value of System.uptime() at last RSSI update (in seconds)

    TowerInfo receivedTowerInfo; //!< Value currently being received by the worker thread
    TowerInfo savedTowerInfo; //!< Copy of complete data, to reduce the amount of time the mutex is locked

    RecursiveMutex mutex; //!< Mutex to prevent accessing certain data from multiple threads at the same time
    os_queue_t commandQueue; //!< Command requests to be processed by the worker thread
    Thread * thread; //!< The worker thread

    static int serving_cb(int type, const char* buf, int len, QuectelTowerRK* context); //!< Callback for Cellular.command for serving cell request
    static int neighbor_cb(int type, const char* buf, int len, QuectelTowerRK* context); //!< Callback for Cellular.command for neighbor cell request
    CommandCode waitOnEvent(system_tick_t timeout); //!< Wait for a command to be added to the queue
    void threadFunction(); //!< Worker thread function

    std::function<void(TowerInfo towerInfo)> scanCallback = nullptr; //!< Callback when scan is complete

    static QuectelTowerRK *_instance; //!< Singleton instance
};
