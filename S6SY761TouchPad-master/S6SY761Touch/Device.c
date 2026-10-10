/*++

Copyright (C) Microsoft Corporation, All Rights Reserved.

SPDX-Licence-Identifier: GPL-2.0
Copyright (c) 2017  Samsung Electronics Co., Ltd.
Copyright (c) 2017  Andi Shyti <andi@etezian.org>
					Andi Shyti <andi.shyti@samsung.com>

Copyright (c) 2022 - 2023  Morc - Richard Gr·Ëik

Module Name:


	S6SY761Touch.c

Abstract:F

	This module contains the implementation of the driver

Environment:

	Windows Driver Framework (WDF)

--*/

#include "Device.h"


NTSTATUS
RequestGetHidXferPacket_ToReadFromDevice(
	_In_  WDFREQUEST        Request,
	_Out_ HID_XFER_PACKET* Packet
)
{
	NTSTATUS                status;
	WDF_REQUEST_PARAMETERS  params;

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET)) {
		status = STATUS_BUFFER_TOO_SMALL;
		KdPrint(("RequestGetHidXferPacket: invalid HID_XFER_PACKET\n"));
		return status;
	}

	RtlCopyMemory(Packet, WdfRequestWdmGetIrp(Request)->UserBuffer, sizeof(HID_XFER_PACKET));
	return STATUS_SUCCESS;
}

NTSTATUS
RequestGetHidXferPacket_ToWriteToDevice(
	_In_  WDFREQUEST        Request,
	_Out_ HID_XFER_PACKET* Packet
)
{
	NTSTATUS                status;
	WDF_REQUEST_PARAMETERS  params;

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET)) {
		status = STATUS_BUFFER_TOO_SMALL;
		KdPrint(("RequestGetHidXferPacket: invalid HID_XFER_PACKET\n"));
		return status;
	}

	RtlCopyMemory(Packet, WdfRequestWdmGetIrp(Request)->UserBuffer, sizeof(HID_XFER_PACKET));
	return STATUS_SUCCESS;
}


#define S6SY761_EVENT_SIZE  8
#define S6SY761_EVENT_COUNT  32

#define S6SY761_MASK_LEFT_EVENTS 0x37
#define S6SY761_MASK_TOUCH_STATE 0xc0
#define S6SY761_MASK_TID 0x3c
#define S6SY761_MASK_EID 0x03
#define S6SY761_MASK_X 0xf0
#define S6SY761_MASK_Y 0x0f

#define S6SY761_TS_NONE 0x00
#define S6SY761_TS_PRESS 0x01
#define S6SY761_TS_MOVE 0x02
#define S6SY761_TS_RELEASE 0x03

/* event id */
#define S6SY761_EVENT_ID_COORDINATE	0x00
#define S6SY761_EVENT_ID_STATUS		0x01

BYTE S6SY761_READ_ALL_EVENTS[1] = { 0x61 }; //read all events
BYTE S6SY761_READ_ONE_EVENT[1] = { 0x60 }; //read single event
BYTE S6SY761_CLEAR_EVENT_STACK[1] = { 0x62 }; //drain all last commands
BYTE S6SY761_SENSE_ON[1] = { 0x10 }; //enable sensing

UINT8 eventbuf[S6SY761_EVENT_SIZE * S6SY761_EVENT_COUNT];


#define DEFINE_GUID2(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID DECLSPEC_SELECTANY name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }





typedef struct __declspec(align(2))
{
	BYTE  ReportId;
	BYTE  Buttons;
	SHORT X;
	SHORT Y;
} mouseInputReport_t;

C_ASSERT(sizeof(mouseInputReport_t) == 6);

//
// HID report descriptor: expose the S6SY761 as a relative mouse.
// Report ID 0x54 is kept for compatibility with the existing driver stack.
//
HID_REPORT_DESCRIPTOR G_DefaultReportDescriptor[] = {
    0x05, 0x01,             // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,             // USAGE (Mouse)
    0xA1, 0x01,             // COLLECTION (Application)
    0x85, 0x54,             // REPORT_ID (0x54)

    0x09, 0x01,             // USAGE (Pointer)
    0xA1, 0x00,             // COLLECTION (Physical)

    0x05, 0x09,             // USAGE_PAGE (Button)
    0x19, 0x01,             // USAGE_MINIMUM (Button 1)
    0x29, 0x03,             // USAGE_MAXIMUM (Button 3)
    0x15, 0x00,             // LOGICAL_MINIMUM (0)
    0x25, 0x01,             // LOGICAL_MAXIMUM (1)
    0x95, 0x03,             // REPORT_COUNT (3)
    0x75, 0x01,             // REPORT_SIZE (1)
    0x81, 0x02,             // INPUT (Data,Var,Abs)
    0x95, 0x01,             // REPORT_COUNT (1)
    0x75, 0x05,             // REPORT_SIZE (5)
    0x81, 0x03,             // INPUT (Const,Var,Abs)

    0x05, 0x01,             // USAGE_PAGE (Generic Desktop)
    0x09, 0x30,             // USAGE (X)
    0x09, 0x31,             // USAGE (Y)
    0x16, 0x00, 0x80,       // LOGICAL_MINIMUM (-32768)
    0x26, 0xFF, 0x7F,       // LOGICAL_MAXIMUM (32767)
    0x75, 0x10,             // REPORT_SIZE (16)
    0x95, 0x02,             // REPORT_COUNT (2)
    0x81, 0x06,             // INPUT (Data,Var,Rel)

    0xC0,                   // END_COLLECTION (Physical)
    0xC0                    // END_COLLECTION (Application)
};

//
// This is the default HID descriptor returned by the mini driver
// in response to IOCTL_HID_GET_DEVICE_DESCRIPTOR. The size
// of report descriptor is currently the size of G_DefaultReportDescriptor.
//

HID_DESCRIPTOR              G_DefaultHidDescriptor = {
	0x09,   // length of HID descriptor
	0x21,   // descriptor type == HID  0x21
	0x0100, // hid spec release
	0x00,   // country code == Not Specified
	0x01,   // number of HID class descriptors
	{                                       //DescriptorList[0]
		0x22,                               //report descriptor type 0x22
		sizeof(G_DefaultReportDescriptor)   //total length of report descriptor
	}
};

NTSTATUS
DriverEntry(
	_In_  PDRIVER_OBJECT    DriverObject,
	_In_  PUNICODE_STRING   RegistryPath
)
/*++

Routine Description:
	DriverEntry initializes the driver and is the first routine called by the
	system after the driver is loaded. DriverEntry specifies the other entry
	points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

	DriverObject - represents the instance of the function driver that is loaded
	into memory. DriverEntry must initialize members of DriverObject before it
	returns to the caller. DriverObject is allocated by the system before the
	driver is loaded, and it is released by the system after the system unloads
	the function driver from memory.

	RegistryPath - represents the driver specific path in the Registry.
	The function driver can use the path to store driver related data between
	reboots. The path does not store hardware instance specific data.

Return Value:

	STATUS_SUCCESS, or another status value for which NT_SUCCESS(status) equals
					TRUE if successful,

	STATUS_UNSUCCESSFUL, or another status for which NT_SUCCESS(status) equals
					FALSE otherwise.

--*/
{
	WDF_DRIVER_CONFIG       config;
	WDF_OBJECT_ATTRIBUTES driverAttributes;
	NTSTATUS                status;

#ifdef _KERNEL_MODE
	//
	// Opt-in to using non-executable pool memory on Windows 8 and later.
	// https://msdn.microsoft.com/en-us/library/windows/hardware/hh920402(v=vs.85).aspx
	//
	ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
#endif

	DbgPrint("fdsfdsa");
	KdPrint(("dfdfsdf"));

	WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&driverAttributes);
	driverAttributes.EvtCleanupCallback = EvtDriverCleanup;

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&driverAttributes,
		&config,
		WDF_NO_HANDLE);
	if (!NT_SUCCESS(status)) {

		goto Exit;
	}
Exit:
	return status;
}
VOID
EvtDriverCleanup(
	_In_ WDFOBJECT Object
)
{
	UNREFERENCED_PARAMETER(Object);
}
NTSTATUS
EvtDeviceAdd(
	_In_  WDFDRIVER         Driver,
	_Inout_ PWDFDEVICE_INIT DeviceInit
)
/*++
Routine Description:

	EvtDeviceAdd is called by the framework in response to AddDevice
	call from the PnP manager. We create and initialize a device object to
	represent a new instance of the device.

Arguments:

	Driver - Handle to a framework driver object created in DriverEntry

	DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

	NTSTATUS

--*/
{
	NTSTATUS                status;
	WDF_OBJECT_ATTRIBUTES   deviceAttributes;
	WDFDEVICE               device;
	PDEVICE_CONTEXT         deviceContext;
	PHID_DEVICE_ATTRIBUTES  hidAttributes;
	UNREFERENCED_PARAMETER(Driver);

	WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

	pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
	pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
	pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
	pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;


	//
	// Mark ourselves as a filter, which also relinquishes power policy ownership
	//
	WdfFdoInitSetFilter(DeviceInit);

	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
		&deviceAttributes,
		DEVICE_CONTEXT);

	status = WdfDeviceCreate(&DeviceInit,
		&deviceAttributes,
		&device);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	deviceContext = GetDeviceContext(device);
	deviceContext->Device = device;
	deviceContext->DeviceData = 0;
	deviceContext->MouseTracking = FALSE;
	deviceContext->MouseTouchId = 0;
	deviceContext->MouseLastX = 0;
	deviceContext->MouseLastY = 0;
	deviceContext->MousePendingDeltaX = 0;
	deviceContext->MousePendingDeltaY = 0;

	hidAttributes = &deviceContext->HidDeviceAttributes;
	RtlZeroMemory(hidAttributes, sizeof(HID_DEVICE_ATTRIBUTES));
	hidAttributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
	hidAttributes->VendorID = HIDMINI_VID;
	hidAttributes->ProductID = HIDMINI_PID;
	hidAttributes->VersionNumber = HIDMINI_VERSION;

	status = QueueCreate(device,
		&deviceContext->DefaultQueue);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = ManualQueueCreate(device,
		&deviceContext->ManualQueue);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	//
	// Use default "HID Descriptor" (hardcoded). We will set the
	// wReportLength memeber of HID descriptor when we read the
	// the report descriptor either from registry or the hard-coded
	// one.
	//
	deviceContext->HidDescriptor = G_DefaultHidDescriptor;

	//
	// The mouse report descriptor is fixed in this driver.
	// Legacy absolute-touchscreen registry values are intentionally ignored.
	//
	deviceContext->ReportDescriptor = G_DefaultReportDescriptor;
	status = STATUS_SUCCESS;

	return status;
}

NTSTATUS
PowerSettingCallback(
	_In_ LPCGUID SettingGuid,
	_In_ PVOID Value,
	_In_ ULONG ValueLength,
	_Inout_opt_ PVOID Context
)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT devContext = NULL;
	UNREFERENCED_PARAMETER(SettingGuid);
	UNREFERENCED_PARAMETER(ValueLength);
	UNREFERENCED_PARAMETER(Value);

	DbgPrint("PowerSettingsCallback");

	if (Context == NULL)
	{
		DbgPrint(
			"TchPowerSettingCallback: Context is NULL"
		);

		status = STATUS_INVALID_DEVICE_REQUEST;
		goto exit;
	}

	devContext = (PDEVICE_CONTEXT)Context;
	

exit:
	return status;
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

	Routine Description:

	This routine caches the SPB resource connection ID.

	Arguments:

	FxDevice - a handle to the framework device object
	FxResourcesRaw - list of translated hardware resources that
		the PnP manager has assigned to the device
	FxResourcesTranslated - list of raw hardware resources that
		the PnP manager has assigned to the device

	Return Value:

	Status

--*/
{
	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	BOOLEAN fInterruptResourceFound = FALSE;
	ULONG interruptIndex = 0;
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:

			//
			// Look for I2C or SPI resource and save connection ID.
			//

			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;

			if ((Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL) &&
				((Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)))
			{
				if (fSpbResourceFound == FALSE)
				{
					pDevice->PeripheralId.LowPart =
						pDescriptor->u.Connection.IdLowPart;
					pDevice->PeripheralId.HighPart =
						pDescriptor->u.Connection.IdHighPart;

					fSpbResourceFound = TRUE;
				}
			}

			break;

		case CmResourceTypeInterrupt:

			if (fInterruptResourceFound == FALSE)
			{
				fInterruptResourceFound = TRUE;
				interruptIndex = i;
			}
			break;

		default:

			//
			// Ignoring all other resource types.
			//

			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}


	//POWER STUFF
	/*status = PoRegisterPowerSettingCallback(
		NULL,
		&GUID_ACDC_POWER_SOURCE,
		PowerSettingCallback,
		pDevice,
		&pDevice->PoFxPowerSettingCallbackHandle1
	);*/

	if (!NT_SUCCESS(status))
	{
		DbgPrint("Error registering power setting callback (1) - 0x%08lX", status);
	}

	if (NT_SUCCESS(status))
	{
		DbgPrint("Successfully registered power setting callback (1) - 0x%08lX", status);
	}

	/*status = PoRegisterPowerSettingCallback(
		NULL,
		&GUID_CONSOLE_DISPLAY_STATE,
		PowerSettingCallback,
		pDevice,
		&pDevice->PoFxPowerSettingCallbackHandle2
	);*/

	if (!NT_SUCCESS(status))
	{
		DbgPrint("Error registering power setting callback (2) - 0x%08lX", status);

	}

	if (NT_SUCCESS(status))
	{
		DbgPrint("Successfully registered power setting callback (2) - 0x%08lX", status);
	}


	//
	// Create the interrupt if an interrupt
	// resource was found.
	//

	if (NT_SUCCESS(status))
	{
		if (fInterruptResourceFound == TRUE)
		{
			WDF_INTERRUPT_CONFIG interruptConfig;
			WDF_INTERRUPT_CONFIG_INIT(
				&interruptConfig,
				OnInterruptIsr,
				NULL);

			interruptConfig.PassiveHandling = TRUE;
			interruptConfig.InterruptTranslated = WdfCmResourceListGetDescriptor(
				FxResourcesTranslated,
				interruptIndex);
			interruptConfig.InterruptRaw = WdfCmResourceListGetDescriptor(
				FxResourcesRaw,
				interruptIndex);

			status = WdfInterruptCreate(
				pDevice->Device,
				&interruptConfig,
				WDF_NO_OBJECT_ATTRIBUTES,
				&pDevice->Interrupt);

			if (!NT_SUCCESS(status))
			{
			}

			if (NT_SUCCESS(status))
			{
				WdfInterruptDisable(pDevice->Interrupt);
			}
		}
	}

	DbgPrint("interruptIndex %d\n", interruptIndex);

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

	Routine Description:

	Arguments:

	FxDevice - a handle to the framework device object
	FxResourcesTranslated - list of raw hardware resources that
		the PnP manager has assigned to the device

	Return Value:

	Status

--*/
{
	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	if (pDevice->Interrupt != NULL)
	{
		WdfObjectDelete(pDevice->Interrupt);
	}

	return status;
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

	Routine Description:

	This routine allocates objects needed by the driver.

	Arguments:

	FxDevice - a handle to the framework device object
	FxPreviousState - previous power state

	Return Value:

	Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status;

	//
	// Create the SPB target.
	//

	WDF_OBJECT_ATTRIBUTES targetAttributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&targetAttributes);

	status = WdfIoTargetCreate(
		pDevice->Device,
		&targetAttributes,
		&pDevice->SpbController);

	if (!NT_SUCCESS(status))
	{
	}

	SpbDeviceOpen(pDevice);

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

	Routine Description:

	This routine destroys objects needed by the driver.

	Arguments:

	FxDevice - a handle to the framework device object
	FxPreviousState - previous power state

	Return Value:

	Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);

	SpbDeviceClose(pDevice);

	if (pDevice->SpbController != WDF_NO_HANDLE)
	{
		WdfObjectDelete(pDevice->SpbController);
		pDevice->SpbController = WDF_NO_HANDLE;
	}

	return STATUS_SUCCESS;
}

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtIoDeviceControl;


NTSTATUS
QueueCreate(
	_In_  WDFDEVICE         Device,
	_Out_ WDFQUEUE* Queue
)
/*++
Routine Description:

	This function creates a default, parallel I/O queue to proces IOCTLs
	from hidclass.sys.

Arguments:

	Device - Handle to a framework device object.

	Queue - Output pointer to a framework I/O queue handle, on success.

Return Value:

	NTSTATUS

--*/
{
	NTSTATUS                status;
	WDF_IO_QUEUE_CONFIG     queueConfig;
	WDF_OBJECT_ATTRIBUTES   queueAttributes;
	WDFQUEUE                queue;
	PQUEUE_CONTEXT          queueContext;

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
		&queueConfig,
		WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = EvtIoDeviceControl;


	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
		&queueAttributes,
		QUEUE_CONTEXT);

	status = WdfIoQueueCreate(
		Device,
		&queueConfig,
		&queueAttributes,
		&queue);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	queueContext = GetQueueContext(queue);
	queueContext->Queue = queue;
	queueContext->DeviceContext = GetDeviceContext(Device);
	queueContext->OutputReport = 0;

	*Queue = queue;

	return status;
}

VOID
EvtIoDeviceControl(
	_In_  WDFQUEUE          Queue,
	_In_  WDFREQUEST        Request,
	_In_  size_t            OutputBufferLength,
	_In_  size_t            InputBufferLength,
	_In_  ULONG             IoControlCode
)
/*++
Routine Description:

	This event callback function is called when the driver receives an

	(KMDF) IOCTL_HID_Xxx code when handlng IRP_MJ_INTERNAL_DEVICE_CONTROL
	(UMDF) IOCTL_HID_Xxx, IOCTL_UMDF_HID_Xxx when handling IRP_MJ_DEVICE_CONTROL

Arguments:

	Queue - A handle to the queue object that is associated with the I/O request

	Request - A handle to a framework request object.

	OutputBufferLength - The length, in bytes, of the request's output buffer,
			if an output buffer is available.

	InputBufferLength - The length, in bytes, of the request's input buffer, if
			an input buffer is available.

	IoControlCode - The driver or system defined IOCTL associated with the request

Return Value:

	NTSTATUS

--*/
{
	NTSTATUS                status;
	BOOLEAN                 completeRequest = TRUE;
	WDFDEVICE               device = WdfIoQueueGetDevice(Queue);
	PDEVICE_CONTEXT         deviceContext = NULL;
	PQUEUE_CONTEXT          queueContext = GetQueueContext(Queue);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	deviceContext = GetDeviceContext(device);

	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:   // METHOD_NEITHER
		//
		// Retrieves the device's HID descriptor.
		//
		_Analysis_assume_(deviceContext->HidDescriptor.bLength != 0);
		status = RequestCopyFromBuffer(Request,
			&deviceContext->HidDescriptor,
			deviceContext->HidDescriptor.bLength);
		break;

	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:   // METHOD_NEITHER
		//
		//Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
		//
		status = RequestCopyFromBuffer(Request,
			&queueContext->DeviceContext->HidDeviceAttributes,
			sizeof(HID_DEVICE_ATTRIBUTES));
		break;

	case IOCTL_HID_GET_REPORT_DESCRIPTOR:   // METHOD_NEITHER
		//
		//Obtains the report descriptor for the HID device.
		//
		status = RequestCopyFromBuffer(Request,
			deviceContext->ReportDescriptor,
			deviceContext->HidDescriptor.DescriptorList[0].wReportLength);
		break;

	case IOCTL_HID_READ_REPORT:             // METHOD_NEITHER
		//
		// Returns a report from the device into a class driver-supplied
		// buffer.
		//
		status = ReadReport(queueContext, Request, &completeRequest);
		break;

	case IOCTL_HID_WRITE_REPORT:            // METHOD_NEITHER
		//
		// Transmits a class driver-supplied report to the device.
		//
		status = WriteReport(queueContext, Request);
		break;

	case IOCTL_HID_GET_FEATURE:             // METHOD_OUT_DIRECT

		status = GetFeature(queueContext, Request);
		break;

	case IOCTL_HID_SET_FEATURE:             // METHOD_IN_DIRECT

		status = SetFeature(queueContext, Request);
		break;

	case IOCTL_HID_GET_INPUT_REPORT:        // METHOD_OUT_DIRECT

		status = GetInputReport(queueContext, Request);
		break;

	case IOCTL_HID_SET_OUTPUT_REPORT:       // METHOD_IN_DIRECT

		status = SetOutputReport(queueContext, Request);
		break;


	case IOCTL_HID_GET_STRING:                      // METHOD_NEITHER

		status = GetString(Request);
		break;

	case IOCTL_HID_GET_INDEXED_STRING:              // METHOD_OUT_DIRECT

		status = GetIndexedString(Request);
		break;

	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:  // METHOD_NEITHER
		//
		// This has the USBSS Idle notification callback. If the lower driver
		// can handle it (e.g. USB stack can handle it) then pass it down
		// otherwise complete it here as not inplemented. For a virtual
		// device, idling is not needed.
		//
		// Not implemented. fall through...
		//
	case IOCTL_HID_ACTIVATE_DEVICE:                 // METHOD_NEITHER
	case IOCTL_HID_DEACTIVATE_DEVICE:               // METHOD_NEITHER
	case IOCTL_GET_PHYSICAL_DESCRIPTOR:             // METHOD_OUT_DIRECT
		//
		// We don't do anything for these IOCTLs but some minidrivers might.
		//
		// Not implemented. fall through...
		//
	default:
		status = STATUS_NOT_IMPLEMENTED;
		break;
	}

	//
	// Complete the request. Information value has already been set by request
	// handlers.
	//
	if (completeRequest) {
		WdfRequestComplete(Request, status);
	}
}

NTSTATUS
RequestCopyFromBuffer(
	_In_  WDFREQUEST        Request,
	_In_  PVOID             SourceBuffer,
	_When_(NumBytesToCopyFrom == 0, __drv_reportError(NumBytesToCopyFrom cannot be zero))
	_In_  size_t            NumBytesToCopyFrom
)
/*++

Routine Description:

	A helper function to copy specified bytes to the request's output memory

Arguments:

	Request - A handle to a framework request object.

	SourceBuffer - The buffer to copy data from.

	NumBytesToCopyFrom - The length, in bytes, of data to be copied.

Return Value:

	NTSTATUS

--*/
{
	NTSTATUS                status;
	WDFMEMORY               memory;
	size_t                  outputBufferLength;

	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WdfMemoryGetBuffer(memory, &outputBufferLength);
	if (outputBufferLength < NumBytesToCopyFrom) {
		status = STATUS_INVALID_BUFFER_SIZE;
		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0,
		SourceBuffer,
		NumBytesToCopyFrom);
	if (!NT_SUCCESS(status)) {

		return status;
	}
	WdfRequestSetInformation(Request, NumBytesToCopyFrom);
	return status;
}

NTSTATUS
ReadReport(
	_In_  PQUEUE_CONTEXT    QueueContext,
	_In_  WDFREQUEST        Request,
	_Always_(_Out_)
	BOOLEAN* CompleteRequest
)
/*++

Routine Description:

	Handles IOCTL_HID_READ_REPORT for the HID collection. Normally the request
	will be forwarded to a manual queue for further process. In that case, the
	caller should not try to complete the request at this time, as the request
	will later be retrieved back from the manually queue and completed there.
	However, if for some reason the forwarding fails, the caller still need
	to complete the request with proper error code immediately.

Arguments:

	QueueContext - The object context associated with the queue

	Request - Pointer to  Request Packet.

	CompleteRequest - A boolean output value, indicating whether the caller
			should complete the request or not

Return Value:

	NT status code.

--*/
{
	NTSTATUS                status;

	//
	// forward the request to manual queue
	//
	status = WdfRequestForwardToIoQueue(
		Request,
		QueueContext->DeviceContext->ManualQueue);
	if (!NT_SUCCESS(status)) {
		*CompleteRequest = TRUE;
	}
	else {
		*CompleteRequest = FALSE;
	}

	return status;
}

NTSTATUS
WriteReport(
	_In_  PQUEUE_CONTEXT    QueueContext,
	_In_  WDFREQUEST        Request
)
/*++

Routine Description:

	Handles IOCTL_HID_WRITE_REPORT all the collection.

Arguments:

	QueueContext - The object context associated with the queue

	Request - Pointer to  Request Packet.

Return Value:

	NT status code.

--*/

{
	NTSTATUS                status;
	HID_XFER_PACKET         packet;
	ULONG                   reportSize;
	PHIDMINI_OUTPUT_REPORT  outputReport;

	status = RequestGetHidXferPacket_ToWriteToDevice(
		Request,
		&packet);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	if (packet.reportId != CONTROL_COLLECTION_REPORT_ID) {
		//
		// Return error for unknown collection
		//
		status = STATUS_INVALID_PARAMETER;
		return status;
	}

	//
	// before touching buffer make sure buffer is big enough.
	//
	reportSize = sizeof(HIDMINI_OUTPUT_REPORT);

	if (packet.reportBufferLen < reportSize) {
		status = STATUS_INVALID_BUFFER_SIZE;

		return status;
	}

	outputReport = (PHIDMINI_OUTPUT_REPORT)packet.reportBuffer;

	//
	// Store the device data in device extension.
	//
	QueueContext->DeviceContext->DeviceData = outputReport->Data;

	//
	// set status and information
	//
	WdfRequestSetInformation(Request, reportSize);
	return status;
}


NTSTATUS
GetFeature(
    _In_  PQUEUE_CONTEXT    QueueContext,
    _In_  WDFREQUEST        Request
)
{
    UNREFERENCED_PARAMETER(QueueContext);
    UNREFERENCED_PARAMETER(Request);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
SetFeature(
    _In_  PQUEUE_CONTEXT    QueueContext,
    _In_  WDFREQUEST        Request
)
{
    UNREFERENCED_PARAMETER(QueueContext);
    UNREFERENCED_PARAMETER(Request);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
GetInputReport(
	_In_  PQUEUE_CONTEXT    QueueContext,
	_In_  WDFREQUEST        Request
)
/*++

Routine Description:

	Handles IOCTL_HID_GET_INPUT_REPORT for all the collection.

Arguments:

	QueueContext - The object context associated with the queue

	Request - Pointer to Request Packet.

Return Value:

	NT status code.

--*/
{
	NTSTATUS                status;
	HID_XFER_PACKET         packet;
	ULONG                   reportSize;
	mouseInputReport_t*      reportBuffer;

	UNREFERENCED_PARAMETER(QueueContext);

	status = RequestGetHidXferPacket_ToReadFromDevice(
		Request,
		&packet);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	if (packet.reportId != CONTROL_COLLECTION_REPORT_ID) {
		//
		// If collection ID is not for control collection then handle
		// this request just as you would for a regular collection.
		//
		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	reportSize = sizeof(mouseInputReport_t);
	if (packet.reportBufferLen < reportSize) {
		status = STATUS_INVALID_BUFFER_SIZE;

		return status;
	}

	reportBuffer = (mouseInputReport_t*)(packet.reportBuffer);

	RtlZeroMemory(reportBuffer, sizeof(*reportBuffer));
	reportBuffer->ReportId = CONTROL_COLLECTION_REPORT_ID;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, reportSize);
	return status;
}


NTSTATUS
SetOutputReport(
	_In_  PQUEUE_CONTEXT    QueueContext,
	_In_  WDFREQUEST        Request
)
/*++

Routine Description:

	Handles IOCTL_HID_SET_OUTPUT_REPORT for all the collection.

Arguments:

	QueueContext - The object context associated with the queue

	Request - Pointer to Request Packet.

Return Value:

	NT status code.

--*/
{
	NTSTATUS                status;
	HID_XFER_PACKET         packet;
	ULONG                   reportSize;
	PHIDMINI_OUTPUT_REPORT  reportBuffer;

	status = RequestGetHidXferPacket_ToWriteToDevice(
		Request,
		&packet);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	if (packet.reportId != CONTROL_COLLECTION_REPORT_ID) {
		//
		// If collection ID is not for control collection then handle
		// this request just as you would for a regular collection.
		//
		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	//
	// before touching buffer make sure buffer is big enough.
	//
	reportSize = sizeof(HIDMINI_OUTPUT_REPORT);

	if (packet.reportBufferLen < reportSize) {
		status = STATUS_INVALID_BUFFER_SIZE;
		return status;
	}

	reportBuffer = (PHIDMINI_OUTPUT_REPORT)packet.reportBuffer;

	QueueContext->OutputReport = reportBuffer->Data;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, reportSize);
	return status;
}


NTSTATUS
GetStringId(
	_In_  WDFREQUEST        Request,
	_Out_ ULONG* StringId,
	_Out_ ULONG* LanguageId
)
/*++

Routine Description:

	Helper routine to decode IOCTL_HID_GET_INDEXED_STRING and IOCTL_HID_GET_STRING.

Arguments:

	Request - Pointer to Request Packet.

Return Value:

	NT status code.

--*/
{
	NTSTATUS                status;
	ULONG                   inputValue;

	WDF_REQUEST_PARAMETERS  requestParameters;

	//
	// IOCTL_HID_GET_STRING:                      // METHOD_NEITHER
	// IOCTL_HID_GET_INDEXED_STRING:              // METHOD_OUT_DIRECT
	//
	// The string id (or string index) is passed in Parameters.DeviceIoControl.
	// Type3InputBuffer. However, Parameters.DeviceIoControl.InputBufferLength
	// was not initialized by hidclass.sys, therefore trying to access the
	// buffer with WdfRequestRetrieveInputMemory will fail
	//
	// Another problem with IOCTL_HID_GET_INDEXED_STRING is that METHOD_OUT_DIRECT
	// expects the input buffer to be Irp->AssociatedIrp.SystemBuffer instead of
	// Type3InputBuffer. That will also fail WdfRequestRetrieveInputMemory.
	//
	// The solution to the above two problems is to get Type3InputBuffer directly
	//
	// Also note that instead of the buffer's content, it is the buffer address
	// that was used to store the string id (or index)
	//

	WDF_REQUEST_PARAMETERS_INIT(&requestParameters);
	WdfRequestGetParameters(Request, &requestParameters);

	inputValue = PtrToUlong(
		requestParameters.Parameters.DeviceIoControl.Type3InputBuffer);

	status = STATUS_SUCCESS;

	//
	// The least significant two bytes of the INT value contain the string id.
	//
	*StringId = (inputValue & 0x0ffff);

	//
	// The most significant two bytes of the INT value contain the language
	// ID (for example, a value of 1033 indicates English).
	//
	*LanguageId = (inputValue >> 16);
	return status;
}


NTSTATUS
GetIndexedString(
	_In_  WDFREQUEST        Request
)
/*++

Routine Description:

	Handles IOCTL_HID_GET_INDEXED_STRING

Arguments:

	Request - Pointer to Request Packet.

Return Value:

	NT status code.

--*/
{
	NTSTATUS                status;
	ULONG                   languageId, stringIndex;

	status = GetStringId(Request, &stringIndex, &languageId);

	// While we don't use the language id, some minidrivers might.
	//
	UNREFERENCED_PARAMETER(languageId);

	if (NT_SUCCESS(status)) {

		if (stringIndex != VHIDMINI_DEVICE_STRING_INDEX)
		{
			status = STATUS_INVALID_PARAMETER;

			return status;
		}

		status = RequestCopyFromBuffer(Request, VHIDMINI_DEVICE_STRING, sizeof(VHIDMINI_DEVICE_STRING));
	}
	return status;
}


NTSTATUS
GetString(
	_In_  WDFREQUEST        Request
)
/*++

Routine Description:

	Handles IOCTL_HID_GET_STRING.

Arguments:

	Request - Pointer to Request Packet.

Return Value:

	NT status code.

--*/
{
	NTSTATUS                status;
	ULONG                   languageId, stringId;
	size_t                  stringSizeCb;
	PWSTR                   string;

	status = GetStringId(Request, &stringId, &languageId);

	// While we don't use the language id, some minidrivers might.
	//
	UNREFERENCED_PARAMETER(languageId);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	switch (stringId) {
	case HID_STRING_ID_IMANUFACTURER:
		stringSizeCb = sizeof(VHIDMINI_MANUFACTURER_STRING);
		string = VHIDMINI_MANUFACTURER_STRING;
		break;
	case HID_STRING_ID_IPRODUCT:
		stringSizeCb = sizeof(VHIDMINI_PRODUCT_STRING);
		string = VHIDMINI_PRODUCT_STRING;
		break;
	case HID_STRING_ID_ISERIALNUMBER:
		stringSizeCb = sizeof(VHIDMINI_SERIAL_NUMBER_STRING);
		string = VHIDMINI_SERIAL_NUMBER_STRING;
		break;
	default:
		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	status = RequestCopyFromBuffer(Request, string, stringSizeCb);
	return status;
}


NTSTATUS
ManualQueueCreate(
	_In_  WDFDEVICE         Device,
	_Out_ WDFQUEUE* Queue
)
/*++
Routine Description:

	This function creates a manual I/O queue to receive IOCTL_HID_READ_REPORT
	forwarded from the device's default queue handler.

	It also creates a periodic timer to check the queue and complete any pending
	request with data from the device. Here timer expiring is used to simulate
	a hardware event that new data is ready.

	The workflow is like this:

	- Hidclass.sys sends an ioctl to the miniport to read input report.

	- The request reaches the driver's default queue. As data may not be avaiable
	  yet, the request is forwarded to a second manual queue temporarily.

	- Later when data is ready (as simulated by timer expiring), the driver
	  checks for any pending request in the manual queue, and then completes it.

	- Hidclass gets notified for the read request completion and return data to
	  the caller.

	On the other hand, for IOCTL_HID_WRITE_REPORT request, the driver simply
	sends the request to the hardware (as simulated by storing the data at
	DeviceContext->DeviceData) and completes the request immediately. There is
	no need to use another queue for write operation.

Arguments:

	Device - Handle to a framework device object.

	Queue - Output pointer to a framework I/O queue handle, on success.

Return Value:

	NTSTATUS

--*/
{
	NTSTATUS                status;
	WDF_IO_QUEUE_CONFIG     queueConfig;
	WDF_OBJECT_ATTRIBUTES   queueAttributes;
	WDFQUEUE                queue;
	PMANUAL_QUEUE_CONTEXT   queueContext;

	WDF_IO_QUEUE_CONFIG_INIT(
		&queueConfig,
		WdfIoQueueDispatchManual);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
		&queueAttributes,
		MANUAL_QUEUE_CONTEXT);

	status = WdfIoQueueCreate(
		Device,
		&queueConfig,
		&queueAttributes,
		&queue);

	if (!NT_SUCCESS(status)) {


		return status;
	}

	queueContext = GetManualQueueContext(queue);
	queueContext->Queue = queue;
	queueContext->DeviceContext = GetDeviceContext(Device);

	*Queue = queue;
	return status;
}

void
EvtTimerFunc(
	_In_  WDFTIMER          Timer
)
/*++
Routine Description:

	This periodic timer callback routine checks the device's manual queue and
	completes any pending request with data from the device.

Arguments:

	Timer - Handle to a timer object that was obtained from WdfTimerCreate.

Return Value:

	VOID

--*/
{
	NTSTATUS                status;
	WDFQUEUE                queue;
	PMANUAL_QUEUE_CONTEXT   queueContext;
	WDFREQUEST              request;
	mouseInputReport_t    readReport;

	queue = (WDFQUEUE)WdfTimerGetParentObject(Timer);
	queueContext = GetManualQueueContext(queue);

	//
	// see if we have a request in manual queue
	//
	status = WdfIoQueueRetrieveNextRequest(
		queueContext->Queue,
		&request);

	if (NT_SUCCESS(status)) {

		RtlZeroMemory(&readReport, sizeof(readReport));
		readReport.ReportId = CONTROL_COLLECTION_REPORT_ID;

		status = RequestCopyFromBuffer(request,
			&readReport,
			sizeof(readReport));

		WdfRequestComplete(request, status);
	}
}
BOOLEAN
OnInterruptIsr(
    _In_  WDFINTERRUPT FxInterrupt,
    _In_  ULONG        MessageID
)
/*++

  Routine Description:

    Read S6SY761 touch events and expose them as relative mouse movement.

    The first finger press establishes a reference point. While that same
    finger remains down, movement is converted to relative X/Y mouse deltas.
    Additional fingers are ignored until the tracked finger is released.

--*/
{
    WDFDEVICE device;
    PDEVICE_CONTEXT pDevice;
    int remain, x, y;
    NTSTATUS status;
    WDFREQUEST request;
    mouseInputReport_t readReport;
    BYTE touchType, touchId;
    UINT8 event_id, *event;

    UNREFERENCED_PARAMETER(MessageID);

    device = WdfInterruptGetDevice(FxInterrupt);
    pDevice = GetDeviceContext(device);

    status = SpbDeviceWriteRead(
        pDevice,
        S6SY761_READ_ONE_EVENT,
        eventbuf,
        1,
        S6SY761_EVENT_SIZE);

    if (!NT_SUCCESS(status) || !eventbuf[0]) {
        return TRUE;
    }

    remain = eventbuf[7] & S6SY761_MASK_LEFT_EVENTS;
    if (remain > S6SY761_EVENT_COUNT - 1) {
        return TRUE;
    }

    if (remain) {
        status = SpbDeviceWriteRead(
            pDevice,
            S6SY761_READ_ALL_EVENTS,
            eventbuf + S6SY761_EVENT_SIZE,
            1,
            remain * S6SY761_EVENT_SIZE);

        if (!NT_SUCCESS(status)) {
            return TRUE;
        }
    }

    /*
     * Consume every event before looking for a pending HID read request.
     * This lets us accumulate movement if hidclass has not posted a read
     * request at the exact instant the interrupt arrives.
     */
    for (int i = 0; i <= remain; ++i)
    {
        event = &eventbuf[i * S6SY761_EVENT_SIZE];
        event_id = event[0] & S6SY761_MASK_EID;

        if (!event[0]) {
            break;
        }

        if (event_id != S6SY761_EVENT_ID_COORDINATE) {
            continue;
        }

        if (!(event[0] & S6SY761_MASK_TID)) {
            continue;
        }

        touchId = (BYTE)(((event[0] & S6SY761_MASK_TID) >> 2) - 1);
        touchType = (BYTE)((event[0] & S6SY761_MASK_TOUCH_STATE) >> 6);

        x = (event[1] << 4) | ((event[3] & 0xf0) >> 4);
        y = (event[2] << 4) | (event[3] & 0x0f);

        switch (touchType)
        {
        case S6SY761_TS_PRESS:
            /*
             * The first finger becomes the tracked finger. Its initial
             * coordinate is only a reference and produces no cursor jump.
             */
            if (!pDevice->MouseTracking) {
                pDevice->MouseTracking = TRUE;
                pDevice->MouseTouchId = touchId;
                pDevice->MouseLastX = x;
                pDevice->MouseLastY = y;
            }
            break;

        case S6SY761_TS_MOVE:
            if (pDevice->MouseTracking &&
                touchId == pDevice->MouseTouchId)
            {
                pDevice->MousePendingDeltaX +=
                    (LONG)x - pDevice->MouseLastX;
                pDevice->MousePendingDeltaY +=
                    (LONG)y - pDevice->MouseLastY;

                if (pDevice->MousePendingDeltaX > 32767) {
                    pDevice->MousePendingDeltaX = 32767;
                } else if (pDevice->MousePendingDeltaX < -32768) {
                    pDevice->MousePendingDeltaX = -32768;
                }

                if (pDevice->MousePendingDeltaY > 32767) {
                    pDevice->MousePendingDeltaY = 32767;
                } else if (pDevice->MousePendingDeltaY < -32768) {
                    pDevice->MousePendingDeltaY = -32768;
                }

                pDevice->MouseLastX = x;
                pDevice->MouseLastY = y;
            }
            break;

        case S6SY761_TS_RELEASE:
            if (pDevice->MouseTracking &&
                touchId == pDevice->MouseTouchId)
            {
                pDevice->MouseTracking = FALSE;
            }
            break;

        default:
            break;
        }
    }

    /*
     * HID expects signed 16-bit relative values. Saturate accumulated
     * movement instead of allowing an integer conversion to wrap.
     */
    RtlZeroMemory(&readReport, sizeof(readReport));
    readReport.ReportId = CONTROL_COLLECTION_REPORT_ID;

    if (pDevice->MousePendingDeltaX > 32767) {
        readReport.X = 32767;
    } else if (pDevice->MousePendingDeltaX < -32768) {
        readReport.X = -32768;
    } else {
        readReport.X = (SHORT)pDevice->MousePendingDeltaX;
    }

    if (pDevice->MousePendingDeltaY > 32767) {
        readReport.Y = 32767;
    } else if (pDevice->MousePendingDeltaY < -32768) {
        readReport.Y = -32768;
    } else {
        readReport.Y = (SHORT)pDevice->MousePendingDeltaY;
    }

    /*
     * If there is no movement, completing a pending read with a zero
     * relative report is valid and prevents hidclass from waiting forever
     * after a press/release sequence.
     */
    status = WdfIoQueueRetrieveNextRequest(
        pDevice->ManualQueue,
        &request);

    if (!NT_SUCCESS(status)) {
        return TRUE;
    }

    status = RequestCopyFromBuffer(
        request,
        &readReport,
        sizeof(readReport));

    if (NT_SUCCESS(status)) {
        pDevice->MousePendingDeltaX -= readReport.X;
        pDevice->MousePendingDeltaY -= readReport.Y;
    }

    WdfRequestComplete(request, status);

    return TRUE;
}
VOID
SpbDeviceOpen(
	_In_  PDEVICE_CONTEXT  pDevice
)
{
	WDF_IO_TARGET_OPEN_PARAMS  openParams;
	NTSTATUS status;
	DECLARE_UNICODE_STRING_SIZE(DevicePath, RESOURCE_HUB_PATH_SIZE);
	RESOURCE_HUB_CREATE_PATH_FROM_ID(
		&DevicePath,
		pDevice->PeripheralId.LowPart,
		pDevice->PeripheralId.HighPart);

	//
	// Open a handle to the SPB controller.
	//

	WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
		&openParams,
		&DevicePath,
		(GENERIC_READ | GENERIC_WRITE));

	openParams.ShareAccess = 0;
	openParams.CreateDisposition = FILE_OPEN;
	openParams.FileAttributes = FILE_ATTRIBUTE_NORMAL;

	status = WdfIoTargetOpen(
		pDevice->SpbController,
		&openParams);

	if (!NT_SUCCESS(status))
	{
	}

	DbgPrint("sense enable\n");
	SpbDeviceWrite(pDevice, S6SY761_SENSE_ON, 1);

	DbgPrint("reading events\n");
	SpbDeviceWrite(pDevice, S6SY761_READ_ALL_EVENTS, 1);

	DbgPrint("draining last events\n");
	SpbDeviceWriteRead(pDevice, S6SY761_CLEAR_EVENT_STACK, eventbuf, 1, 256);

	DbgPrint("enabling Interrupt\n");
	WdfInterruptEnable(pDevice->Interrupt);

}
VOID
SpbDeviceClose(
	_In_  PDEVICE_CONTEXT  pDevice
)
{
	WdfInterruptDisable(pDevice->Interrupt);

	WdfIoTargetClose(pDevice->SpbController);
}
VOID
SpbDeviceWrite(
	_In_ PDEVICE_CONTEXT pDevice,
	_In_ PVOID pInputBuffer,
	_In_ size_t inputBufferLength
)
{
	WDF_MEMORY_DESCRIPTOR  inMemoryDescriptor;
	ULONG_PTR  bytesWritten = (ULONG_PTR)NULL;
	NTSTATUS status;


	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inMemoryDescriptor,
		pInputBuffer,
		(ULONG)inputBufferLength);

	status = WdfIoTargetSendWriteSynchronously(
		pDevice->SpbController,
		NULL,
		&inMemoryDescriptor,
		NULL,
		NULL,
		&bytesWritten
	);

	if (!NT_SUCCESS(status))
	{
	}
}

HRESULT
SpbDeviceWriteRead(
	_In_ PDEVICE_CONTEXT pDevice,
	_In_ PVOID pInputBuffer,
	_In_ PVOID pOutputBuffer,
	_In_ size_t inputBufferLength,
	_In_ size_t outputBufferLength
)
{
	WDF_MEMORY_DESCRIPTOR  inMemoryDescriptor;
	WDF_MEMORY_DESCRIPTOR  outMemoryDescriptor;
	ULONG_PTR  bytesWritten = (ULONG_PTR)NULL;
	ULONG_PTR  bytesRead = (ULONG_PTR)NULL;
	NTSTATUS status;


	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inMemoryDescriptor,
		pInputBuffer,
		(ULONG)inputBufferLength);

	status = WdfIoTargetSendWriteSynchronously(
		pDevice->SpbController,
		NULL,
		&inMemoryDescriptor,
		NULL,
		NULL,
		&bytesWritten
	);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outMemoryDescriptor,
		pOutputBuffer,
		(ULONG)outputBufferLength);

	status = WdfIoTargetSendReadSynchronously(
		pDevice->SpbController,
		NULL,
		&outMemoryDescriptor,
		NULL,
		NULL,
		&bytesRead
	);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	return STATUS_SUCCESS;
}
