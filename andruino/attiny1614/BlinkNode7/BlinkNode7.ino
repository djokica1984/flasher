// ATtiny1614 node 3 on the shared half-duplex control bus.
// Connect PB3/RX (physical pin 6) and PB2/TX (physical pin 7) to Uno D5,
// each through its own 1 kOhm series resistor. PB0 (physical pin 9) is the
// inverted level of the active-low LED control on PA5 (physical pin 3) only in FLASH mode.
// Use a common ground.
#define NODE_ADDRESS 7
#include <attiny_bus_node.h>
