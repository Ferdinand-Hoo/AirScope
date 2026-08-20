# Fix the local management network

The standalone access point will use `192.168.4.1/24` as its management address
and serve DHCP leases from `192.168.4.100` through `192.168.4.199`. The first
release will not expose the subnet, DHCP pool, DNS, gateway, NAT, or captive
portal as experiment settings, preserving a stable HTTPS and automation
endpoint while wireless parameters change.
