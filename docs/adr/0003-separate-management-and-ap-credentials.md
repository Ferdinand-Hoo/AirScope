# Separate management and AP credentials

The Management Interface will always require its own Management Credential,
independent of the AP Configuration and its WiFi password. This keeps
administrative control protected while the Experimental Access Point is
deliberately configured as an open network or has its WiFi password changed.
The username and default password are both `admin` on first boot and after
physical recovery, prioritizing predictable access in the trusted lab
environment over default credential strength. The password can be replaced
after login and a replacement remains stored across ordinary firmware updates.
