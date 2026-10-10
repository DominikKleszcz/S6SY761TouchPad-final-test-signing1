# S6SY761 touch-to-mouse build

This reviewed version changes the HID child exposed by the S6SY761 driver from an
absolute touchscreen to a relative HID mouse.

Behavior:
- first finger down establishes the reference point;
- moving that same finger produces relative mouse X/Y deltas;
- the initial touch position does not move the cursor;
- additional fingers are ignored;
- releasing the tracked finger stops movement;
- the physical S6SY761 I2C/SPB handling remains unchanged.

## ARM64 build

Open `S6SY761Touch.sln` in Visual Studio with the Windows Driver Kit installed.
Select:

`Release | ARM64`

Then Build -> Build Solution.

The repository already contained ARM64 project configurations. Current Microsoft
WDK releases support native ARM64 driver development beginning with WDK
10.0.26100.1.

## Important: no TESTSIGNING is enabled by this project

This source package does not turn on TESTSIGNING and does not add any boot
configuration changes.

If your modified/offline Windows already permits unsigned kernel drivers, the
resulting `.sys` can be tested without the "Test Mode" watermark.

Check the current boot configuration from an elevated Command Prompt:

`bcdedit /enum {current}`

Do not enable TESTSIGNING merely to build this driver.

If Windows refuses the driver because of code-integrity/signature policy, stop
there and report the exact error. In particular, do not randomly change BCD
options: the exact policy on the customized Windows build needs to be checked
first.

## Installation

The supplied INF still targets the original hardware ID:

`ACPI\SCSY0761`

After building, install the resulting package using an elevated Command Prompt
with:

`pnputil /add-driver "C:\path\to\S6SY761Touch.inf" /install`

If Windows rejects the package, send the exact PnPUtil error and the Device
Manager error code. Do not enable TESTSIGNING unless we explicitly decide it is
necessary.

## Report format

Input report ID: `0x54`

Bytes:
- byte 0: report ID
- byte 1: mouse buttons (always zero)
- bytes 2-3: signed relative X
- bytes 4-5: signed relative Y

The mouse descriptor contains only the HID input report; legacy Feature/Output
IOCTL handlers are left present but explicitly return STATUS_NOT_IMPLEMENTED.
This avoids advertising touchscreen-specific feature reports from a mouse collection.


## Review notes

The active report descriptor is the 55-byte mouse descriptor in `Device.c`. The old
touchscreen descriptor and its 61-byte input report structures are removed. The
input report is exactly 6 bytes including Report ID. The driver does not advertise
Feature or Output reports.
