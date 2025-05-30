# flexNudge


## Graphic Interface
It contains a user-friendly interface for connecting a device and performing data collection that will be sent to Edge Impulse studio, all in one place! Using the Edge Impulse API and Web Serial API.

1. It runs through Github Pages at http://todocono.github.io/flexNudge
2. Connect your device via serial port
3. Input your Edge Impulse API key and Project ID to connect to your studio
4. Start collecting data

<img width="1437" alt="Screenshot 2025-05-27 at 2 17 53 PM" src="https://github.com/user-attachments/assets/a4a4e14b-8447-42f3-820d-ac6996dda858" />

## Circuit Board
Source files of the schematic and the PCB are stored in the folder PCB. We used the PRO edition of http://easyeda.com/ 

## Firmware
Files in the nrf52840 need to be compiled with Arduino 2.3.6 with Seeed board definition for Xiao nrf52840 1.1.10 
We convert the .hex to uf2 with the command python uf2conv.py /Users/(USER)/Documents/Arduino/nrf52840/datalogger_v4/build/Seeeduino.nrf52.xiaonRF52840Sense/datalogger_v4.ino.hex -c -f 0xADA52840

## 3D case
Printed in flexible TPU, model hosted at https://www.tinkercad.com/things/9DcnymyWoEG/edit?returnTo=%2Fdashboard&sharecode=wvaeXW7-uak3Jjdi1nR7ELJ-LvCoNMIx4T89OGUJ2sk
