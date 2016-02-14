XInput Joystick Library - Architecture
**************************************

Core functions:
        - Support multiple backends
        - Keep track of the state and virtual key presses of the gamepads
        - Provide a single, simple function (xiw_core_PushEvent) to the backends. The core updates the
state and adds new virtual key presses to the XInput event queue
        - Provide a simple mapping between backend and XInput value ranges, including axis-to-button
mapping and abstraction of the different value ranges of triggers and thumbpads. The interface
between the backends and the core uses a single common value range between -0x8000 and 0x7fff. The
function xiw_util_ConvToXIWValue can be used by the backends to convert any value to the common
value range.


Backend implementation:
        - A backend exports its interface to the core via an entry in the core's xinput_backends table.
        - Setting the Initialize function pointer to NULL in the XINPUTW_BACKEND structure will disable
that backend. This can be used to disable any backends not available for the current system
        - Updates to a gamepad's state are made by calling the xiw_core_PushEvent function. The core is
thread-safe, allowing for event-driven updates from separate threads. The core will also call
SyncKeyState whenever the client application requests the current state, allowing for a
polling-driven approach.
        - In an event-driven approach, the SyncKeyState function MUST NOT block while waiting for the
event thread to return from xiw_core_PushEvent. This would cause a deadlock.
        - A backend must only send updates for a certain slot if it has been allotted that slot via its
TryConnectDevice function
        - If an error occurs or the device is disconnected, the next call to SyncKeyState,
SyncBatteryState or SetRumble must return FALSE. When this happens, the core calls DisconnectDevice
to free any resources
        - A backend must not call xiw_core_PushEvent for a slot after DisconnectDevice has been called
for that slot (unless TryConnectDevice has been called again). The function DisconnectDevice may
wait for the event thread to stop.