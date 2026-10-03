# Additional permission under GNU GPL version 3 section 7

This applies to the DuchesS Wireless **transmitter** firmware (this folder).
It is an additional permission added to the GNU General Public License,
version 3 or (at your option) any later version, under which this firmware
is licensed. The full GPL text is in the LICENSE file at the repository root.

## Why this exists

The transmitter firmware is built on Espressif's ESP-IDF / Arduino-ESP32
framework. Parts of that framework — including the Bluetooth controller,
PHY (radio calibration), Wi-Fi and coexistence libraries — are distributed by
Espressif only as precompiled binary libraries, without published source code.
The GPL normally requires that anyone distributing a compiled binary also be
able to provide the complete corresponding source code. This permission makes
it clear that building and distributing this firmware with those libraries is
allowed.

## The permission

If you modify this Program, or any covered work, by linking or combining it
with the precompiled binary libraries distributed by Espressif Systems as part
of ESP-IDF and the Arduino-ESP32 framework (including the Bluetooth
controller, PHY, Wi-Fi and coexistence libraries), or modified versions of
those libraries, the licensors of this Program grant you additional permission
to convey the resulting work. Corresponding Source for a non-source form of
such a combination need not include the source code of those libraries that
has not been published by their copyright holders, but shall include the
source code of all other parts of the covered work.

You may remove this additional permission from your own modified versions of
this Program, as allowed by section 7 of the GNU GPL version 3.
