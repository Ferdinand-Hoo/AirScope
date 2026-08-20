# Trust physical access during development

The first release protects management access from wireless network clients but
treats physical access to the development board as trusted. Management
passwords and Automation Tokens are stored only as salted hashes, while
recoverable AP credentials and the HTTPS private key remain in NVS; Secure Boot,
Flash Encryption, encrypted NVS, and restrictive eFuse settings are deferred
to a separate production-hardening decision so development and USB recovery
remain available.
