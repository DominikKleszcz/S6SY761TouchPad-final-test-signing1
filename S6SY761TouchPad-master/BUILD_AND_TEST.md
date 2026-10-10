# S6SY761Touch — final touch-to-mouse test build

This is the final development/test variant.

Behavior:
- first finger down establishes the reference point;
- the initial touch position does not move the cursor;
- movement of that same finger produces relative mouse X/Y deltas;
- other fingers are ignored;
- release stops tracking;
- the next touch establishes a new reference.

The HID input report is 6 bytes including Report ID `0x54`.

## Build

Open a WDK/Developer Command Prompt as Administrator and run:

    msbuild S6SY761Touch.sln /p:Configuration=Release /p:Platform=ARM64 /m

## Create the test-signed package

Run:

    powershell.exe -ExecutionPolicy Bypass -File .\Prepare-Test-Package.ps1

The script builds the ARM64 driver, creates a local code-signing test certificate if necessary,
signs the SYS, generates the CAT, signs the CAT, and exports/imports the test certificate.
It does not require Internet access or a timestamp server.

## Enable test signing

Run as Administrator:

    Enable-TestSigning.cmd

Reboot Windows.

## Install

After reboot:

    Install-TestDriver.cmd

The script uses PnPUtil to stage/install the ARM64 package for the matching ACPI hardware ID.

## Disable test signing

After testing:

    Disable-TestSigning.cmd

Reboot again.

The package does not modify `nointegritychecks`, Secure Boot, or any hidden signature-enforcement bypass.
