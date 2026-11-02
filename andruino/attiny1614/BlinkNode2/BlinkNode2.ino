// ATtiny1614 node 2 on the shared half-duplex control bus.
// Connect PB3/RX and PB2/TX to the shared Uno D5 bus, each through
// its own 1 kOhm series resistor. Check pin names on the exact package.
// Use a common ground.
#define NODE_ADDRESS 2
#include <attiny_bus_node.h>
