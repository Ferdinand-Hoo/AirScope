# Use one management API for web and automation

All configuration reads and mutations will pass through one authenticated,
versioned HTTPS JSON Management API. The browser Management Interface consumes
the same API exposed to external automation, keeping validation and application
semantics consistent and avoiding a page-specific control path.
