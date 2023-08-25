# ESP32 Wifi Manager

Is a C esp-idf component for ESP32 allows easy Wifi networking and http client.
Based on the Tony Pottier project https://github.com/tonyp7/esp32-wifi-manager

Advanced features:
  - support secure enterprise wifi networks
  - support flash logging
  - secure http client
  - OTA support

# Getting Started

## Requirements

To get you started, esp32-wifi-manager needs:

- esp-idf **4.4.4 and up**
- esp32 or esp32-c3

## Hello World

Clone the repository where you want it to be. If you are unfamiliar with Git, you can use Github Desktop on Windows:

```bash 
git clone https://github.com/vivask/wifi-manager.git
```

In Visual Studio Code open folder *wifi-manager*

## User Interface

The user interface source code is located here https://github.com/vivask/wifi-manager-ui
The compiled user interface code is located *wifi-manager/components/wifi-manager/dist*
After making changes to the user interface code, compile it according to the instructions. Next, you need to replace the existing code:

```bash 
rm -rf wifi-manager/components/wifi-manager/dist
cp wifi-manager-ui/components/wifi-manager/dist wifi-manager/components/wifi-manager/dist
```