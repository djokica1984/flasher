// Fresh two-node bus node 2.
// Physical pin 6 (PB3/RX) and pin 7 (PB2/TX) are connected to Uno D5,
// each through an individual 1 kOhm resistor.
// Physical pin 9 (PB1) is reserved for the external PMOS gate and must
// remain electrically driven by Uno D4; the firmware never drives it.
#define NODE_ADDRESS 2
#include <attiny_bus_node.h>
