# Use CSA for live channel switching

The Experimental Access Point will change channels through a Channel Switch
Announcement while WiFi remains started. Stopping and restarting the access
point is not an acceptable implementation because the platform is intended to
exercise live AP channel migration rather than ordinary AP reconfiguration.
Success means the AP remains active, announces the transition, and applies the
new channel; continuity of the initiating web request and compatibility of
individual stations are outside this guarantee.
