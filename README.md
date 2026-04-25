# Motion-Tracking-System-Embedded-Mouse
Program was developed in Keil MicroVision. Setting up the starter files was very fun.

Collaborators: [@CadenDo](https://github.com/cadendo)

## Embedded USB HID Air Mouse (Tiva C + LIS3DH)

This project implements a real-time embedded motion tracking system that functions as a USB Human Interface Device (HID) mouse. It runs on the **Texas Instruments EK-TM4C123GXL (Tiva C Series)** and translates physical motion into cursor movement on a host computer.

### Overview

Originally designed to use the **PMW3389 laser motion sensor**, the system was adapted to use the **LIS3DH 3-axis accelerometer** due to supply constraints. This change effectively converts the device into an *air mouse*, where cursor movement is controlled by tilting the device along the X and Y axes.

### Features

* Real-time USB HID mouse functionality
* Air mouse control using accelerometer input
* Reliable button input handling (no double-detection)
* Low-latency performance for motion and click events

### Hardware

* **MCU:** Texas Instruments EK-TM4C123GXL (Tiva C Series)
* **Sensor:** LIS3DH 3-axis accelerometer

### Communication Protocols

* **SPI** → Sensor data acquisition
* **UART** → Debugging / serial communication
* **USB (HID)** → Host communication (mouse input)

### Performance

* **Latency:** ~1.14 ms (combined click + motion)

  * Meets target thresholds:

    * Click: < 5 ms
    * Motion: < 12 ms
* **Click Reliability:** 100% (no false double clicks detected)

### Implementation Notes

* Motion is derived from accelerometer tilt (X and Y axes)
* Embedded software handles sensor polling, input processing, and USB report generation
* Designed with modular architecture to support sensor substitution

### Learning Outcomes

This project demonstrates:

* Integration of multiple serial communication protocols (SPI, UART, USB)
* Embedded software design for real-time systems
* Practical application of USB HID stack on microcontrollers
* System-level debugging and optimization
* Use of AI tools alongside peer collaboration during development

### Status

✅ Fully functional prototype
✅ Meets latency and reliability targets

