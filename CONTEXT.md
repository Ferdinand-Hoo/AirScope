# WiFi Experiment Platform

This context describes a configurable wireless access point used to conduct
repeatable WiFi configuration experiments independently of commercial router
brands.

## Language

**Experimental Access Point**:
An access point whose wireless parameters can be deliberately changed for a
specific experiment.
_Avoid_: Extended router, commercial router

**AP Configuration**:
The complete set of desired wireless parameters for an Experimental Access
Point, including its network identity, security settings, and radio channel.
_Avoid_: Router settings, advanced settings

**Security Profile**:
A named, valid combination of AP authentication, encryption, and management
frame protection settings.
_Avoid_: Security type, encryption option

**Legacy Security Profile**:
A Security Profile retained specifically for compatibility experiments with
older stations and restricted to isolated laboratory use.
_Avoid_: Recommended security, normal mode

**Expert Security Settings**:
Individually selected authentication, encryption, management frame protection,
and SAE behavior that together form a validated Security Profile.
_Avoid_: Raw settings, unchecked configuration

**Radio Profile**:
A valid combination of WiFi protocol mode, channel bandwidth, transmit power,
client limit, beacon interval, and DTIM period.
_Avoid_: Advanced settings, arbitrary radio values

**Persistent Configuration**:
The AP Configuration retained across device restarts and applied during normal
startup.
_Avoid_: Saved settings, last settings

**Default Configuration**:
The known AP Configuration restored through physical recovery when the
Persistent Configuration is unusable.
_Avoid_: Factory router settings, backup configuration

**Applied Configuration**:
The AP Configuration accepted by the WiFi subsystem and confirmed by runtime
readback. It is not, by itself, independent over-the-air verification.
_Avoid_: Saved configuration, requested configuration

**Operation Result**:
The recorded outcome of the most recent Configuration Transaction or Channel
Switch Operation, including whether application and runtime readback succeeded.
_Avoid_: Air capture, independent verification

**Runtime Event**:
A volatile record of a management, configuration, channel, client, or recovery
event identified by boot, sequence, and monotonic runtime.
_Avoid_: Persistent audit log, wall-clock record

**Configuration Transaction**:
An atomic validation and application of non-channel AP Configuration changes
that either becomes both applied and persistent or leaves the previous
configuration intact.
_Avoid_: Partial update, best-effort save

**Channel Switch Operation**:
An atomic request to perform a CSA Channel Switch and persist the new channel
only after the transition succeeds.
_Avoid_: Configuration Transaction, channel field update

**Management Interface**:
The web interface through which an operator views and changes the AP
Configuration.
_Avoid_: Router page, control panel

**Management API**:
The authenticated HTTPS JSON interface used by both the Management Interface
and external automation to inspect and change platform state.
_Avoid_: Web handler, internal endpoint

**Management Network**:
The fixed local IPv4 network through which stations access the Management
Interface and Management API while connected to the Experimental Access Point.
_Avoid_: Upstream network, configurable LAN

**Management Credential**:
The administrator identity and password used to establish a browser management
session independently of the AP Configuration.
_Avoid_: WiFi password, AP password

**Automation Token**:
A revocable secret issued for an external client to call the Management API
without storing the Management Credential.
_Avoid_: Admin password, permanent device key

**Status Display**:
The touch-enabled on-device view of the Applied Configuration, management
address, operation results, and current access-point status. Touch interaction
navigates and controls presentation but does not change AP Configuration.
_Avoid_: Touch configuration, local management interface

**CSA Channel Switch**:
A channel transition announced in advance to associated stations through
Channel Switch Announcement elements while the Experimental Access Point
remains active.
_Avoid_: AP restart, channel reset

**Regulatory Domain**:
The radio rules that constrain valid AP Configurations. The platform uses the
China 2.4 GHz domain with channels 1 through 13.
_Avoid_: Region setting, unrestricted channels
