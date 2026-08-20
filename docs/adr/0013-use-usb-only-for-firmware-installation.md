# Use USB only for firmware installation

The first release will install and recover firmware only through USB and will
not expose local or Internet OTA. The flash layout therefore does not reserve
dual OTA application slots; adding OTA later requires an explicit partition
layout and migration decision rather than shipping an incomplete update path.
