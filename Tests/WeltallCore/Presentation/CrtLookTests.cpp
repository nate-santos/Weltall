#include "Presentation/CrtLook.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;
using Catch::Approx;

TEST_CASE("The monitor's and the television's looks are whole steps of the CRT's parts", "[Presentation]")
{
	const FCrtParameters Monitor = CrtParameters(FCrtLook::Monitor());
	CHECK(Monitor.LumaBlur == Approx(0.15f));
	CHECK(Monitor.ChromaBlur == Approx(0.15f));
	CHECK(Monitor.BeamMin == Approx(0.17f));
	CHECK(Monitor.BeamMax == Approx(0.40f));
	CHECK(Monitor.MaskKind == 1);
	CHECK(Monitor.TriadsPerHeight == 480.0f);
	CHECK(Monitor.MaskStrength == Approx(0.4f));
	CHECK(Monitor.GlowAmount == Approx(0.03f));
	CHECK(Monitor.GlowRadius == Approx(1.5f));
	CHECK(Monitor.CurveX == 0.0f);
	CHECK(Monitor.CornerRadius == 0.0f);
	CHECK(Monitor.Vignette == 0.0f);

	const FCrtParameters Television = CrtParameters(FCrtLook::Television());
	CHECK(Television.LumaBlur == Approx(0.3f));
	CHECK(Television.ChromaBlur == Approx(1.0f));
	CHECK(Television.BeamMin == Approx(0.22f));
	CHECK(Television.BeamMax == Approx(0.45f));
	CHECK(Television.MaskKind == 2);
	CHECK(Television.TriadsPerHeight == 360.0f);
	CHECK(Television.MaskStrength == Approx(0.5f));
	CHECK(Television.GlowAmount == Approx(0.05f));
	CHECK(Television.GlowRadius == Approx(2.0f));
	CHECK(Television.CurveX == Approx(0.03f));
	CHECK(Television.CurveY == Approx(0.045f));
	CHECK(Television.CornerRadius == Approx(0.04f));
	CHECK(Television.Vignette == Approx(0.2f));
}

TEST_CASE("Every step of the CRT's parts draws within what its passes take", "[Presentation]")
{
	for (int32 Step = 0; Step <= FCrtLook::Steps; ++Step)
	{
		INFO("step " << Step);
		const FCrtLook Look{Step, ECrtMask::SlotMask, Step, ECrtMaskSize::Coarse, Step, Step, Step, Step, Step, Step};
		const FCrtParameters Parameters = CrtParameters(Look);
		// A full line's peak stays within full light, and a dark line is never wider than a bright one.
		CHECK(Parameters.BeamMax >= 0.4f);
		CHECK(Parameters.BeamMin > 0.0f);
		CHECK(Parameters.BeamMin <= Parameters.BeamMax);
		CHECK(Parameters.LumaBlur > 0.0f);
		CHECK(Parameters.ChromaBlur >= Parameters.LumaBlur);
		CHECK(Parameters.MaskStrength <= 1.0f);
		CHECK(Parameters.GlowRadius > 0.0f);
		CHECK(Parameters.Vignette <= 0.5f);
	}
	// A step past the range draws as its end.
	FCrtLook Past;
	Past.Scanlines = FCrtLook::Steps + 2;
	FCrtLook End;
	End.Scanlines = FCrtLook::Steps;
	CHECK(CrtParameters(Past).BeamMin == CrtParameters(End).BeamMin);
}
