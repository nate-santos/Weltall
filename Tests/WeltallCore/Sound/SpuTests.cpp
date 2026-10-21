#include "Sound/Spu.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

using namespace Weltall;

namespace
{
	constexpr uint32 VoiceBase(int32 Voice) { return static_cast<uint32>(Voice) * 16; }
	constexpr uint32 Volume = 0x0;
	constexpr uint32 Pitch = 0x4;
	constexpr uint32 Start = 0x6;
	constexpr uint32 Adsr1 = 0x8;
	constexpr uint32 Adsr2 = 0xA;
	constexpr uint32 Envelope = 0xC;
	constexpr uint32 MainLeft = 0x180;
	constexpr uint32 MainRight = 0x182;
	constexpr uint32 KeyOn = 0x188;
	constexpr uint32 KeyOff = 0x18C;
	constexpr uint32 EndFlags = 0x19C;
	constexpr uint32 Control = 0x1AA;

	/** Writes a block at an address (in 8-byte units): its header, its flags and every nibble the same. */
	void PutBlock(FSpu& Spu, uint32 Units, uint8 Header, uint8 Flags, uint8 Nibbles)
	{
		const std::size_t At = static_cast<std::size_t>(Units) * 8;
		Spu.Ram()[At] = Header;
		Spu.Ram()[At + 1] = Flags;
		std::fill(Spu.Ram().begin() + static_cast<std::ptrdiff_t>(At) + 2, Spu.Ram().begin() + static_cast<std::ptrdiff_t>(At) + 16, Nibbles);
	}

	/** An SPU with its output on at full volume and voice 0 set to play from Units at the given pitch, full volume both sides. */
	void SetUp(FSpu& Spu, uint32 Units, uint16 VoicePitch, uint16 Attack, uint16 Sustain)
	{
		Spu.Write(Control, 0xC000);
		Spu.Write(MainLeft, 0x3FFF);
		Spu.Write(MainRight, 0x3FFF);
		Spu.Write(VoiceBase(0) + Volume, 0x3FFF);
		Spu.Write(VoiceBase(0) + Volume + 2, 0x3FFF);
		Spu.Write(VoiceBase(0) + Pitch, VoicePitch);
		Spu.Write(VoiceBase(0) + Start, static_cast<uint16>(Units));
		Spu.Write(VoiceBase(0) + Adsr1, Attack);
		Spu.Write(VoiceBase(0) + Adsr2, Sustain);
	}

	std::vector<int16> Render(FSpu& Spu, int32 Frames)
	{
		std::vector<int16> Stereo(static_cast<std::size_t>(Frames) * 2);
		Spu.Render(Stereo);
		return Stereo;
	}
}

TEST_CASE("The SPU's envelope attacks linearly by its step and shift, then holds at the top", "[Sound]")
{
	FSpu Spu;
	// A block of silence that loops on itself; attack shift 0, step 0 (+7 << 11 a sample), a slow decay to sustain level 15.
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x00);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x0000);
	Spu.Write(KeyOn, 1);
	std::vector<int16> Levels;
	for (int32 Sample = 0; Sample < 4; ++Sample)
	{
		Render(Spu, 1);
		Levels.push_back(static_cast<int16>(Spu.Read(VoiceBase(0) + Envelope)));
	}
	CHECK(Levels == std::vector<int16>{0x3800, 0x7000, 0x7FFF, 0x7FFF});
}

TEST_CASE("The SPU's voices rest silent until they are keyed on", "[Sound]")
{
	FSpu Spu;
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x77);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	Render(Spu, 64);
	CHECK(Spu.Read(VoiceBase(0) + Envelope) == 0);
	CHECK(Spu.Read(VoiceBase(1) + Envelope) == 0);
}

TEST_CASE("The SPU's release falls to nothing after a key off", "[Sound]")
{
	FSpu Spu;
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x00);
	// Instant attack, sustain level 15 held (sustain shift and step all ones), release shift 0: -8 << 11 a sample.
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	Spu.Write(KeyOn, 1);
	Render(Spu, 16);
	CHECK(Spu.Read(VoiceBase(0) + Envelope) == 0x7FFF);
	Spu.Write(KeyOff, 1);
	Render(Spu, 1);
	CHECK(Spu.Read(VoiceBase(0) + Envelope) == 0x7FFF - 0x4000);
	Render(Spu, 2);
	CHECK(Spu.Read(VoiceBase(0) + Envelope) == 0);
}

TEST_CASE("The SPU plays a block's samples at its pitch and flags the loop end", "[Sound]")
{
	FSpu Spu;
	// Nibble 7 with shift 0 and no filter is 0x7000 in every sample; the block ends and repeats itself.
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x77);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	Spu.Write(KeyOn, 1);
	CHECK((Spu.Read(EndFlags) & 1) == 0);
	// 28 samples at pitch 0x1000 finish the block.
	Render(Spu, 27);
	CHECK((Spu.Read(EndFlags) & 1) == 0);
	Render(Spu, 1);
	CHECK((Spu.Read(EndFlags) & 1) == 1);
	// Once the interpolation holds four samples of 0x7000 and the envelope is at the top, both sides play it at half volume twice over.
	const std::vector<int16> Stereo = Render(Spu, 1);
	const int32 Expected = ((((0x7000 * 0x7F80) >> 15) * 0x7FFF >> 15) * 0x7FFE >> 15) * 0x7FFE >> 15;
	CHECK(std::abs(Stereo[0] - Expected) <= 4);
	CHECK(Stereo[0] == Stereo[1]);
	// A key on clears the voice's end flag.
	Spu.Write(KeyOn, 1);
	CHECK((Spu.Read(EndFlags) & 1) == 0);
}

TEST_CASE("The SPU mutes a one-shot sample at its end and stops a voice whose pitch is 0", "[Sound]")
{
	FSpu Spu;
	// End without repeat: the envelope drops to 0 and the voice releases.
	PutBlock(Spu, 0x200, 0x00, 0x01, 0x77);
	PutBlock(Spu, 0x100, 0x00, 0x07, 0x00);
	SetUp(Spu, 0x200, 0x2000, 0x00FF, 0x1FC0);
	Spu.Write(VoiceBase(0) + 0xE, 0x100);
	Spu.Write(KeyOn, 1);
	Render(Spu, 13);
	CHECK(Spu.Read(VoiceBase(0) + Envelope) == 0x7FFF);
	Render(Spu, 1);
	CHECK(Spu.Read(VoiceBase(0) + Envelope) == 0);
	CHECK((Spu.Read(EndFlags) & 1) == 1);

	FSpu Still;
	PutBlock(Still, 0x200, 0x00, 0x01, 0x77);
	SetUp(Still, 0x200, 0x0000, 0x00FF, 0x1FC0);
	Still.Write(KeyOn, 1);
	Render(Still, 100);
	CHECK((Still.Read(EndFlags) & 1) == 0);
}

TEST_CASE("The SPU's ADPCM filters take the samples before into account", "[Sound]")
{
	FSpu Spu;
	// Filter 1 (60/64 of the sample before), shift 4: the nibble 1 adds 0x100 to each sample, which rise toward 0x1000.
	PutBlock(Spu, 0x200, 0x14, 0x07, 0x11);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	Spu.Write(VoiceBase(0) + Envelope, 0x7FFF);
	Spu.Write(KeyOn, 1);
	// After the interpolation's delay of three samples and the attack, the output grows with the samples.
	std::vector<int16> Outputs;
	for (int32 Sample = 0; Sample < 12; ++Sample)
	{
		Outputs.push_back(Render(Spu, 1)[0]);
	}
	CHECK(std::is_sorted(Outputs.begin(), Outputs.end()));
	CHECK(Outputs.back() > 0x200);
}

TEST_CASE("The SPU's volumes set directly are twice the register's low 15 bits", "[Sound]")
{
	FSpu Spu;
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x77);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	Spu.Write(VoiceBase(0) + Volume + 2, 0x7FFF);
	Spu.Write(KeyOn, 1);
	Render(Spu, 40);
	const std::vector<int16> Stereo = Render(Spu, 1);
	// 0x3FFF is +0x7FFE; 0x7FFF, with bit 14 as the sign, is -2.
	CHECK(Stereo[0] > 0);
	CHECK(Stereo[1] < 0);
	CHECK(Stereo[1] > -8);
}

TEST_CASE("The SPU keeps its output silent while muted", "[Sound]")
{
	FSpu Spu;
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x77);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	Spu.Write(Control, 0x8000);
	Spu.Write(KeyOn, 1);
	const std::vector<int16> Stereo = Render(Spu, 64);
	CHECK(std::all_of(Stereo.begin(), Stereo.end(), [](int16 Value) { return Value == 0; }));
}

TEST_CASE("The SPU's noise shifts in the parity of its taps at the clock's rate", "[Sound]")
{
	FSpu Spu;
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x00);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	// Noise shift 15, step 7: the timer reloads by 4 and loses 7 a sample, so the level shifts every sample.
	Spu.Write(Control, static_cast<uint16>(0xC000 | 0x0F << 10 | 3 << 8));
	Spu.Write(0x194, 1);
	Spu.Write(VoiceBase(0) + Envelope, 0x7FFF);
	Spu.Write(KeyOn, 1);
	std::vector<int16> Stereo = Render(Spu, 20);
	// The register starts at 0 and shifts in ones: the voice plays a growing level.
	CHECK(std::any_of(Stereo.begin(), Stereo.end(), [](int16 Value) { return Value != 0; }));
}

TEST_CASE("The SPU's reverb writes its work area only when its writes are on", "[Sound]")
{
	const auto WorkArea = [](bool bWrites) {
		FSpu Spu;
		PutBlock(Spu, 0x200, 0x00, 0x07, 0x77);
		SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
		Spu.Write(Control, static_cast<uint16>(0xC000 | (bWrites ? 0x80 : 0)));
		Spu.Write(0x1A2, 0xE000);
		// Same-side reflection at 16 units, input volume full, reflection volume full.
		Spu.Write(0x1C0 + 2 * 2, 0x7FFF);
		Spu.Write(0x1C0 + 2 * 10, 0x0010);
		Spu.Write(0x1C0 + 2 * 11, 0x0020);
		Spu.Write(0x1C0 + 2 * 30, 0x7FFF);
		Spu.Write(0x1C0 + 2 * 31, 0x7FFF);
		Spu.Write(0x198, 1);
		Spu.Write(KeyOn, 1);
		Render(Spu, 400);
		const std::span<const uint8> Ram = std::as_const(Spu).Ram();
		return std::any_of(Ram.begin() + 0xE000 * 8, Ram.end(), [](uint8 Byte) { return Byte != 0; });
	};
	CHECK(WorkArea(true));
	CHECK_FALSE(WorkArea(false));
}

TEST_CASE("The SPU mixes the CD input at its volumes, then the main volume", "[Sound]")
{
	FSpu Spu;
	Spu.Write(Control, 0xC001);
	Spu.Write(MainLeft, 0x3FFF);
	Spu.Write(MainRight, 0x2000);
	Spu.Write(0x1B0, 0x4000);
	Spu.Write(0x1B2, 0x7FFF);
	const std::vector<int16> Cd{1000, -1000, 2000, -2000};
	CHECK(Spu.QueueCdInput(Cd) == 2);
	CHECK(Spu.QueuedCdInput() == 2);
	const std::vector<int16> Stereo = Render(Spu, 3);
	// 1000 * 4000h >> 15 = 500, then * 7FFEh >> 15; -1000 * 7FFFh >> 15 = -1000, then * 4000h >> 15.
	CHECK(Stereo[0] == 499);
	CHECK(Stereo[1] == -500);
	CHECK(Stereo[2] == 999);
	CHECK(Stereo[3] == -1000);
	// Then it runs dry: silence.
	CHECK(Stereo[4] == 0);
	CHECK(Stereo[5] == 0);
	CHECK(Spu.QueuedCdInput() == 0);
}

TEST_CASE("The SPU takes the CD input's frames even while it is off, and hears it while muted", "[Sound]")
{
	FSpu Spu;
	Spu.Write(MainLeft, 0x3FFF);
	Spu.Write(MainRight, 0x3FFF);
	Spu.Write(0x1B0, 0x7FFF);
	Spu.Write(0x1B2, 0x7FFF);
	const std::vector<int16> Cd(40, 3000);
	Spu.Write(Control, 0xC000);
	Spu.QueueCdInput(Cd);
	const std::vector<int16> Off = Render(Spu, 10);
	CHECK(std::all_of(Off.begin(), Off.end(), [](int16 Value) { return Value == 0; }));
	CHECK(Spu.QueuedCdInput() == 10);
	Spu.Write(Control, 0x8001);
	const std::vector<int16> Muted = Render(Spu, 10);
	CHECK(std::all_of(Muted.begin(), Muted.end(), [](int16 Value) { return Value > 2900; }));
	Spu.QueueCdInput(Cd);
	Spu.ClearCdInput();
	CHECK(Spu.QueuedCdInput() == 0);
	CHECK(Spu.QueueCdInput(std::vector<int16>(FSpu::CdInputFrames * 2 + 2)) == FSpu::CdInputFrames);
}

TEST_CASE("The SPU's main volume also scales the reverb's output", "[Sound]")
{
	FSpu Spu;
	PutBlock(Spu, 0x200, 0x00, 0x07, 0x77);
	SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
	Spu.Write(Control, 0xC080);
	Spu.Write(0x1A2, 0xE000);
	// Same-side reflection at 16 units, input volume full, reflection volume full, output volume full.
	Spu.Write(0x1C0 + 2 * 2, 0x7FFF);
	Spu.Write(0x1C0 + 2 * 10, 0x0010);
	Spu.Write(0x1C0 + 2 * 11, 0x0020);
	Spu.Write(0x1C0 + 2 * 30, 0x7FFF);
	Spu.Write(0x1C0 + 2 * 31, 0x7FFF);
	Spu.Write(0x184, 0x7FFF);
	Spu.Write(0x186, 0x7FFF);
	Spu.Write(0x198, 1);
	Spu.Write(MainLeft, 0);
	Spu.Write(MainRight, 0);
	Spu.Write(KeyOn, 1);
	const std::vector<int16> Stereo = Render(Spu, 400);
	CHECK(std::all_of(Stereo.begin(), Stereo.end(), [](int16 Value) { return Value == 0; }));
	const std::span<const uint8> Ram = std::as_const(Spu).Ram();
	CHECK(std::any_of(Ram.begin() + 0xE000 * 8, Ram.end(), [](uint8 Byte) { return Byte != 0; }));
}

TEST_CASE("The player's gains scale a voice and the CD input as they join the mix, and change nothing at full", "[Sound]")
{
	const auto Played = [](int32 VoiceGain, int32 CdGain)
	{
		FSpu Spu;
		PutBlock(Spu, 0x200, 0x00, 0x07, 0x77);
		SetUp(Spu, 0x200, 0x1000, 0x00FF, 0x1FC0);
		Spu.Write(Control, 0xC001);
		Spu.Write(0x1B0, 0x7FFF);
		Spu.Write(0x1B2, 0x7FFF);
		Spu.SetVoiceGain(0, VoiceGain);
		Spu.SetCdGain(CdGain);
		Spu.Write(KeyOn, 1);
		Render(Spu, 40);
		const std::vector<int16> Voice = Render(Spu, 1);
		Spu.Write(KeyOff, 1);
		Spu.Write(VoiceBase(0) + Volume, 0);
		Spu.Write(VoiceBase(0) + Volume + 2, 0);
		Render(Spu, 4);
		Spu.QueueCdInput(std::vector<int16>{8000, -8000});
		const std::vector<int16> Cd = Render(Spu, 1);
		return std::array<int32, 3>{Voice[0], Cd[0], Cd[1]};
	};
	const std::array<int32, 3> Full = Played(FSpu::FullGain, FSpu::FullGain);
	REQUIRE(Full[0] > 0x200);
	// 8000 * 7FFFh >> 15 = 7999, then the main volume's 7FFEh; -8000 stays -8000 as the shifts round down.
	CHECK(Full[1] == 7998);
	CHECK(Full[2] == -8000);
	const std::array<int32, 3> Half = Played(FSpu::FullGain / 2, FSpu::FullGain / 4);
	CHECK(std::abs(Half[0] - Full[0] / 2) <= 1);
	// 7999 * 2000h >> 15 = 1999, then 7FFEh: 1998.
	CHECK(Half[1] == 1998);
	CHECK(Half[2] == -2000);
	const std::array<int32, 3> Silent = Played(0, 0);
	CHECK(Silent == std::array<int32, 3>{});
}
