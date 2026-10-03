# Additional permission under GNU GPL version 3 section 7

This applies to the DuchesS Wireless **receiver** firmware (this folder).
It is an additional permission added to the GNU General Public License,
version 3 or (at your option) any later version, under which this firmware
is licensed. The full GPL text is in the LICENSE file at the repository root.

## Why this exists

The receiver firmware is built with the Raspberry Pi Pico SDK and uses two
libraries that come with it for Bluetooth on the Pico 2 W:

- **BTstack** by BlueKitchen GmbH — its own license allows only
  non-commercial use; Raspberry Pi additionally grants a license for use with
  Raspberry Pi hardware (see `src/rp2_common/pico_btstack/LICENSE.RP` in the
  Pico SDK).
- **cyw43-driver** by George Robotics, including the precompiled firmware for
  the Infineon CYW43439 wireless chip — in the version used by Pico SDK 2.2.0
  its license also allows only non-commercial use, with a separate license
  for Raspberry Pi hardware.

Those terms are not compatible with the GPL on their own. This permission
makes it clear that building and distributing this firmware combined with
those libraries is allowed.

## The permission

If you modify this Program, or any covered work, by linking or combining it
with BTstack (by BlueKitchen GmbH) and/or cyw43-driver (by George Robotics
Pty Ltd, including the CYW43439 wireless chip firmware it contains), as
distributed with the Raspberry Pi Pico SDK, or modified versions of those
libraries, the licensors of this Program grant you additional permission to
convey the resulting work. Corresponding Source for a non-source form of such
a combination shall include the source code for the parts of those libraries
used, where that source code has been published by their copyright holders,
as well as that of the covered work.

You may remove this additional permission from your own modified versions of
this Program, as allowed by section 7 of the GNU GPL version 3.

## Note

This permission covers the licensing of **this project's** code. It does not
change the licenses of BTstack or cyw43-driver themselves; anyone
distributing the combined firmware still has to follow those libraries' own
terms (for example, their non-commercial and Raspberry-Pi-hardware
conditions).
