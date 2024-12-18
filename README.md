# Flight Simulator 2024 Teleport Utility
* Simple util to make the game playable.
* Works in Career mode
* Version 1.2.7.0 supported
* webserver at http://localhost:8089 - open firewall to access from other devices
* works with MS Store version, (I'll add tutorial to find pointers in any version)

# Speed run - CR farming
1. Find well paid mission take a note of destination ICAO
2. Go to Free Flight for given ICAO, when plane is on runway, in web UI input ICAO under game instance Actions and hit Add (it will store the coordinates in locations.csv for future use)
3. Start you mission, skip the first part, when asked to "announce taxi" pause the game (important as loading scenery might crash your plane)
4. Click Teleport + 500ft, wait for game to load scenery
5. Click Teleport - you are now on runway of destination airport
6. Taxi onto the grass to enable skip to shutdown

Sometimes it won't detect you landed at destination, you need to takeoff and sometimes fly to nearest VOR and land again.
When flying to VOR is not required you can do a quick takeoff/landing by pausing on rotate clicking +500 un-pausing for a split second and going back to runway with -500.

# Playing the game 
Follow similar steps to Speed run, do a proper take off, teleport using +2000ft button do a proper approach.

# Hotkeys
* Ctrl+Alt+F11 - +500ft
* Ctrl+Alt+F12 - -500ft

# Contributing
* Use Microsoft Visual Studio Community 2022 (64-bit) to compile the project (requires cpprest library)
* Entire UI is in the standalone index.html that uses React
* 90% of code is AI generated, code quality is atrocious
* Any contributions welcome

# Adding support for new game version 
Add memory pointers into `[Pointers]` section of config.ini with game version as key
First pointer is added to the game base address, followed by list of offsets
