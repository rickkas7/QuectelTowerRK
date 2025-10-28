# QuectelTowerRK

*Cellular tower information for Particle devices with Quectel cellular modems*

 This includes:
 - M-SoM (M-SoM, Muon)
 - b5som variants of B-SoM (B504e, B524, B523)
 - Tracker (Tracker One, Monitor One, Tracker SoM)

This library extracts the tracker_cellular.cpp and tracker_cellular.h files out of the [tracker_edge](https://github.com/particle-iot/tracker-edge) project and renames the class to be more generic.

It also adds blocking and callback methods, instead of requiring a fixed delay after calling startScan.

See examples 1-simple and 2-async for blocking and non-blocking usage.

There are two main reasons to use this class:

- You need not just serving cell identifier, but also neighboring cell info for better cellular geolocation
- You want to efficiently access RSSI information.

One issue with Cellular.RSSI() is that it is blocking, and if the system thread is blocked, it can block for a very long time, up to 10 minutes. This class includes a thread for getting serving and neighboring cell info, but also includes a request to get the CellularSignal (strength and quality) information. By using getSignal() in this class instead of using Cellular.RSSI() directly, you are much less likely to be blocked. The RSSI is updated every few seconds. This technique is also used in tracker-edge and monitor-edge, and is how the signal strength LED is implemented.

Other notes:

- This cannot be used with the bsom (B4xx) and Boron, which have u-blox cellular modems. This class only works with Quectel cellular modems including the BG95, BG96, and EG91.
- It's fast, often under 20 milliseconds, and almost always under a few seconds, because it's just returning the data that's already stored in the cellular modem.
- It can only be used after connecting to cellular, so it won't help with scanning for towers when you can't connect.
- If you do not need neighboring cells, you should use [CellularGlobalIdentity](https://docs.particle.io/reference/device-os/api/cellular/cellular-global-identity/) built into Device OS, which does not require a separate library.
