# MercMouse

MercMouse lets you play Mercenaries: Playground of Destruction on PCSX2 with mouse aim.

## Requirements

* An asi loader
* PCSX2 1.7 or newer
* One of the following Mercenaries: Playground of Destruction versions
  - SLUS-20932
  - SLES-52590
  - SLES-52588

## Installation

* Get [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)
* Place the dll from Ultimate ASI Loader into the main PCSX2 directory (rename it to d3d9.dll if you are using the QT frontend)
* Unzip MercMouse.zip into the main PCSX2 directory

## Config

settings.ini is used for configuration, it's automatically created with default values on runtime.

Default settings are meant to be equivalent to source engine with sensitivity 1.

```ini
[MercMouse]
DegreesPerCount=0.022000
Sensitivity=1.000000
InvertMouse=0
ZoomSensitivity=0.500000
```

**If you want to menu around in PCSX2 press START to pause the game, otherwise the game will keep getting mouse input.**

**If you have an unsupported version of Mercenaries open an issue**
