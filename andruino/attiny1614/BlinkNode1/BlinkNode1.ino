// Fresh two-node bus node 1.
// Physical pin 6 (PB3/RX) and pin 7 (PB2/TX) are connected to Uno D5,
// each through an individual 1 kOhm resistor.
// Physical pin 9 (PB0) drives this node's PMOS gate: HIGH disables UPDI,
// LOW enables UPDI. The node firmware drives and controls this output.
#define NODE_ADDRESS 1
#include <attiny_bus_node.h>
