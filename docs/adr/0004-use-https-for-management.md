# Use HTTPS for management

The Management Interface will be served only over HTTPS with a device
self-signed certificate. Operators accept the certificate warning when first
opening the fixed local management address; plain HTTP is excluded because the
Experimental Access Point may intentionally operate as an open network where
credentials and sessions would otherwise be observable.
