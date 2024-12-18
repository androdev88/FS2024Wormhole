# Flight Simulator 2024 Teleport Utility
* Simple util to make the game playable.
* Works in Career mode
* Version 1.2.7.0 supported
* webserver at http://localhost:8089 - open firewall to access from other devices
* works with MS Store version
* app create Tray icon showing number of game instances, clicking the icon will close the app
  
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
If you are using Steam version of the game you probably will have to find memory pointers yourself.
You will need working VFRmap addon (in free flight) and Cheat Engine.

1. Open Cheat Engine and connect to the game, set value type to Double
2. Start a free flight, pause the game
3. Use VFRmap to teleport the plane to some arbitrary altitude e.g. 12345 ft
4. Search for 12345 Double value in Cheat Enginge
5. Repeat steps 3-5 until you are down to 200-300 values, select all and add to the address list
6. Unpause the game, put plane in a steep descend and start freezing values in address list (put checkbox), find one that freezes the altitude (the plain will start "jumping" as it will be teleported to frozen value every 50ms)
7. Remove all other items from address list, you should have one that controls plane altitude
8. Copy and paste this address using "Adjust address by": `-30` this will add address for plane's latitude (in radians), remove altitude address from list
9. Right click on that address and Generate pointermap - it will take a long time and use a lot of disk space
10. Restart the game follow steps 1-8
11. Once you have found new address for altitude, right click and choose Pointer scan for this address
12. Check "Compare results with other saved pointermap(s)" and choose the file from step 8
13. Wait - takes even longer than step 8
14. Once you have a list of possible pointers, sort it and use the one with fewest offsets
15. Put the found pointer and offsets in config file `[Pointers]` section with game version (from window header) as key, first value should be the part after `+` in Base Address, add offsets after commas (prepend values with 0x)
16. Share your finding with the community :)

If you find that after the game restart the pointer stops working try reopening pointer map from step 13 and find another pointer that shows correct value (latitude in radians).

There are better tutorials on how to use Cheat Engine Pointer Scanning on the Internet.

Example pointer map used for 1.0.0 release is at https://github.com/androdev88/FS2024Wormhole/blob/1.2.7.0/1.2.7.0.zip it was generated on 1.2.7.0 MS Store game version.
