#include "Input/HidGamepads.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <vector>

using namespace Weltall;

namespace
{
	constexpr uint32 Bit(EGamepadControl Control)
	{
		return GamepadControlBit(Control);
	}

	/** A DualShock 4 report's first ten bytes after Header: sticks, the three button bytes, the triggers. */
	std::vector<uint8> DualShock4Report(std::vector<uint8> Header, std::array<uint8, 9> Data)
	{
		Header.insert(Header.end(), Data.begin(), Data.end());
		Header.resize(64, 0);
		return Header;
	}
}

TEST_CASE("Sony's gamepads are told apart by their product numbers", "[Input]")
{
	CHECK(HidGamepadModelOf(0x054C, 0x05C4) == EHidGamepadModel::DualShock4);
	CHECK(HidGamepadModelOf(0x054C, 0x09CC) == EHidGamepadModel::DualShock4);
	CHECK(HidGamepadModelOf(0x054C, 0x0BA0) == EHidGamepadModel::DualShock4);
	CHECK(HidGamepadModelOf(0x054C, 0x0CE6) == EHidGamepadModel::DualSense);
	CHECK(HidGamepadModelOf(0x054C, 0x0DF2) == EHidGamepadModel::DualSense);
	CHECK(HidGamepadModelOf(0x054C, 0x0CDA) == EHidGamepadModel::PlayStationClassic);
	CHECK(HidGamepadModelOf(0x054C, 0x0268) == EHidGamepadModel::Other);
	CHECK(HidGamepadModelOf(0x045E, 0x05C4) == EHidGamepadModel::Other);
	// Their names show PlayStation's buttons (ButtonGlyphs' IconSetOfDevice); another goes by its own.
	CHECK(HidGamepadName(EHidGamepadModel::DualShock4) == "DualShock 4");
	CHECK(HidGamepadName(EHidGamepadModel::PlayStationClassic) == "PlayStation Classic Controller");
	CHECK(HidGamepadName(EHidGamepadModel::Other).empty());
}

TEST_CASE("A DualShock 4's report gives its sticks, buttons and triggers", "[Input]")
{
	// Left stick full left and up, right stick at rest, hat down-left with Cross and Triangle, L1, Options and R3, the
	// touchpad's click; L2 half way.
	const std::array<uint8, 9> Data = {0x00, 0x00, 0x80, 0x80, 0x05 | 0x20 | 0x80, 0x01 | 0x20 | 0x80, 0x02, 0x80, 0x00};
	FGamepadReading Usb;
	REQUIRE(ReadDualShock4Report(DualShock4Report({0x01}, Data), false, Usb));
	CHECK(Usb.LeftX == -1.0f);
	CHECK(Usb.LeftY == 1.0f);
	CHECK(Usb.RightX == 0.0f);
	CHECK(Usb.RightY == 0.0f);
	CHECK(Usb.LeftTrigger == Catch::Approx(128.0f / 255.0f));
	CHECK(Usb.RightTrigger == 0.0f);
	CHECK(Usb.Buttons
		== (Bit(EGamepadControl::DPadDown) | Bit(EGamepadControl::DPadLeft) | Bit(EGamepadControl::FaceBottom) | Bit(EGamepadControl::FaceTop)
			| Bit(EGamepadControl::LeftShoulder) | Bit(EGamepadControl::SpecialRight) | Bit(EGamepadControl::RightThumb) | Bit(EGamepadControl::SpecialLeft)));
	// Bluetooth's full report carries the same two bytes further on; its short one as USB's.
	FGamepadReading Bluetooth;
	REQUIRE(ReadDualShock4Report(DualShock4Report({0x11, 0xC0, 0x00}, Data), false, Bluetooth));
	CHECK(Bluetooth == Usb);
	FGamepadReading Short;
	std::vector<uint8> ShortReport = DualShock4Report({0x01}, Data);
	ShortReport.resize(10);
	REQUIRE(ReadDualShock4Report(ShortReport, false, Short));
	CHECK(Short == Usb);
	FGamepadReading Other;
	CHECK_FALSE(ReadDualShock4Report(DualShock4Report({0x05}, Data), false, Other));
	CHECK_FALSE(ReadDualShock4Report(std::vector<uint8>{0x01, 0x80}, false, Other));
	// The wireless adapter reports all but zeros while no controller is paired with it, its status saying so: at rest.
	std::vector<uint8> Unpaired = DualShock4Report({0x01}, {0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00});
	Unpaired[31] = 0x04;
	FGamepadReading Adapter;
	REQUIRE(ReadDualShock4Report(Unpaired, true, Adapter));
	CHECK(Adapter == FGamepadReading{});
	CHECK(HeldGamepadControls(Adapter) == 0);
	FGamepadReading Paired;
	REQUIRE(ReadDualShock4Report(DualShock4Report({0x01}, Data), true, Paired));
	CHECK(Paired == Usb);
	CHECK(IsDualShock4Adapter(0x054C, 0x0BA0));
	CHECK_FALSE(IsDualShock4Adapter(0x054C, 0x05C4));
}

TEST_CASE("A DualSense's reports give its controls over USB and Bluetooth", "[Input]")
{
	// Full report: sticks, triggers, a counter, then the button bytes: hat right with Square, R1 and Create, the PS button.
	std::vector<uint8> Usb = {0x01, 0xFF, 0xFF, 0x00, 0x80, 0x00, 0xFF, 0x17, 0x02 | 0x10, 0x02 | 0x10, 0x01};
	Usb.resize(64, 0);
	FGamepadReading FromUsb;
	REQUIRE(ReadDualSenseReport(Usb, false, FromUsb));
	CHECK(FromUsb.LeftX == 1.0f);
	CHECK(FromUsb.LeftY == -1.0f);
	CHECK(FromUsb.RightX == -1.0f);
	CHECK(FromUsb.RightTrigger == 1.0f);
	CHECK(FromUsb.Buttons
		== (Bit(EGamepadControl::DPadRight) | Bit(EGamepadControl::FaceLeft) | Bit(EGamepadControl::RightShoulder) | Bit(EGamepadControl::SpecialLeft)));
	// Bluetooth's full report: two bytes before the same layout.
	std::vector<uint8> Full = {0x31, 0x00};
	Full.insert(Full.end(), Usb.begin() + 1, Usb.end());
	Full.resize(78, 0);
	FGamepadReading FromFull;
	REQUIRE(ReadDualSenseReport(Full, true, FromFull));
	CHECK(FromFull == FromUsb);
	// Bluetooth's short report until it is sent one of its own: laid out as the DualShock 4's.
	std::vector<uint8> Short = {0x01, 0xFF, 0xFF, 0x00, 0x80, 0x02 | 0x10, 0x02 | 0x10, 0x01, 0x00, 0xFF};
	FGamepadReading FromShort;
	REQUIRE(ReadDualSenseReport(Short, true, FromShort));
	CHECK(FromShort == FromUsb);
}

TEST_CASE("Held controls add the triggers and the sticks' directions past XInput's dead zones", "[Input]")
{
	FGamepadReading Reading;
	Reading.Buttons = Bit(EGamepadControl::FaceBottom);
	// XInput's dead zones: 7849 of 32767 for the left stick, 8689 for the right; 30 of 255 for a trigger.
	Reading.LeftX = 7800.0f / 32767.0f;
	Reading.LeftY = 7900.0f / 32767.0f;
	Reading.RightX = -8600.0f / 32767.0f;
	Reading.RightY = -8800.0f / 32767.0f;
	Reading.LeftTrigger = 31.0f / 255.0f;
	Reading.RightTrigger = 30.0f / 255.0f;
	CHECK(HeldGamepadControls(Reading)
		== (Bit(EGamepadControl::FaceBottom) | Bit(EGamepadControl::LeftStickUp) | Bit(EGamepadControl::RightStickDown) | Bit(EGamepadControl::LeftTrigger)));
}

TEST_CASE("The PlayStation Classic controller's buttons and d-pad come in its own order", "[Input]")
{
	FHidControls Controls;
	// Triangle, Cross, L2, R1, Start; the d-pad up and left.
	Controls.Buttons = {1, 3, 5, 8, 10};
	Controls.X = -1.0f;
	Controls.Y = -1.0f;
	const FGamepadReading Reading = ReadHidControls(EHidGamepadModel::PlayStationClassic, Controls);
	CHECK(Reading.Buttons
		== (Bit(EGamepadControl::FaceTop) | Bit(EGamepadControl::FaceBottom) | Bit(EGamepadControl::RightShoulder) | Bit(EGamepadControl::SpecialRight)
			| Bit(EGamepadControl::DPadUp) | Bit(EGamepadControl::DPadLeft)));
	CHECK(Reading.LeftTrigger == 1.0f);
	CHECK(Reading.RightTrigger == 0.0f);
	// Its axes are its d-pad, not a stick.
	CHECK(Reading.LeftX == 0.0f);
	CHECK(Reading.LeftY == 0.0f);
	Controls = {};
	Controls.Buttons = {2, 4, 6, 7, 9};
	Controls.X = 0.0f;
	Controls.Y = 1.0f;
	CHECK(ReadHidControls(EHidGamepadModel::PlayStationClassic, Controls).Buttons
		== (Bit(EGamepadControl::FaceRight) | Bit(EGamepadControl::FaceLeft) | Bit(EGamepadControl::LeftShoulder) | Bit(EGamepadControl::SpecialLeft)
			| Bit(EGamepadControl::DPadDown)));
}

TEST_CASE("Another gamepad's controls come as most PC gamepads number them", "[Input]")
{
	FHidControls Controls;
	// Square, Cross, Circle, Triangle, L1, R1, L2, R2, Select, Start, L3, R3; a 13th has no place.
	Controls.Buttons = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
	Controls.Hat = 3;
	Controls.X = 1.0f;
	Controls.Y = 1.0f;
	Controls.Z = -1.0f;
	Controls.Rz = -0.5f;
	Controls.Rx = 1.0f;
	const FGamepadReading Reading = ReadHidControls(EHidGamepadModel::Other, Controls);
	CHECK(Reading.Buttons
		== (Bit(EGamepadControl::FaceLeft) | Bit(EGamepadControl::FaceBottom) | Bit(EGamepadControl::FaceRight) | Bit(EGamepadControl::FaceTop)
			| Bit(EGamepadControl::LeftShoulder) | Bit(EGamepadControl::RightShoulder) | Bit(EGamepadControl::SpecialLeft)
			| Bit(EGamepadControl::SpecialRight) | Bit(EGamepadControl::LeftThumb) | Bit(EGamepadControl::RightThumb) | Bit(EGamepadControl::DPadDown)
			| Bit(EGamepadControl::DPadRight)));
	CHECK(Reading.LeftTrigger == 1.0f);
	CHECK(Reading.RightTrigger == 1.0f);
	// Down is positive on HID's axes, up on the engine's.
	CHECK(Reading.LeftX == 1.0f);
	CHECK(Reading.LeftY == -1.0f);
	CHECK(Reading.RightX == -1.0f);
	CHECK(Reading.RightY == 0.5f);
	// Without Z and Rz, the right stick is on the rotations; a hat at rest holds nothing.
	Controls = {};
	Controls.Rx = 0.25f;
	Controls.Ry = 1.0f;
	Controls.Hat = 8;
	const FGamepadReading Rotations = ReadHidControls(EHidGamepadModel::Other, Controls);
	CHECK(Rotations.RightX == 0.25f);
	CHECK(Rotations.RightY == -1.0f);
	CHECK(Rotations.Buttons == 0);
}

TEST_CASE("The motors' reports for USB and for Bluetooth, signed", "[Input]")
{
	const std::vector<uint8> Ds4Usb = DualShock4MotorReport(false, 0xFF, 0x40);
	REQUIRE(Ds4Usb.size() == 32);
	CHECK(Ds4Usb[0] == 0x05);
	CHECK(Ds4Usb[1] == 0x01);
	CHECK(Ds4Usb[4] == 0xFF);
	CHECK(Ds4Usb[5] == 0x40);
	// The checksums are zlib's CRC-32 of A2h and the report before them (worked out apart, with Python's zlib).
	const std::vector<uint8> Ds4Bluetooth = DualShock4MotorReport(true, 0xFF, 0x40);
	REQUIRE(Ds4Bluetooth.size() == 78);
	CHECK(Ds4Bluetooth[0] == 0x11);
	CHECK(Ds4Bluetooth[1] == 0xC0);
	CHECK(Ds4Bluetooth[3] == 0x01);
	CHECK(Ds4Bluetooth[6] == 0xFF);
	CHECK(Ds4Bluetooth[7] == 0x40);
	CHECK(std::vector<uint8>(Ds4Bluetooth.end() - 4, Ds4Bluetooth.end()) == std::vector<uint8>{147, 117, 200, 135});
	const std::vector<uint8> DualSenseUsb = DualSenseMotorReport(false, 0, 0xFF, 0x00);
	REQUIRE(DualSenseUsb.size() == 63);
	CHECK(DualSenseUsb[0] == 0x02);
	CHECK(DualSenseUsb[1] == 0x03);
	CHECK(DualSenseUsb[3] == 0xFF);
	CHECK(DualSenseUsb[4] == 0x00);
	CHECK(DualSenseUsb[39] == 0x04);
	const std::vector<uint8> DualSenseBluetooth = DualSenseMotorReport(true, 3, 0xFF, 0x00);
	REQUIRE(DualSenseBluetooth.size() == 78);
	CHECK(DualSenseBluetooth[0] == 0x31);
	CHECK(DualSenseBluetooth[1] == 0x30);
	CHECK(DualSenseBluetooth[2] == 0x10);
	CHECK(DualSenseBluetooth[3] == 0x03);
	CHECK(DualSenseBluetooth[5] == 0xFF);
	CHECK(DualSenseBluetooth[41] == 0x04);
	CHECK(std::vector<uint8>(DualSenseBluetooth.end() - 4, DualSenseBluetooth.end()) == std::vector<uint8>{14, 155, 153, 16});
}
