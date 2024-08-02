<!--BEGIN_BANNER_IMAGE-->
<!--END_BANNER_IMAGE-->

# Embedded for LiveKit
<!--BEGIN_DESCRIPTION-->
<!--END_DESCRIPTION-->

# Table of Contents

- [Docs](#docs)
- [Installation](#installation)
- [Usage](#usage)

## Docs

Docs and guides at [https://docs.livekit.io](https://docs.livekit.io).

## Installation

`protoc` must be in your path with `protobufc` installed.

Make sure that `set-target` is the platform you are targetting. Example below is an `esp32s3`
* `idf.py set-target esp32s3`

Configure device specific settings. None needed at this time
* `idf.py menuconfig`

Set your Wifi SSID + Password as env variables
* `export WIFI_SSID=foo`
* `export WIFI_PASSWORD=bar`
* `export LIVEKIT_URL`

Build and flash
* `idf.py build`
* `sudo -E idf.py flash`

## Usage

<!--BEGIN_REPO_NAV-->
<!--END_REPO_NAV-->
