#include "Input/HidGamepadDevice.h"

#if PLATFORM_WINDOWS

#include "Features/IModularFeatures.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GenericPlatform/IInputInterface.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "HAL/PlatformTime.h"
#include "IInputDevice.h"
#include "IInputDeviceModule.h"
#include "Input/GamepadEvents.h"
#include "Input/HidGamepads.h"
#include "Tasks/Task.h"
#include "WeltallLog.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include <hidsdi.h>
#include <setupapi.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include <algorithm>

namespace
{
	using Weltall::EHidGamepadModel;

	const FName DeviceInterfaceName(TEXT("WeltallHidGamepads"));

	/** Microsoft's gamepads are XInput's, as is any device whose path names an XInput interface. */
	constexpr USHORT MicrosoftVendor = 0x045E;
	const TCHAR* const XInputInterface = TEXT("IG_");

	/** The length of Sony's gamepads' input reports over USB; over Bluetooth they declare longer ones. */
	constexpr USHORT SonyUsbReportLength = 64;

	/** More reports than a gamepad sends between two frames: a read loop stops there. */
	constexpr int32 MostReportsAFrame = 256;

	/** An axis or the hat of a gamepad read through its HID description, and its logical range. */
	struct FHidValue
	{
		USAGE Usage = 0;
		USHORT Collection = 0;
		LONG Minimum = 0;
		LONG Maximum = 0;
		USHORT BitSize = 0;
	};

	/** A HID gamepad open for reading and writing, overlapped. */
	class FHidPad
	{
	public:
		/** Opens the HID device at Path if it is a gamepad Weltall reads (not XInput's); null otherwise. Any thread. */
		static TUniquePtr<FHidPad> Open(const FString& Path);

		~FHidPad()
		{
			CancelIoEx(Handle, nullptr);
			DWORD Ignored = 0;
			if (bReading)
			{
				GetOverlappedResult(Handle, &ReadOverlapped, &Ignored, true);
			}
			if (bWriting)
			{
				GetOverlappedResult(Handle, &WriteOverlapped, &Ignored, true);
			}
			CloseHandle(Handle);
			CloseHandle(ReadOverlapped.hEvent);
			CloseHandle(WriteOverlapped.hEvent);
			if (Preparsed != nullptr)
			{
				HidD_FreePreparsedData(Preparsed);
			}
		}

		const FString& Path() const { return DevicePath; }
		const FString& Name() const { return DeviceName; }
		bool HasMotors() const { return OutputLength > 0 && (Model == EHidGamepadModel::DualShock4 || Model == EHidGamepadModel::DualSense); }

		/** Starts reading its reports; false if it cannot. On the thread that reads them. */
		bool StartRead()
		{
			ResetEvent(ReadOverlapped.hEvent);
			bReading = ReadFile(Handle, ReadBuffer.GetData(), static_cast<DWORD>(ReadBuffer.Num()), nullptr, &ReadOverlapped) || GetLastError() == ERROR_IO_PENDING;
			return bReading;
		}

		/** Reads the reports that arrived since the last call into Reading; false once the gamepad has gone. */
		bool Read(Weltall::FGamepadReading& Reading)
		{
			for (int32 Report = 0; Report < MostReportsAFrame; ++Report)
			{
				DWORD Length = 0;
				if (!GetOverlappedResult(Handle, &ReadOverlapped, &Length, false))
				{
					return GetLastError() == ERROR_IO_INCOMPLETE;
				}
				Interpret(TConstArrayView<uint8>(ReadBuffer.GetData(), static_cast<int32>(Length)), Reading);
				if (!StartRead())
				{
					return false;
				}
			}
			return true;
		}

		/** Whether its last motor report is still being written. */
		bool IsWriting()
		{
			DWORD Written = 0;
			if (bWriting && !GetOverlappedResult(Handle, &WriteOverlapped, &Written, false) && GetLastError() == ERROR_IO_INCOMPLETE)
			{
				return true;
			}
			// One that failed is not written again: a gamepad that left is found by its reads.
			bWriting = false;
			return false;
		}

		/** Writes its motors' speeds. */
		void WriteMotors(uint8 Small, uint8 Large)
		{
			const std::vector<uint8> Report = Model == EHidGamepadModel::DualShock4 ? Weltall::DualShock4MotorReport(bBluetooth, Small, Large)
																					 : Weltall::DualSenseMotorReport(bBluetooth, Sequence++, Small, Large);
			// Windows writes a report padded to the longest the device declares.
			WriteBuffer.SetNumZeroed(OutputLength);
			FMemory::Memcpy(WriteBuffer.GetData(), Report.data(), FMath::Min<int32>(OutputLength, static_cast<int32>(Report.size())));
			ResetEvent(WriteOverlapped.hEvent);
			bWriting = WriteFile(Handle, WriteBuffer.GetData(), static_cast<DWORD>(WriteBuffer.Num()), nullptr, &WriteOverlapped) || GetLastError() == ERROR_IO_PENDING;
		}

	private:
		FHidPad() = default;

		void Interpret(TConstArrayView<uint8> Report, Weltall::FGamepadReading& Reading)
		{
			const std::span<const uint8> Bytes(Report.GetData(), static_cast<std::size_t>(Report.Num()));
			switch (Model)
			{
			case EHidGamepadModel::DualShock4:
				Weltall::ReadDualShock4Report(Bytes, bAdapter, Reading);
				return;
			case EHidGamepadModel::DualSense:
				Weltall::ReadDualSenseReport(Bytes, bBluetooth, Reading);
				return;
			case EHidGamepadModel::PlayStationClassic:
			case EHidGamepadModel::Other:
				break;
			}
			Weltall::FHidControls Controls;
			if (ReadControls(Report, Controls))
			{
				Reading = Weltall::ReadHidControls(Model, Controls);
			}
		}

		/** The controls a report of its HID description holds; false for a report of other things. */
		bool ReadControls(TConstArrayView<uint8> Report, Weltall::FHidControls& Controls)
		{
			PCHAR Data = reinterpret_cast<PCHAR>(const_cast<uint8*>(Report.GetData()));
			const ULONG Length = static_cast<ULONG>(Report.Num());
			ULONG Count = static_cast<ULONG>(Usages.Num());
			if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0, Usages.GetData(), &Count, Preparsed, Data, Length) != HIDP_STATUS_SUCCESS)
			{
				return false;
			}
			Controls.Buttons.assign(Usages.GetData(), Usages.GetData() + Count);
			for (const FHidValue& Value : Values)
			{
				ULONG Raw = 0;
				if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, Value.Collection, Value.Usage, &Raw, Preparsed, Data, Length) != HIDP_STATUS_SUCCESS)
				{
					continue;
				}
				// A range from a negative minimum is of signed values; one whose maximum is not above its minimum spans its bits.
				const LONG Signed = Value.Minimum < 0 && Value.BitSize > 0 && Value.BitSize < 32 && (Raw >> (Value.BitSize - 1)) != 0
					? static_cast<LONG>(Raw) - (1L << Value.BitSize)
					: static_cast<LONG>(Raw);
				const bool bSpansBits = Value.Maximum <= Value.Minimum;
				const double Minimum = bSpansBits ? 0.0 : Value.Minimum;
				const double Maximum = bSpansBits ? static_cast<double>((1ull << FMath::Min<USHORT>(Value.BitSize, 31)) - 1) : Value.Maximum;
				if (Value.Usage == HID_USAGE_GENERIC_HATSWITCH)
				{
					// Eight directions, or four (each then every other one of the eight); any other value is at rest.
					const int64 Position = static_cast<int64>(Signed) - static_cast<int64>(Minimum);
					const int64 Positions = static_cast<int64>(Maximum - Minimum) + 1;
					if (Position >= 0 && Position < Positions && (Positions == 8 || Positions == 4))
					{
						Controls.Hat = static_cast<int32>(Positions == 4 ? Position * 2 : Position);
					}
					continue;
				}
				const float Axis = static_cast<float>(FMath::Clamp((Signed - Minimum) / FMath::Max(Maximum - Minimum, 1.0) * 2.0 - 1.0, -1.0, 1.0));
				switch (Value.Usage)
				{
				case HID_USAGE_GENERIC_X: Controls.X = Axis; break;
				case HID_USAGE_GENERIC_Y: Controls.Y = Axis; break;
				case HID_USAGE_GENERIC_Z: Controls.Z = Axis; break;
				case HID_USAGE_GENERIC_RX: Controls.Rx = Axis; break;
				case HID_USAGE_GENERIC_RY: Controls.Ry = Axis; break;
				case HID_USAGE_GENERIC_RZ: Controls.Rz = Axis; break;
				default: break;
				}
			}
			return true;
		}

		/** The axes and the hat its HID description gives, each once. */
		void FindValues()
		{
			USHORT Count = 0;
			HidP_GetValueCaps(HidP_Input, nullptr, &Count, Preparsed);
			TArray<HIDP_VALUE_CAPS> Caps;
			Caps.SetNumZeroed(Count);
			if (Count == 0 || HidP_GetValueCaps(HidP_Input, Caps.GetData(), &Count, Preparsed) != HIDP_STATUS_SUCCESS)
			{
				return;
			}
			for (const HIDP_VALUE_CAPS& Cap : Caps)
			{
				if (Cap.UsagePage != HID_USAGE_PAGE_GENERIC)
				{
					continue;
				}
				const USAGE First = Cap.IsRange ? Cap.Range.UsageMin : Cap.NotRange.Usage;
				const USAGE Last = Cap.IsRange ? Cap.Range.UsageMax : Cap.NotRange.Usage;
				for (USAGE Usage = First; Usage >= First && Usage <= Last; ++Usage)
				{
					const bool bRead = (Usage >= HID_USAGE_GENERIC_X && Usage <= HID_USAGE_GENERIC_RZ) || Usage == HID_USAGE_GENERIC_HATSWITCH;
					if (bRead && !Values.ContainsByPredicate([Usage](const FHidValue& Value) { return Value.Usage == Usage; }))
					{
						Values.Add({Usage, Cap.LinkCollection, Cap.LogicalMin, Cap.LogicalMax, Cap.BitSize});
					}
					if (Usage == Last)
					{
						break;
					}
				}
			}
		}

		FString DevicePath;
		FString DeviceName;
		HANDLE Handle = INVALID_HANDLE_VALUE;
		EHidGamepadModel Model = EHidGamepadModel::Other;
		bool bBluetooth = false;
		bool bAdapter = false;
		PHIDP_PREPARSED_DATA Preparsed = nullptr;
		TArray<FHidValue> Values;
		TArray<USAGE> Usages;
		TArray<uint8> ReadBuffer;
		TArray<uint8> WriteBuffer;
		int32 OutputLength = 0;
		uint8 Sequence = 0;
		OVERLAPPED ReadOverlapped{};
		OVERLAPPED WriteOverlapped{};
		bool bReading = false;
		bool bWriting = false;
	};

	TUniquePtr<FHidPad> FHidPad::Open(const FString& Path)
	{
		if (Path.Contains(XInputInterface, ESearchCase::IgnoreCase))
		{
			return nullptr;
		}
		// Asking no access of a device opens any HID device, for its description; reading it is asked only of a gamepad.
		HANDLE Query = CreateFileW(*Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (Query == INVALID_HANDLE_VALUE)
		{
			return nullptr;
		}
		HIDD_ATTRIBUTES Attributes{};
		Attributes.Size = sizeof(Attributes);
		PHIDP_PREPARSED_DATA Preparsed = nullptr;
		HIDP_CAPS Caps{};
		WCHAR Product[128] = {};
		const bool bDescribed = HidD_GetAttributes(Query, &Attributes) && HidD_GetPreparsedData(Query, &Preparsed) && HidP_GetCaps(Preparsed, &Caps) == HIDP_STATUS_SUCCESS;
		if (bDescribed && !HidD_GetProductString(Query, Product, sizeof(Product)))
		{
			Product[0] = 0;
		}
		CloseHandle(Query);
		const bool bGamepad = bDescribed && Caps.UsagePage == HID_USAGE_PAGE_GENERIC
			&& (Caps.Usage == HID_USAGE_GENERIC_JOYSTICK || Caps.Usage == HID_USAGE_GENERIC_GAMEPAD) && Attributes.VendorID != MicrosoftVendor
			&& Caps.InputReportByteLength > 0;
		HANDLE Handle = bGamepad
			? CreateFileW(*Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)
			: INVALID_HANDLE_VALUE;
		if (bGamepad && Handle == INVALID_HANDLE_VALUE)
		{
			// A device that refuses writing is still read, without its motors.
			Handle = CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			Caps.OutputReportByteLength = 0;
		}
		if (Handle == INVALID_HANDLE_VALUE)
		{
			if (Preparsed != nullptr)
			{
				HidD_FreePreparsedData(Preparsed);
			}
			return nullptr;
		}
		TUniquePtr<FHidPad> Pad(new FHidPad());
		Pad->DevicePath = Path;
		Pad->Handle = Handle;
		Pad->Preparsed = Preparsed;
		Pad->Model = Weltall::HidGamepadModelOf(Attributes.VendorID, Attributes.ProductID);
		Pad->bAdapter = Weltall::IsDualShock4Adapter(Attributes.VendorID, Attributes.ProductID);
		Pad->bBluetooth = Caps.InputReportByteLength != SonyUsbReportLength;
		const std::string_view Known = Weltall::HidGamepadName(Pad->Model);
		Pad->DeviceName = !Known.empty() ? FString(static_cast<int32>(Known.size()), Known.data()) : Product[0] != 0 ? FString(Product) : FString(TEXT("Gamepad"));
		Pad->ReadBuffer.SetNumZeroed(Caps.InputReportByteLength);
		Pad->OutputLength = Caps.OutputReportByteLength;
		Pad->Usages.SetNumZeroed(FMath::Max<ULONG>(HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_BUTTON, Preparsed), 1));
		Pad->FindValues();
		Pad->ReadOverlapped.hEvent = CreateEventW(nullptr, true, false, nullptr);
		Pad->WriteOverlapped.hEvent = CreateEventW(nullptr, true, false, nullptr);
		return Pad;
	}

	/** The gamepads among the HID devices present, but those already open. Any thread. */
	TArray<TUniquePtr<FHidPad>> FindPads(const TSet<FString>& Open)
	{
		TArray<TUniquePtr<FHidPad>> Found;
		GUID HidClass;
		HidD_GetHidGuid(&HidClass);
		const HDEVINFO Devices = SetupDiGetClassDevsW(&HidClass, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (Devices == INVALID_HANDLE_VALUE)
		{
			return Found;
		}
		SP_DEVICE_INTERFACE_DATA Interface{};
		Interface.cbSize = sizeof(Interface);
		for (DWORD Index = 0; SetupDiEnumDeviceInterfaces(Devices, nullptr, &HidClass, Index, &Interface); ++Index)
		{
			DWORD Size = 0;
			SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, nullptr, 0, &Size, nullptr);
			if (Size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
			{
				continue;
			}
			TArray<uint8> Storage;
			Storage.SetNumZeroed(static_cast<int32>(Size));
			SP_DEVICE_INTERFACE_DETAIL_DATA_W* Detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(Storage.GetData());
			Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			if (!SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, Detail, Size, nullptr, nullptr))
			{
				continue;
			}
			const FString Path(Detail->DevicePath);
			if (!Open.Contains(Path))
			{
				if (TUniquePtr<FHidPad> Pad = FHidPad::Open(Path))
				{
					Found.Add(MoveTemp(Pad));
				}
			}
		}
		SetupDiDestroyDeviceInfoList(Devices);
		return Found;
	}

	/** The HID gamepads as one of the engine's input devices: found again whenever Windows says its devices changed. */
	class FHidGamepadDevice final : public IInputDevice
	{
	public:
		explicit FHidGamepadDevice(const TSharedRef<FGenericApplicationMessageHandler>& InHandler) : Handler(InHandler) {}

		virtual ~FHidGamepadDevice() override
		{
			// The pads a search still opens close with it.
			if (Search.IsSet())
			{
				Search->Wait();
			}
		}

		virtual void Tick(float DeltaTime) override
		{
			if (Search.IsSet() && Search->IsCompleted())
			{
				for (TUniquePtr<FHidPad>& Pad : Search->GetResult())
				{
					Connect(MoveTemp(Pad));
				}
				Search.Reset();
			}
			if (!Search.IsSet() && bSearchAgain)
			{
				bSearchAgain = false;
				TSet<FString> Open;
				for (const FPad& Pad : Pads)
				{
					Open.Add(Pad.Hid->Path());
				}
				Search.Emplace(UE::Tasks::Launch(UE_SOURCE_LOCATION, [Open = MoveTemp(Open)]() { return FindPads(Open); }));
			}
		}

		virtual void SendControllerEvents() override
		{
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			const double Now = FPlatformTime::Seconds();
			for (int32 Index = 0; Index < Pads.Num();)
			{
				FPad& Pad = Pads[Index];
				const FPlatformUserId User = Mapper.GetUserForInputDevice(Pad.Device);
				if (!Pad.Hid->Read(Pad.Reading))
				{
					Disconnect(Pad, User);
					Pads.RemoveAt(Index);
					continue;
				}
				Pad.Events.Send(*Handler, User, Pad.Device, Pad.Reading, Now);
				if (Pad.Events.IsHeld())
				{
					Pad.LastUsed = Now;
				}
				const bool bMotorsChanged = Pad.Motors[0] != Pad.SentMotors[0] || Pad.Motors[1] != Pad.SentMotors[1];
				if (Pad.Hid->HasMotors() && bMotorsChanged && !Pad.Hid->IsWriting())
				{
					Pad.Hid->WriteMotors(Pad.Motors[0], Pad.Motors[1]);
					Pad.SentMotors = Pad.Motors;
				}
				++Index;
			}
		}

		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InHandler) override
		{
			Handler = InHandler;
		}

		virtual bool Exec(UWorld* World, const TCHAR* Command, FOutputDevice& Output) override
		{
			return false;
		}

		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType Channel, float Value) override
		{
			FForceFeedbackValues Values = Feedback.FindRef(ControllerId);
			switch (Channel)
			{
			case FForceFeedbackChannelType::LEFT_LARGE: Values.LeftLarge = Value; break;
			case FForceFeedbackChannelType::LEFT_SMALL: Values.LeftSmall = Value; break;
			case FForceFeedbackChannelType::RIGHT_LARGE: Values.RightLarge = Value; break;
			case FForceFeedbackChannelType::RIGHT_SMALL: Values.RightSmall = Value; break;
			}
			SetChannelValues(ControllerId, Values);
		}

		/**
		 * The engine addresses force feedback on Windows by the user's index: of that user's gamepads here, the one used last
		 * runs its motors, the others stop.
		 */
		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override
		{
			Feedback.Add(ControllerId, Values);
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			FPad* Used = nullptr;
			for (FPad& Pad : Pads)
			{
				if (Mapper.GetUserIndexForPlatformUser(Mapper.GetUserForInputDevice(Pad.Device)) == ControllerId && Pad.Hid->HasMotors()
					&& (Used == nullptr || Pad.LastUsed > Used->LastUsed))
				{
					Used = &Pad;
				}
			}
			const auto Speed = [](float Left, float Right) { return static_cast<uint8>(FMath::Clamp(FMath::Max(Left, Right), 0.0f, 1.0f) * 255.0f + 0.5f); };
			for (FPad& Pad : Pads)
			{
				if (Mapper.GetUserIndexForPlatformUser(Mapper.GetUserForInputDevice(Pad.Device)) == ControllerId)
				{
					const bool bRuns = &Pad == Used;
					Pad.Motors = {bRuns ? Speed(Values.LeftSmall, Values.RightSmall) : uint8(0), bRuns ? Speed(Values.LeftLarge, Values.RightLarge) : uint8(0)};
				}
			}
		}

		virtual void SetDeviceProperty(int32 ControllerId, const FInputDeviceProperty* Property) override
		{
			// Windows' word that its devices changed, which the engine passes on to every input device.
			static const FName DevicesChanged(TEXT("Request_Device_Update"));
			if (Property != nullptr && Property->Name == DevicesChanged)
			{
				bSearchAgain = true;
			}
		}

		virtual bool IsGamepadAttached() const override
		{
			return !Pads.IsEmpty();
		}

	private:
		struct FPad
		{
			TUniquePtr<FHidPad> Hid;
			FInputDeviceId Device;
			FGamepadEvents Events;
			Weltall::FGamepadReading Reading;
			double LastUsed = 0.0;
			/** The small motor's speed, then the large one's: as the game asks, and as last written. */
			std::array<uint8, 2> Motors{};
			std::array<uint8, 2> SentMotors{};
		};

		void Connect(TUniquePtr<FHidPad> Hid)
		{
			if (!Hid->StartRead())
			{
				return;
			}
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			// A gamepad plugged in again is the same device to the engine.
			FInputDeviceId& Device = Devices.FindOrAdd(Hid->Path());
			if (!Device.IsValid())
			{
				Device = Mapper.AllocateNewInputDeviceId();
			}
			FInputDeviceDescriptor Descriptor;
			Descriptor.HardwareDeviceHandle = Device;
			Descriptor.InputDeviceName = DeviceInterfaceName;
			Descriptor.HardwareDeviceIdentifier = FName(*Hid->Name());
			FInputDeviceRegistry::RegisterDevice(Descriptor);
			Mapper.Internal_MapInputDeviceToUser(Device, Mapper.GetPlatformUserForNewlyConnectedDevice(), EInputDeviceConnectionState::Connected);
			UE_LOG(LogWeltall, Log, TEXT("Gamepad connected: %s"), *Hid->Name());
			FPad& Pad = Pads.AddDefaulted_GetRef();
			Pad.Hid = MoveTemp(Hid);
			Pad.Device = Device;
		}

		void Disconnect(FPad& Pad, FPlatformUserId User)
		{
			Pad.Events.Release(*Handler, User, Pad.Device);
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			Mapper.Internal_MapInputDeviceToUser(Pad.Device, Mapper.GetUserForUnpairedInputDevices(), EInputDeviceConnectionState::Disconnected);
			UE_LOG(LogWeltall, Log, TEXT("Gamepad disconnected: %s"), *Pad.Hid->Name());
		}

		TSharedRef<FGenericApplicationMessageHandler> Handler;
		TArray<FPad> Pads;
		TMap<FString, FInputDeviceId> Devices;
		TMap<int32, FForceFeedbackValues> Feedback;
		TOptional<UE::Tasks::TTask<TArray<TUniquePtr<FHidPad>>>> Search;
		bool bSearchAgain = true;
	};

	class FHidGamepadsFeature final : public IInputDeviceModule
	{
	public:
		virtual TSharedPtr<IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& Handler) override
		{
			// The editor's game module may be unloaded before its application lets go of its input devices.
			if (GIsEditor)
			{
				return nullptr;
			}
			return MakeShared<FHidGamepadDevice>(Handler);
		}
	};

	TUniquePtr<FHidGamepadsFeature> Feature;
}

void HidGamepads::Register()
{
	Feature = MakeUnique<FHidGamepadsFeature>();
	IModularFeatures::Get().RegisterModularFeature(IInputDeviceModule::GetModularFeatureName(), Feature.Get());
}

void HidGamepads::Unregister()
{
	if (Feature.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(IInputDeviceModule::GetModularFeatureName(), Feature.Get());
		Feature.Reset();
	}
}

#else

void HidGamepads::Register()
{
}

void HidGamepads::Unregister()
{
}

#endif
