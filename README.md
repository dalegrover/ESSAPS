# ESSAPS: Experimental Small-Scale Anodizing Power Supply

The goal of this project is to create an automated power supply to support small-scale anodizing.  By “small-scale”, the output is in the range of 10-15A; this is sufficient for anodizing surfaces of around 1 ft^2 at 12 ASF (amp per sq. ft), or up to around 3 ft^2 at “low current density” (LCD) rates of about 4.5 ASF.
The user interface is via a web page served over WiFi.  The pages are simple HTML--no Javascript, etc.

The power supply is automated and performs the following:
* Given ASF, surface area, ramp time, and target thickness, calculates current, total time, estimated peak voltage, and estimated peak resistance.
* Ramps up the constant current to the target current.
* Stops the current and signals the operator if the time is up or (if enabled) a decrease in resistance is detected (i.e., peak anodizing resistance or PAR).  The latter has been claimed by some to provide a much more reliable method of maximizing layer thickness independent of electrolyte condition, temperature, etc.
* Generates plots in real time of the voltage and current.
* Generates numeric data points for voltage and current (can be copied to a spreadsheet for analysis or plotting).
* By adding a large-valued capacitor and a few low-cost components, the power supply can act as a wire-bonder for bonding aluminum wire to objects to be anodized (i.e., Capacitor-Discharge Welding [CDW], also known in some circles as "sput welding").

This device doesn't do anything that a constant-current power supply plus an attentive person can't do.  But, it will do the ramping, timing, and peak resistance detection autonomously, freeing the operator to do other things.

The hardware components of this power supply are:
* 10-15A, 24-50V fixed or variable voltage switching power supply.  A popular brand would be "Mean Well".   24V is likely just sufficient for just anodizing; 48-50V is useful if wire-bonding will be used.
* DPS5015 or DPS5020.  Make sure it supports communication (sometimes sold with a USB converter or Bluetooth, though neither adapter is used for this project).  The DPS3012 will not work--it does not support ModBus apparently.
* ESP-WROOM-32 module.  Make sure the metal shield says "ESP-WROOM-32"--other versions may not work with this code.
* JST-XX 4-pin female cable
* If CDW is desired, a 50,000 to 90,000 uF (i.e., 0.05 to 0.1 F) electrolytic capacitor rated at least 5-10V more than the maximum power supply output.

Build the code using the Arduino IDE.  Follow the directions to add the ESP32 support to Arduino from, for example, https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html
It should be possible to connect a USB cable to the ESP-32 and program it.
Settings:
* Board: "ESP32 Dev Module"
* Upload Speed: "921600"
* CPU Frequency: "240MHz (WiFi/BT)"
* Flash Frequency: "80MHz"
* Flash Mode: "QIO"
* Flash Size: "4MB (32Mb)"
* Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
* Core Debug Level: "None"
* PSRAM: "Disabled"


See the project wiki for more details.
