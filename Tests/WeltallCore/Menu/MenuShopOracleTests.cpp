#include "Game/CharacterEquipment.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"
#include "Support/MenuOracleRun.h"

#include <catch_amalgamated.hpp>

#include <algorithm>

using namespace Weltall;
using namespace Weltall::Testing;
using namespace Weltall::Testing::MenuPad;

// The shops against the original module run whole under the oracle harness,
// frame by frame.

TEST_CASE("A shop sells weapons and accessories into a cart and takes the gold on Yes, as the original does", "[Content][Menu]")
{
	// Shop 3: who can use the goods and what they change, units put in the cart and one taken out, the list scrolled,
	// the total accepted with Yes.
	FMenuScenario Scenario{"shop_buy", EMenuMode::Shop, 3,
		{{0, 20}, {Cross, 2}, {0, 70}, {Right, 2}, {0, 6}, {Right, 2}, {0, 6}, {Left, 2}, {0, 6}, {Down, 2}, {0, 6}, {Down, 2}, {0, 6}, {Down, 2}, {0, 6},
			{Down, 2}, {0, 6}, {Down, 2}, {0, 6}, {Down, 2}, {0, 6}, {Down, 2}, {0, 6}, {Down, 2}, {0, 6}, {Right, 2}, {0, 6}, {Up, 2}, {0, 6}, {Up, 2},
			{0, 6}, {Up, 2}, {0, 6}, {Cross, 2}, {0, 30}, {Left, 2}, {0, 6}, {Cross, 2}, {0, 40}, {Circle, 2}, {0, 60}},
		700};
	Scenario.Pokes = {
		{0x1d30, {0x1f, 0x0, 0xff, 0x7}},
		{0x1924, {0xb8, 0xb, 0x0, 0x0}},
		{0x1d38, {0x3}},
		{0x1d9c, {0x1}},
	};
	CheckMenuScenario(Scenario, "ec81119f",
		"44223061 ef6008a9 855d6682*2 f677cbeb*2 04a124bd*2 bc8580b3 e6b8a4a2*13 1b25e068 6f3b07ce 0a31838f 59d8227c 19205652 fa1fa687 "
		"ef63544f f8983154 0b36ebdc 82412259 bcc5c6b5 e3501b79 e0b89ae0 cd682655 42a14c9e c9c684b2 c4ab586d e14ec0d5 331a07f4 d467e3dd "
		"58fae54e 671d49ea 630133c7 32190ac3 3caa6696 f9015d23 8a0763ac 5f0b8ecc 7c981f5c a1c0486e feb832b0 b9531ec4 b2a33ea5 588dc8d1 "
		"91364801 a64a26e7*5 8f1dba93*6 1d8ca383*6 07a44e4f*6 c62fee73*6 a64a26e7*6 8f1dba93 aeb3692b 8f1dba93 10aff09a a867e27a*2 c0aae8f4*3 "
		"ea98351b c0aae8f4 53e13b58 7497fb58*5 b1917473 a971ee1b 55eae0c6 3e520292*4 b84500ff dd304076 b84500ff 0738ddfe 54a6e51d*2 "
		"b9a10d70*3 c5ba6aac b9a10d70 1d3bc0bf 97a92087*5 13df9ff8 5b9c93f2 a31b1f7c b00510cc*4 5c5eedc6 03834dea 5c5eedc6 2935f60c "
		"d7103ba9*2 2efea68e*3 55ba45d4 2efea68e 26e90188 85ba0137*5 dd1bba7a e96dd087 36d820c5 e0a69b8e*4 c439ff92 afb21d18 c439ff92 "
		"6d32aa9b a0b1c418*2 c32ee245*3 ded0d11f c32ee245 073e3dca ce1f58e6*5 aa596c92 9066764f 3e4cf20f 64fc9640*4 a8c15336 fbe672cd "
		"a8c15336 6f011c6a c9b2a44e*2 ebad42e6*3 baf5d4c4 ebad42e6 d0e0a995 3047d9ca*5 831b4da8 673ace4b 2613e279 3dafd639*4 c039ed07*2 "
		"f6f1289e 8b2edb73 d3961542 24266ea3 b194d740 0169ef70 1ff2a1b6 cd88b2a3 ea947793*23 a757c7cc cdba6e1c*8 1b25e068 edb267bf 14f750a2 "
		"cd29fafc*38 cd0409d0 cd29fafc*2 24ddaa35*2");
}

TEST_CASE("A shop's items past the gold are refused and a cart can be given up, as the original does", "[Content][Menu]")
{
	// Shop 8 with little gold: rows dimmed past it, the cart refused there, the purchase and the cart's cancelling
	// answered No, then the cart given up with Yes.
	FMenuScenario Scenario{"shop_items", EMenuMode::Shop, 8,
		{{0, 20}, {Cross, 2}, {0, 70}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Down, 2},
			{0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4},
			{Down, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Circle, 2}, {0, 30}, {Right, 2}, {0, 6}, {Cross, 2}, {0, 30},
			{Cross, 2}, {0, 30}, {Circle, 2}, {0, 30}, {Circle, 2}, {0, 30}, {Left, 2}, {0, 6}, {Cross, 2}, {0, 40}, {Down, 2}, {0, 6}, {Circle, 2}, {0, 60}},
		800};
	Scenario.Pokes = {
		{0x1924, {0x96, 0x0, 0x0, 0x0}},
	};
	CheckMenuScenario(Scenario, "6322b282",
		"44223061 ef6008a9 855d6682*2 f677cbeb*2 04a124bd*2 bc8580b3 e6b8a4a2*13 1b25e068 7e31359e e0979077 db5548b9 f2b05a86 de71381f "
		"7775bbe7 a2c9a7c8 2815fb00 dcd6c1d2 50cb34ce 66e0ce30 3f1f62c7 527728d7 855958b4 a92efd76 987ffa9b aa3b785f 924864bd 1032362d "
		"0d31985d 15112ecd 454129b5 2d1686ef e1d204a7 49ff101b 913bdcf9 4f6c1e03 02cf2ed7 2e4194da e78f55e4 469f1dfd a9423773 2f1890bd "
		"62e65912 5dc407a4*5 f1121f29*6 d338fe14*6 aab5a596*6 46f4d571*6 5dc407a4*6 f1121f29 b0d876dd f1121f29 52b0884b 5e3f1f18*2 9c9b23c4 "
		"a06ca391 9c9b23c4 98477c25 45434f0c*2 aab91152 b9949768 aab91152 00877e1c 78f9fef7*2 d39b0171 104505cf d39b0171 b1a5c7dd 2a15f4af*2 "
		"b9e49faf 5f0ea2fe b9e49faf c281292a e49ccaca*2 2b79632f e32a019c 2b79632f 671e4de4 9a5bcee4*2 771db144 20b30736 771db144 96f5b108 "
		"e7f41025*2 70f1e2c5 9746eb8e 70f1e2c5 45340a71 22e34d71*2 ed788b76 873345cd ed788b76 96e5d288 e3ef5c92*2 7dd8c08e 31485664 7dd8c08e "
		"0512ed9f ca021d01*2 d965ec59 b4c9440e d965ec59 4a15867f 1ff3d9cc*2 e5858e0e 961bf601 e5858e0e 056c161e 78ae7f01*2 d225a8ec 8d49ca8e "
		"d225a8ec 16e3a7c8 a60bab80*2 0f117995 51935153 0f117995 0df88a36 72bd4b5b*2 4fff6047 f49572eb 4fff6047*4 ead634ad 9b2792c0 "
		"ead634ad*4 c763a557 4894bae2 c763a557*4 1531f4f3 a7b4070e 7c362e14 8b2edb73 d3961542 24266ea3 b194d740 0169ef70 3f1a7415 14c26d6c "
		"db6f52be*23 f71e414c db6f52be*8 1b25e068 79f47c8c*3 bae3c0bc 1531f4f3*2 72bd4b5b*6 4fff6047*6 ead634ad*6 c763a557*6 1531f4f3 "
		"f6f1289e 8b2edb73 d3961542 24266ea3 b194d740 0169ef70 bca0e66e 805b91ff 1bd95c55*23 da2ca650 909301a5 79f47c8c*2 bae3c0bc "
		"1531f4f3*3 72bd4b5b*6 4fff6047*6 ead634ad*6 c763a557*6 a7b4070e 7c362e14 8b2edb73 d3961542 24266ea3 b194d740 0169ef70 3f1a7415 "
		"14c26d6c db6f52be*23 f71e414c fa587669*8 1b25e068 909301a5*2 14f750a2 cd29fafc*37 942dfd03 724ab6ea*7 85b8453c 724ab6ea*2 24ddaa35*2");
}

namespace
{
	/**
	 * Shop 3: items (a key item left out) marked, unmarked and sold with Yes; Equipment: Citan chosen by the faces
	 * and one of his accessories sold.
	 */
	FMenuScenario ShopSell()
	{
		FMenuScenario Scenario{"shop_sell", EMenuMode::Shop, 3,
			{{0, 20}, {Down, 2}, {0, 4}, {Cross, 2}, {0, 60}, {Cross, 2}, {0, 60}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Down, 2}, {0, 4}, {Right, 2},
				{0, 4}, {Left, 2}, {0, 4}, {Cross, 2}, {0, 30}, {Left, 2}, {0, 6}, {Cross, 2}, {0, 40}, {Up, 2}, {0, 6}, {Cross, 2}, {0, 40}, {Right, 2},
				{0, 6}, {Right, 2}, {0, 6}, {Cross, 2}, {0, 60}, {Right, 2}, {0, 4}, {Cross, 2}, {0, 30}, {Left, 2}, {0, 6}, {Cross, 2}, {0, 40}, {Circle, 2},
				{0, 30}, {Circle, 2}, {0, 60}},
			900};
		Scenario.Pokes = {
			{0x1d30, {0x1f, 0x0, 0xff, 0x7}},
			{0x1924, {0x64, 0x0, 0x0, 0x0}},
			{0x1f90, {0x5, 0x2, 0x1, 0x1}},
			{0x2026, {0x1, 0x6, 0x4c, 0xa}},
			{0x428, {0x10, 0x0, 0x5a}},
		};
		return Scenario;
	}
}

TEST_CASE("A shop buys the party's items and a member's accessory, as the original does", "[Content][Menu]")
{
	CheckMenuScenario(ShopSell(), "53ca54ba",
		"44223061 ef6008a9 855d6682*2 f677cbeb*2 04a124bd*2 bc8580b3 e6b8a4a2*12 246d7349 724ab6ea*6 a34436da 3122795b fe35f645 c7c77fc7*2 "
		"c8930c4b*2 cb2bb467*2 2c00d935*2 575ccdb0*2 dc281ecf*2 c0beec44*2 40d73173*45 a34436da c19c1267 501c21ec a07d581e b98780d7 6a97bfa7 "
		"5e24399a 58d4a31e 051ad2cd 8fe07597 c3009eee 48093b50 24609e11 ef5ffa34 edc8f17d 78a580f9 553c9863 96e7c480 b3fb114a 663af566 "
		"913c5697 d7cc7fe6 6328ae87 9f2103a2 7683f786 78e4da67 9fad1992 d823fa20 e6399bf4 ba3f67af deda6bb0 50131773 0bfaef14 67603177 "
		"1d8fbc9a 06e40ff7*5 b2136e29*6 8977769c*6 52a2230f*6 538f3362*3 6a6acded 538f3362 2d62ae17 0147fa3b*3 6710cd60 0147fa3b 035eeb38 "
		"617308b4*3 434d2a35 617308b4 58491690 850acc26*3 a3d9f6b2 850acc26 b45e3d66 407383e2*3 de919052 407383e2 c48c4705 9bc8a244*4 "
		"7670bf95 691b89ce 9316812a 515e8059 7c58e707 c3ac6288 54ce6215 360e3ed4 ab90b7c4*23 4ba6fb71 c6c98277*8 a34436da 2990182b e3fb2eee "
		"40d73173*38 31528a5b 40d73173 490d51b1*7 2ee3f685 b1346776 2222a2bb 6aaee896 8b8f018f*7 673c19c5 8134dc7d be0d51fe 8849aeeb "
		"061a809f 0f3b7e80 f4bc39d0 8effb608*2 a3402f56*21 c1329711 4c2ab3be*7 189a5b3c bb889d6a*8 16e6fac8 7f56ee4d 315c8c65 52f281ee "
		"d61330e6 3290bbb8 853312b7 c39ddce6 9b6fe721 b0e5f0f3 ec454255 c4eb8983 77352b34 16a67dfe 141219be 70c85482 9853fd8d 13f39bd6 "
		"4ad04271 5fdfd38f 8a1fe9f5 94d52cb9 55fc6ad9 f74add91 fe273932 208bdc07 1424e430 f4b51ae6 7660d2b0 800523fe f643177e 84286767 "
		"edb5e0c8 d7ee5c45 f2c15345 0e61a53d c06aab2b*5 e977e56a*6 0bd11226*6 4c3d3a74*6 ded76b01*2 2dedba92 ded76b01 25a3a2e1 a6f18030 "
		"877902d7*3 7670bf95 691b89ce 9316812a 515e8059 7c58e707 c3ac6288 0ab96aeb b4486e51 c73aa444*23 f3d3c91e 91d263f8*8 a34436da "
		"2990182b 909da9df 490d51b1*38 ed20ebad d2c60253 20690638*30 36db09ec 20690638*2 24ddaa35*2");
}

TEST_CASE("Selling a worn accessory recomputes its wearer, or in the original leaves its bonuses until Status or Equip", "[Content][Menu]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	const auto Tables = FEquipmentTables::Load(Disc->Image(), *Index);
	REQUIRE(Tables);
	constexpr int32 Citan = 2;
	FMenuScenario Original = ShopSell();
	FMenuScenario Fixed = ShopSell();
	Fixed.Options.bSaleRecomputesEquipment = true;
	const FMenuRun Kept = RunMenuScenario(*Disc, Original);
	const FMenuRun Recomputed = RunMenuScenario(*Disc, Fixed);

	FGameState Expected = Kept.Game;
	RecomputeCharacterEquipment(Expected, Citan, *Tables);
	CHECK(std::ranges::equal(Recomputed.Game.Block(), Expected.Block()));
	CHECK_FALSE(std::ranges::equal(Kept.Game.Block(), Expected.Block()));
}

TEST_CASE("The shop's pointer at Buy, Sell and Exit sits beside their words with the fix, over them as the original draws it", "[Content][Menu]")
{
	// The arc shown with Buy chosen, then Sell.
	FMenuScenario Scenario{"shop_pointer", EMenuMode::Shop, 3, {{0, 40}, {Down, 2}, {0, 20}}, 70};
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMenuRun Original = RunMenuScenario(*Disc, Scenario);
	Scenario.Options.bShopPointerBesideWords = true;
	const FMenuRun Fixed = RunMenuScenario(*Disc, Scenario);
	REQUIRE(Original.Frames.size() == Fixed.Frames.size());
	CHECK(Original.Frames.front() == Fixed.Frames.front());
	CHECK(Original.Frames != Fixed.Frames);
	CHECK(Original.Leaves == Fixed.Leaves);
}
