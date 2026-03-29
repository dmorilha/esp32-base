# ESP32

This is how I am bootstrapping my ESP32 projects,
as I explore the limits of the platform.

Since this SoC has two cores, background tasks are spawn for NTP updates,
and for persisting log messages into a SD card every 5 minutes.

The NTP expects a WiFi network connected to the internet is intermittently
available. I've been using my cellphone as the gateway.

There is currently no synchronization among the tasks as there is not
a lot of room for overhead.
