# MiniBikeDash
Author: Ashwin Thomas

Date: August 15, 2026

This is designed for use on a bike equipped with a predator 212 engine and a 12V battery.
The dash features a digital tachometer, fuel gauge, and battery level.
The display uses an 8080 parallel interface controlled by an STM32G4

The predator 212 uses a single flywheel magneto ignition system. 
The tachometer taps into the killwire of the engine and isolates the signal using an optocoupler circuit.
The optocoupler is wired for negative logic such that TIM3 captures the falling edge and processes the time between captures.

The fuel gauge uses a 600 ohm potentiometer actuated by a float in the gas tank.
The battery level uses a resistor divider to sample the battery voltage.
ADC DMA sampling for both fuel and battery level are performed using TIM6 interrupts on a 10Hz sampling rate to reject spikes in their levels. The DMA uses a circular buffer to take the median value of the samples. 
Using the median value, the fuel level is updated within a maximum allowable change.