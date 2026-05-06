`# Gate Controller - External API - `0.11`
## <a id="gate_contents">MQTT API</a>
#### Validation and Credits
- `PUB` [config/gate/validation](#config_gate_validation)
- `SUB` [status/gate/validation](#status_gate_validation) **`retained`**
- `PUB` [control/gate/validation/lock](#control_gate_validation_lock)
- `PUB` [control/gate/validation/unlock](#control_gate_validation_unlock)
- `SUB` [status/gate/validation/lock](#status_gate_validation_lock) **`retained`**
- `PUB` [events/validator/validation/success](#events_validator_validation_success)
- `PUB` [events/validator/validation/fail](#events_validator_validation_fail)
- `SUB` [events/gate/validations](#events_gate_validations)
- `SUB` [status/gate/direction](#status_gate_direction) **`retained`** **`DEPRECATED`**
- `SUB` [status/gate/credits](#status_gate_credits) **`retained`**
#### Control
- `PUB` [control/gate/mode](#control_gate_mode)
- `SUB` [status/gate/mode](#status_gate_mode) **`retained`**
- `SUB` [status/gate/emergency](#status_gate_emergency) **`retained`**
- `PUB` [control/gate/reboot](#control_gate_reboot)
#### Status
- `SUB` [status/gate/emergencyButton](#status_gate_emergency_button) **`retained`**
- `SUB` [status/gate/faults](#status_gate_faults) **`retained`**
- `SUB` [status/gate/paddles](#status_gate_paddles) **`retained`**
- `SUB` [status/gate/paddlesForced](#status_gate_paddles_forced) **`retained`**
- `SUB` [status/gate/panelAccess](#status_gate_panel_access) **`retained`**
- `SUB` [status/gate/power](#status_gate_power) **`retained`**
- `SUB` [status/gate/sensorsBlocked](#status_gate_sensors_blocked) **`retained`**
- `SUB` [status/gate/software](#status_gate_software) **`retained`**
- `SUB` [status/gate/stats](#status_gate_stats) **`retained`**
- `PUB` [status/validator/state](#status_validator_state) **`retained`**
#### Heartbeats
- `PUB` [heartbeat/validator](#heartbeat_validator)
- `SUB` [heartbeat/gate](#heartbeat_gate)
#### Config
- `PUB` [config/gate/sensors](#config_gate_sensors)
- `SUB` [status/gate/sensors](#status_gate_sensors) **`retained`**
- `PUB` [config/gate/paddleForces](#config_gate_paddle_forces)
- `SUB` [status/gate/paddleForces](#status_gate_paddle_forces) **`retained`**
- `PUB` [config/gate/paddleSpeeds](#config_gate_paddle_speeds)
- `SUB` [status/gate/paddleSpeeds](#status_gate_paddle_speeds) **`retained`**
- `PUB` [config/gate/system](#config_gate_system)
- `SUB` [status/gate/system](#status_gate_system) **`retained`**
#### Metrics
- `PUB` [control/gate/metrics/reset](#control_gate_metrics_reset)
- `SUB` [metrics/gate/credits](#metrics_gate_credits) **`retained`**
- `SUB` [metrics/gate/evasion](#metrics_gate_evasion) **`retained`**
- `SUB` [metrics/gate/paddles](#metrics_gate_paddles) **`retained`**
- `SUB` [metrics/gate/passage](#metrics_gate_passage) **`retained`**

## <a id="http_api">HTTP API</a>
- `POST` [upload_software](#upload_software)
- `GET`  [download_logs](#download_logs)

##
---
> ### <a id="config_gate_validation"></a>config/gate/validation `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message to request a change to the Gate's validation behaviour.
**Note**: These values are only used when the gate is in `validation` mode, see [`control/gate/mode`](#control_gate_mode).

#### Payload
- `entryTimeout`: The number of seconds a validation has to be claimed before expiring, on the entry side (**__integer__**) [`5-30`]
- `exitTimeout`: The number of seconds a validation has to be claimed before expiring, on the exit side (**__integer__**) [`5-30`]
- `entryMode`: The entry direction validation behaviour (**__string__**) [`controlled, free, locked`]
- `exitMode`: The exit direction validation behaviour (**__string__**) [`controlled, free, locked`]
- `paddlePosition`: Default position for the paddles in validation mode (**__string__**) [`open_entry, open_exit, closed`]
#### Example
```json
{
    "entryTimeout": 15,
    "exitTimeout": 10,
    "entryMode": "controlled",
    "exitMode": "free",
    "paddlePosition": "closed"
}
```
---
> ### <a id="status_gate_validation"></a>status/gate/validation `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message indicating a change to the Gate's validation configuration.\
Sent on validation mode startup and on change.
#### Payload
The payload follows the same structure as the config message, see [config/gate/validation](#config_gate_validation)

---
> ### <a id="control_gate_validation_lock"></a>control/gate/validation/lock `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Request to lock the direction of the gate, prior to a validation taking place.\

Validation locking is not required, as the gate will lock itself based on a first-come, first-served basis.
However, to add more control, users of this API can choose to lock prior to a validation taking place to allow time for
e.g. card transaction processing to prevent collisions with the opposite direction.

If the lock request is accepted (see [`status/gate/validation/lock`](#status_gate_validation_lock)), success events
([`events/validator/validation/success`](#events_validator_validation_success)) from a validator in the locked direction will always be `accepted` and
success events from any validator, or free passage, in the opposite direction will be `rejected`.\
The lock remains in place until unlocked (see [`control/gate/validation/unlock`](#control_gate_validation_unlock)), the validation configuration
changes ([`config/gate/validation`](#config_gate_validation)) or the gate mode is changed ([`control/gate/mode`](#control_gate_mode)).

**Note**: A direction can only be locked if a validation lock is not already in place.
#### Payload
- `direction`: The direction for the lock request (**__string__**) [`entry, exit`].
#### Examples
```json
{
    "direction": "entry"
}
```
---
> ### <a id="control_gate_validation_unlock"></a>control/gate/validation/unlock `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Request to unlock the direction of the gate, after locking with [`control/gate/validation/lock`](#control_gate_validation_lock).\
Status of the lock is reported in the [`status/gate/validation/lock`](#status_gate_validation_lock) topic.

**Note**: If the gate was not locked with the [`control/gate/validation/lock`](#control_gate_validation_lock) message (i.e. the gate locked itself),
then this message will have no effect.
#### Payload
- `direction`: The direction for the unlock request (**__string__**) [`entry, exit`].
#### Examples
```json
{
    "direction": "entry"
}
```
---
> ### <a id="status_gate_validation_lock"></a>status/gate/validation/lock `SUB` `retained` <span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
The gate will only serve one direction at a time - rejecting validations or free passage from the opposite direction.\
This status message indicates the direction currently being served by the gate, if any.

The gate will lock this direction itself on [`events/validator/validation/success`](#events_validator_validation_success) or `free passage` events, or it
can be locked with the [`control/gate/validation/lock`](#control_gate_validation_lock) message to add more control on priorities.
#### Payload
- `direction`: The direction that the gate is currently locked in (**__string__**) [`entry, exit, none`].
#### Examples
```json
{
    "direction": "none"
}
```
---
> ### <a id="events_validator_validation_success"></a>events/validator/validation/success `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates, to the gate, that a connected validator has performed a successful validation for the specified direction.\
If accepted (see [`events/gate/validations`](#events_gate_validations)) the gate paddles will open for the requested direction.
Successful validations will only be accepted for directions that are `controlled`, see [`config/gate/validation`](#config_gate_validation).
#### Payload
- `id`: Unique transaction ID associated with this validation (**__string__**).
- `direction`: The direction associated with this validation (**__string__**) [`entry, exit`].
- `text` _**(optional)**_: Text indicating any additional details associated with this validation (**__string array__**).
#### Examples
```json
{
    "id": "875f1c9e-5e70-417e-a566-80351ec78947",
    "direction": "entry"
}
```
```json
{
    "id": "875f1c9e-5e70-417e-a566-80351ec78949",
    "direction": "exit"
}
```
```json
{
    "id": "875f1c9e-5e70-417e-a566-80351ec78948",
    "direction": "entry",
    "text": [
        "$1.75 Deducted",
        "Applied to Fare Cap",
        "$1.50 Balance"
    ]
}
```
---
> ### <a id="events_validator_validation_fail"></a>events/validator/validation/fail `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates, to the gate, that a connected validator has recorded a failed validation attempt for the specified direction.\
Failed validations are not acknowledged through [`events/gate/validations`](#events_gate_validations).
#### Payload
- `direction`: The direction associated with this validation (**__string__**) [`entry, exit`].
- `text` _**(optional)**_: Text detailing the cause of the rejected validation (**__string array__**).
#### Examples
```json
{
    "direction": "entry"
}
```
```json
{
    "direction": "entry",
    "text": [
        "Expired card"
    ]
}
```
---
> ### <a id="events_gate_validations"></a>events/gate/validations `SUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates acknowledgement of validations sent through [`events/validator/validation/success`](#events_validator_validation_success).
#### Payload
- `direction`: The direction associated with the validation (**__string__**) [`entry, exit`].
- `id`: The unique transaction ID associated with the validation.
- `type`: The type associated with this event (**__string__**) [`accepted, rejected, consumed, timeout`]
    - `accepted`: A validation was accepted for the specified direction.
    - `rejected`: A validation was not accepted for the specified direction.
    - `consumed`: A validation was consumed by a successful passage in the associated direction through the aisle.
    - `timeout`: The validation in the associated direction was not used within the timeout defined in [`config/gate/validation`](#config_gate_validation)
#### Example
```json
{
    "id": "875f1c9e-5e70-417e-a566-80351ec78948",
    "direction": "entry",
    "type": "accepted"
}
```
---
> ### <a id="status_gate_direction"></a>status/gate/direction `SUB` `retained` **`DEPRECATED`**<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
**Note**: This message is **`DEPRECATED`** and will be removed in a future API - as it duplicates [`status/gate/paddles`](#status_gate_paddles)

Messages indicating the direction of the paddles.
#### Payload
- `direction`: The direction the paddles are currently open (**__string__**) [`entry, exit`, `none`].
#### Example
```json
{
    "direction": "none"
}
```
---
> ### <a id="status_gate_credits"></a>status/gate/credits `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message detailing the current credits held in the gate.
#### Payload
- `entry`: The number of entry credits currently held (**__integer__**).
- `exit`: The number of exit credits currently held (**__integer__**).
#### Example
```json
{
  "entry": 1,
  "exit": 0
}
```
---
> ### <a id="control_gate_mode"></a> control/gate/mode `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message to change the Gate's operating mode - which will persist until a separate mode request is made.

**Note**: Emergency mode can be driven through this control message trigger or internally in the gate via a local IO event trigger.
Both triggers must clear before the gate will exit emergency mode - see [`status/gate/emergency`](#status_gate_emergency).

#### Payload
- `mode`: The new operating mode for the gate (**__string__**) [`emergency, maintenance, validation`]
  - `emergency`: Open paddles in the exit direction, ignoring external and sensor-based requests to open or close the paddles.
  - `maintenance`: Gate is idle, ignoring external and sensor-based requests to open or close the paddles - set to either `validation` or `emergency` to clear this mode.
  - `validation`: Normal operating mode of the gate, external and sensor-based requests to open or close the paddles are used based on validation mode, see [`config/gate/validation`](#config_gate_validation).
#### Example
```json
{
  "mode": "validation"
}
```
---
> ### <a id="status_gate_mode"></a>status/gate/mode `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message indicating a change to the Gate's operating mode.
#### Payload
- `mode`: The current mode of the gate (**__string__**) [`emergency, fault, initializing, maintenance, power_fail, updating, validation`]
    - `emergency`: Gate is in emergency operation mode - gate controls will be ignored, except mode change requests.
    - `fault`: Gate has entered a fault state preventing operation - gate controls will be ignored. See [`status/gate/faults`](#status_gate_faults)
    - `initializing`: Gate is starting up, once complete it will enter `emergency`, `maintenance` or `validation` based on configuration.
    - `maintenance`: Gate is idle - All gate functions available but ignoring external and sensor-based requests to open or close the paddles.
    - `power_fail`: Gate has entered power failure mode - Gate will begin shutdown into low power mode.
    - `updating`: Gate is running a software/firmware update - Gate controls will be ignored.
    - `validation`: Gate is in standard validation mode - this is the normal operating mode of the gate - all gate functions available and responding to external and sensor-based requests to open and close the paddles.
#### Example
```json
{
  "mode": "validation"
}
```
---
> ### <a id="status_gate_emergency"></a>status/gate/emergency `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates a change in the gate's emergency state.
#### Payload
- `local`: Whether the local emergency trigger is active - based on physical input(s) into the gate, see [`status/gate/emergencyButton`](#status_gate_emergency_button) (**__boolean__**) [`true, false`]
- `remote`: Whether the remote emergency trigger is active - based on `emergency` mode being requested in [`control/gate/mode`](#control_gate_mode) (**__boolean__**) [`true, false`]
#### Example
```json
{
    "local": false,
    "remote": false
}
```

---
> ### <a id="control_gate_reboot"></a>control/gate/reboot `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message to request a restart of the gate controller.
#### Payload
None
#### Example
```json
{}
```

---
> ### <a id="status_gate_emergency_button"></a>status/gate/emergencyButton `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates a change in the emergency button state.
#### Payload
- `active`: Whether the emergency button is active or not (**__boolean__**) [`true, false`]
#### Example
```json
{
  "active": false
}
```

---
> ### <a id="status_gate_faults"></a>status/gate/faults `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Lists all active gate faults.
Faults are split into two categories, `major` and `minor`.\
`major` faults indicate the gate has suffered a critical failure and will cause the gate to enter `fault` mode (see
[`status/gate/mode`](#status_gate_mode)).\
`minor` faults indicate the gate has suffered a failure but remains in operation, although operation may be degraded.
#### Payload
The payload consists of an array of `Fault` objects:
- `code`: Fault code associated with, and unique to, the `component` (**__string__**)
- `component`: Textual component identifier (**__string__**)
- `description` : Textual description of the fault code (**__string__**)
- `severity`: As defined above (**__string__**) [`major, minor`]
- `timestamp`: Indicates when the fault was raised (**__string__**)
#### Example
```json
{
    "faults" : [
        {"code": "1", "component": "flexchain", "description": "Flexchain disconnected", "severity": "major", "timestamp": "Jan 20 13:43:45"},
        {"code": "1", "component": "parent-motor", "description": "Comms failure", "severity": "major", "timestamp": "Jan 20 13:43:45"},
        {"code": "2", "component": "parent-motor", "description": "Voltage error", "severity": "major", "timestamp": "Jan 20 13:43:45"},
        {"code": "1", "component": "motor-control-service", "description": "Software failure", "severity": "minor", "timestamp": "Jan 20 13:43:45"}
    ]
}
```

---
> ### <a id="status_gate_paddles"></a>status/gate/paddles `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates a paddle movement status change.
Updates when the paddles have been commanded to move, once they reach their destination or when interrupted.
#### Payload
- `event`: The new position of the paddles (**__string__**) [`opening_entry, open_entry, opening_exit, open_exit, closing, closed, blocked`]
#### Example
```json
{
  "event": "closing"
}
```
```json
{
  "event": "closed"
}
```
---
> ### <a id="status_gate_paddles_forced"></a>status/gate/paddlesForced `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates a change in a gate paddle force detection - based on force and duration defined in [config/gate/motors](#config_gate_motors).\
If `active` is `true`, the gate paddles are being forced against their closed position, either pushed or pulled.
If `active` is `false`, the gates are no longer being forced and remain in their original position.
#### Payload
- `active`: Whether the paddles are currently being forced (**boolean**) [`true, false`]
- `type`: The classification of paddle force, if known (**__string__**) [`emergency, unauthorized`]
- `from`: The side of the gate that the force is being exerted from, if known (**__string__**) [`entry, exit, unknown`]
#### Example
```json
{
  "active": true,
  "type": "unauthorized",
  "from": "entry"
}
```
```json
{
    "active": false
}
```
---
> ### <a id="status_gate_panel_access"></a>status/gate/panelAccess `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates a change in the side panel access sensors, e.g. a panel has been opened.\
**Note**: If the gate is in `validation` mode (see [`status/gate/mode`](#status_gate_mode)) and a panel is opened, it
will immediately enter a `locked` configuration for safety reasons (see [`config/gate/validation`](#config_gate_validation)).
#### Payload
- `parent`: The status of the parent side panel access detection (**__string__**) [`open, closed`]
- `child`: The status of the child side panel access detection (**__string__**) [`open, closed`]
#### Example
```json
{
  "parent": "closed",
  "child": "closed"
}
```
---
> ### <a id="status_gate_power"></a>status/gate/power `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates a change in the gate's power source.
#### Payload
- `status`: The current power source in use by the gate (**__string__**) [`mains, backup`]

#### Example
```json
{
  "status": "mains"
}
```
---
> ### <a id="status_gate_sensors_blocked"></a>status/gate/sensorsBlocked `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Indicates the blocked state of the sensors in either side of the gate, based on configuration [`config/gate/sensors`](#config_gate_sensors)
#### Payload
- `entry`: Whether an entry-side sensor is blocked (**__boolean__**).
- `exit`: Whether an exit-side sensor is blocked (**__boolean__**).
#### Example
```json
{
    "entry": false,
    "exit": false
}
```
---
> ### <a id="status_gate_software"></a>status/gate/software `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Reports the software version of the gate as a single value representing all sub-systems, Gate Controller, POD and Vision.
Sent on gate startup and when software is updated. If the software versions of the sub-systems do not relate to a Gate
release version, the version will be reported as 0.0.1. This can happen on failed software updates or if a sub-system is
running an unexpected software version.
#### Payload
- `version`: The current version of the gate software (**__string__**).
#### Example
```json
{
    "version": "1.0.0"
}
```
---
> ### <a id="status_gate_stats"></a>status/gate/stats `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Reports system statistics relating to resources and performance.
#### Payload
- `time`: "Current system time" (**__string__**).
- `uptime`: The total time the system has been running without a restart (**__string__**).
- `cpuTempCelsius`: The current temperature of the CPU in degrees celsius (**__double__**).
- `cpuLoadPercent`: The percentage of the CPU's processing capacity currently being used (**__double__**).
- `memoryUsagePercent`: The percentage of the total system memory (RAM) that is currently in use (**__double__**).
- `diskUsagePercent`: The percentage of the total disk space that is currently being used (**__double__**).
- `diskFreeGB`: The amount of available free disk space on the system (**__double__**).
#### Example
```json
{
    "time": "Mon Mar 24 13:43:45 GMT 2025",
    "uptime":"6 days 21 hours 54 minutes 6 seconds",
    "cpuTempCelsius":53.0,
    "cpuLoadPercent":33.7,
    "memoryUsagePercent":58.2,
    "diskUsagePercent":28.5,
    "diskFreeGB":10.1
}
```
---
> ### <a id="status_validator_state"></a>status/validator/state `PUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Reports current state of the entry and exit validators.\
If a validator reports `out-of-service` for a `controlled` direction, the validation mode will become
`locked` (for that direction) - see [config/gate/validation](#config_gate_validation).
#### Payload
- `entry`: The state of the entry validator (**__string__**) [`out-of-service, in-service`].
- `exit`: The state of the exit validator (**__string__**) [`out-of-service, in-service`].
#### Example
```json
{
    "entry": "out-of-service",
    "exit":"in-service"
}
```

---
> ### <a id="heartbeat_gate"></a>heartbeat/gate `SUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message sent as a regular "heartbeat" to indicate the gate controller and subsystem status.
#### Payload
- `ipAddress`: External IP address of the gate (**__string__**).
- `vision`: The status of the vision subsystem (**__string__**) [`online, offline`].
- `entryPod`: The status of the entry-side POD (**__string__**) [`online, offline`].
- `exitPod`: The status of the exit-side POD (**__string__**) [`online, offline`].
#### Examples
```json
{
    "ipAddress": "10.16.225.32",
    "vision": "online",
    "entryPod": "online",
    "exitPod": "online"
}
```
---
> ### <a id="heartbeat_validator"></a>heartbeat/validator `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message as a regular "heartbeat" to indicate a validator is online for the specified gate direction.\
If a validator goes offline (indicated by a drop in heartbeat) for a `controlled` direction, the validation mode will then become `locked` (for that direction)
- see [config/gate/validation](#config_gate_validation).
#### Payload
- `id`: A unique identifier for the validator (**__string__**).
- `direction`: The direction that this validator can generate validations for (**__string__**) [`entry, exit, both`].
- `ipAddress`: The IP address of the validator (**__string__**).
#### Example
```json
{
    "id": "entry-validator-1",
    "direction": "entry",
    "ipAddress": "192.168.1.20"
}
```

---
> ### <a id="config_gate_sensors"></a>config/gate/sensors `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Defines sensor configurations.
#### Payload
- `blockedSensorDuration`: Parameters defining the detection of a blocked sensor which then set the values in [status/gate/sensorsBlocked](#status_gate_sensors_blocked).
    - `entry`: The duration a sensor on the entry side must be blocked to trigger status change, in seconds (**__integer__**) [`0-59`]
    - `exit`: The duration a sensor on the exit side must be blocked to trigger status change, in seconds (**__integer__**) [`0-59`]
    **Note**: A value of `0` in either of these parameters will disable the event.
#### Example
```json
{
  "blockedSensorDuration" : {
    "entry": 5,
    "exit": 3
  }
}
```
---
> ### <a id="status_gate_sensors"></a>status/gate/sensors `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message indicating a change to the gate's sensor configuration.\
Sent on operational mode startup and whenever [config/gate/sensors](#config_gate_sensors) is received (even if it doesn't change).
#### Payload
The payload follows the same structure as the config message, see [config/gate/sensors](#config_gate_sensors)


---
> ### <a id="config_gate_paddle_forces"></a>config/gate/paddleForces `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Defines paddle force configuration profiles.
#### Payload
- `entry`: Paddle **entry** force profile when in `validation` mode, from lowest(`1`) to highest (`5`) (**__integer__**) [`1-5`]
- `exit`: Paddle **exit** force profile when in `validation` mode, from lowest(`1`) to highest (`5`) (**__integer__**) [`1-5`]
#### Example
```json
{
  "entry": 3,
  "exit": 4
}
```
---
> ### <a id="status_gate_paddle_forces"></a> status/gate/paddleForces `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message indicating a change to the gate's paddle forces configuration.\
Sent on operational mode startup and whenever [config/gate/paddleForces](#config_gate_paddle_forces) is received (even if it doesn't change).
#### Payload
The payload follows the same structure as the config message, see [config/gate/paddleForces](#config_gate_paddle_forces)

---
> ### <a id="config_gate_paddle_speeds"></a>config/gate/paddleSpeeds `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Defines paddle speed configurations.
#### Payload
- `entry`: Speed profiles when in `validation` mode.
  - `open`: Opening-from-entry-side speed from, slowest (`1`) to fastest (`5`) (**__integer__**) [`1-5`]
  - `close`: Closing-from-entry-side speed from, slowest (`1`) to fastest (`5`) (**__integer__**) [`1-5`]
- `exit`: Speed profiles when in `validation` mode.
  - `open`: Opening-from-exit-side speed, from slowest (`1`) to fastest (`5`) (**__integer__**) [`1-5`]
  - `close`: Closing-from-exit-side speed, from slowest (`1`) to fastest (`5`) (**__integer__**) [`1-5`]
#### Example
```json
{
  "entry": {
    "open": 4,
    "close": 4
  },
  "exit": {
    "open": 4,
    "close": 4
  }
}
```

---
> ### <a id="status_gate_paddle_speeds"></a>status/gate/paddleSpeeds `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message indicating a change to the gate's paddle configuration.\
Sent on operational mode startup and whenever [`config/gate/paddleSpeeds`](#config_gate_paddle_speeds) is received (even if it doesn't change).
#### Payload
The payload follows the same structure as the config message, see [`config/gate/paddleSpeeds]`(#config_gate_paddle_speeds).


---
> ### <a id="config_gate_system"></a>config/gate/system `PUB`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Defines gate system configuration.
#### Payload
- `url`: Address used for external communications (**__string__**).
- `ntp`: NTP server address (**__string__**).
- `timeZone`: The desired time zone for the location of the gate. Available time zones are provided in the [2024a](https://ftp.iana.org/tz/tzdb-2024a/) release of the tz database. (**__string__**). [`Europe/London, Etc/UTC, America/New_York, America/Los_Angeles`, etc.]
- `placement`
  - `deviceId`: Device ID to be used in configuration (**__string__**).
  - `gateType`: The type of the gate (**__string__**) [`WAG, STD`].
  - `position`: The position of the gate in an array (**__integer__**).
  - `stationId`: Station ID to be used in configuration (**__string__**).
  - `stationName`: Station Name to be used in displays (**__string__**).
  - `substationId`: Sub station ID to be used in configuration (**__string__**).

#### Example
```json
{
  "url": "http:\\ecurl",
  "ntp": "http:\\ntp",
  "timeZone": "America/New_York",
  "placement" : {
    "deviceId": "ENG00115",
    "gateType": "WAG",
    "position": 0,
    "stationId": "1",
    "stationName": "Flare Street",
    "substationId": "5"
  }
}
```
---
> ### <a id="status_gate_system"></a>status/gate/system `SUB` `retained`<span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message indicating a change to the gate's system configuration.\
Sent on operational mode startup and whenever [config/gate/system](#config_gate_system) is received.
#### Payload
The payload follows the same structure as the config message, see [config/gate/system](#config_gate_system)


---
> ### <a id="control_gate_metrics_reset"></a>control/gate/metrics/reset `SUB` `retained` <span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Message to reset gate metrics reported by [`metrics/gate/credits`](#metrics_gate_credits) and [`metrics/gate/passage`](#metrics_gate_passage).
#### Payload
None
#### Example
```json
{}
```
---
> ### <a id="metrics_gate_credits"></a>metrics/gate/credits `SUB` `retained` <span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Current credit metrics since reset (see [`control/gate/metrics/reset`](#control_gate_metrics_reset)) or gate restart.

**Note**: The free entry/exits are included in the counts if the gate is configured for that mode (see [`config/gate/validation`](#config_gate_validation)).

**Note**: Validation credits are never `removed` - they are timed out early, e.g. if gate changes to emergency.
#### Payload
- `entry`: Credits counts for the entry direction.
- `exit`: Credits counts for the exit direction.
    - `received`: Number of credit requests received for this direction (**integer**).
    - `accepted`: Credits `received` and then accepted (**integer**).
    - `rejected`: Credit `received` and then rejected (**integer**).
    - `consumed`: Credits `accepted` and then consumed by traversal through the walkway (**integer**).
    - `removed`: Credits `accepted` and then removed by e.g. backout from `free` sides, or mode change (**integer**).
    - `timeout`: Credits `accepted` and then timed out (**integer**).
#### Example
```json
{
  "entry" : {
    "received": 0,
    "accepted": 0,
    "rejected": 0,
    "consumed": 0,
    "removed": 0,
    "timeout": 0
  },
  "exit" : {
    "received": 0,
    "accepted": 0,
    "rejected": 0,
    "consumed": 0,
    "removed": 0,
    "timeout": 0
  }
}
```
---
> ### <a id="metrics_gate_evasion"></a>metrics/gate/evasion `SUB` `retained` <span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Current evasion metrics, since reset (see [`control/gate/metrics/reset`](#control_gate_metrics_reset)) or gate restart.
#### Payload
- `entry`: Evasion counts for the entry direction.
- `exit`: Evasion counts for the exit direction.
#### Example
```json
{
  "entry" : {
    "crawlUnder": 0,
    "paddlesForced": 0,
    "panelAccess": 0,
    "unauthorisedAccess": 0
  },
  "exit" : {
    "crawlUnder": 0,
    "paddlesForced": 0,
    "panelAccess": 0,
    "unauthorisedAccess": 0
  }
}
```
---
> ### <a id="metrics_gate_paddles"></a>metrics/gate/paddles `SUB` `retained` <span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Current paddle metrics, showing paddle movements, since reset (see [`control/gate/metrics/reset`](#control_gate_metrics_reset)) or gate restart.
#### Payload
- `blocked`: The number of times the paddles were blocked when opening in the associated direction.
- `opened`: The number of times the paddles opened in the associated direction.
#### Example
```json
{
  "entry" : {
    "blocked": 0,
    "opened": 0
  },
  "exit" : {
    "blocked": 0,
    "opened": 0
  }
}
```
---
> ### <a id="metrics_gate_passage"></a>metrics/gate/passage `SUB` `retained` <span style="display: inline-block; float: right;">[^](#gate_contents)</span>
---
#### Description
Current passage metrics, showing transitions through the walkway, since reset (see [`control/gate/metrics/reset`](#control_gate_metrics_reset)) or gate restart.
#### Payload
- `entry`: Passage counts for the entry direction.
- `exit`: Passage counts for the exit direction.
#### Example
```json
{
  "entry" : {
    "bicycles": 0,
    "people": 0,
    "scooters": 0,
    "strollers": 0,
    "suitcases": 0,
    "wheelchairs": 0
  },
  "exit" : {
    "bicycles": 0,
    "people": 0,
    "scooters": 0,
    "strollers": 0,
    "suitcases": 0,
    "wheelchairs": 0
  }
}
```

##
---
> ### <a id="upload_software"></a>upload_software `POST` <span style="display: inline-block; float: right;">[^](#http_api)</span>
---
#### Description
An endpoint for uploading software update packages to the gate.
#### Request Details
- Content-Type: `application/octet-stream`
- Root URI path: `upload_software`
- Port: `8321`
- Additional: `Data should be sent in binary format`
#### Response Details
- **On success**:
  - Status code: `200 OK`
  - Body: `plain text response confirmation`
- **On failure**:
  - Status codes: `400 Bad Request`, `405 Method Not Allowed`, `413 Payload Too Large`
  - Body: `plain text response detailing the failure`
#### Example
```
curl -X POST http://<gate-ip-address>:8321/upload_software --data-binary "@file.zip"
```

##
---
> ### <a id="download_logs"></a>download_logs `GET` <span style="display: inline-block; float: right;">[^](#http_api)</span>
---
#### Description
An endpoint for requesting log files from all subsystems on the gate.

#### Request Details
- Root URI path: `download_logs`
- Port: `8323`
- Additional: `Data will be sent in a binary format`
#### Response Details
- **On success**:
    - Status code: `200 OK`
    - Body: `plain text response confirmation`
- **On failure**:
    - Status codes: `400 Bad Request`, `405 Method Not Allowed`, `413 Payload Too Large`, `422 Unprocessable Content`, `500 Internal Server Error`
    - Body: `plain text response detailing the failure`
#### Example
```
curl -o downloaded-logs.zip http://<gate-ip-address>:8323/download_logs"
```
