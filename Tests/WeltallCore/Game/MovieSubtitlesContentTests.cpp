#include "Game/GameSession.h"
#include "Media/GameMovies.h"
#include "Media/MoviePlayer.h"
#include "Support/GameDiscs.h"
#include "Support/GameLoopSession.h"

#include <catch_amalgamated.hpp>

#include <map>

using namespace Weltall;

namespace
{
	FGameFileIndex IndexOf(const FIso9660& Disc)
	{
		auto Program = ReadBootExecutable(Disc);
		REQUIRE(Program);
		auto Index = FGameFileIndex::Read(*Program);
		REQUIRE(Index);
		return std::move(*Index);
	}

	void SetSubtitles(FGameSession& Session, EVoiceLanguage Voices, EMovieSubtitles Subtitles)
	{
		FPlayerSettings Settings = Session.Settings();
		Settings.VoiceLanguage = Voices;
		Settings.MovieSubtitles = Subtitles;
		Session.SetSettings(Settings);
	}

	bool HasSubtitles(const FGameSession& Session, int32 Movie)
	{
		const auto Subtitles = Session.MovieSubtitles(Movie);
		REQUIRE(Subtitles);
		return Subtitles->has_value();
	}
}

TEST_CASE("The supported undubs subtitle the movies their patch lists, every line's picture in its file", "[Media][Content][Voices]")
{
	const std::map<EDiscRelease, std::map<int32, std::size_t>> Expected{
		{EDiscRelease::UndubDisc1, {{1, 1}, {2, 6}, {5, 2}, {6, 2}, {7, 8}, {8, 15}, {9, 14}, {13, 43}}},
		{EDiscRelease::UndubDisc2, {{0, 8}, {4, 40}, {8, 6}}},
	};
	for (const auto& [Release, Movies] : Expected)
	{
		REQUIRE_DISC(Disc, Release);
		INFO(DiscReleaseId(Release));
		const FGameFileIndex Index = IndexOf(*Disc);
		const FMovieSubtitleTable& Table = Index.MovieSubtitles();
		REQUIRE(Table.Entries().size() == Movies.size());
		for (const auto& [Movie, Lines] : Movies)
		{
			INFO("movie " << Movie);
			const FMovieSubtitleEntry* Entry = Table.Find(Movie);
			REQUIRE(Entry != nullptr);
			CHECK(Entry->Lines.size() == Lines);
			const std::optional<FMovieLocation> Location = FindMovie(Index, Movie);
			REQUIRE(Location);
			for (std::size_t Line = 1; Line < Entry->Lines.size(); ++Line)
			{
				CHECK(Entry->Lines[Line - 1].EndFrame <= Entry->Lines[Line].FirstFrame);
			}
			auto Subtitles = FMovieSubtitles::Load(Disc->Image(), Index, *Entry);
			REQUIRE(Subtitles);
			for (const FMovieSubtitleLine& Line : Entry->Lines)
			{
				const auto Picture = Subtitles->At(Line.FirstFrame);
				REQUIRE(Picture);
				bool bCovers = false;
				for (std::size_t Alpha = 3; Alpha < Picture->Pixels.size(); Alpha += 4)
				{
					bCovers = bCovers || Picture->Pixels[Alpha] == 255;
				}
				CHECK(bCovers);
			}
		}
	}
	for (const EDiscRelease Release : {EDiscRelease::UsaDisc1, EDiscRelease::UsaDisc2})
	{
		REQUIRE_DISC(Disc, Release);
		CHECK(IndexOf(*Disc).MovieSubtitles().Entries().empty());
	}
}

TEST_CASE("Movie subtitles show with the undub's sound on Auto, over either sound when on, and never when off", "[Game][Content][Voices]")
{
	REQUIRE_DISC(Usa1, EDiscRelease::UsaDisc1);
	REQUIRE_DISC(Undub1, EDiscRelease::UndubDisc1);
	constexpr int32 Opening = 13;
	constexpr int32 Logos = 16;
	auto Game = Testing::FGameLoopSession::Start(*Usa1);
	FGameSession& Session = *Game->Session;
	CHECK_FALSE(HasSubtitles(Session, Opening));
	const FGameFileIndex Japanese = IndexOf(*Undub1);
	Session.AddVoiceDisc(EVoiceLanguage::Japanese, 0, Undub1->Image(), Japanese);
	SetSubtitles(Session, EVoiceLanguage::Automatic, EMovieSubtitles::Automatic);
	CHECK_FALSE(HasSubtitles(Session, Opening));
	SetSubtitles(Session, EVoiceLanguage::Japanese, EMovieSubtitles::Automatic);
	CHECK(HasSubtitles(Session, Opening));
	CHECK_FALSE(HasSubtitles(Session, Logos));
	SetSubtitles(Session, EVoiceLanguage::English, EMovieSubtitles::On);
	CHECK(HasSubtitles(Session, Opening));
	CHECK_FALSE(HasSubtitles(Session, Logos));
	SetSubtitles(Session, EVoiceLanguage::Japanese, EMovieSubtitles::Off);
	CHECK_FALSE(HasSubtitles(Session, Opening));
	// On the second disc the first disc's undub has nothing to give.
	SetSubtitles(Session, EVoiceLanguage::Japanese, EMovieSubtitles::On);
	Session.Kernel().Disc = 1;
	CHECK_FALSE(HasSubtitles(Session, 4));

	auto Undub = Testing::FGameLoopSession::Start(*Undub1);
	CHECK(HasSubtitles(*Undub->Session, Opening));
	// With no USA disc, English falls back to the undub's own sound, which the subtitles go with.
	SetSubtitles(*Undub->Session, EVoiceLanguage::English, EMovieSubtitles::Automatic);
	CHECK(HasSubtitles(*Undub->Session, Opening));
}

TEST_CASE("The opening shows its first subtitle from frame 931 to 970, laid where the patch draws it", "[Media][Content][Voices]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UndubDisc1);
	auto Game = Testing::FGameLoopSession::Start(*Disc);
	auto Subtitles = Game->Session->MovieSubtitles(13);
	REQUIRE(Subtitles);
	REQUIRE(Subtitles->has_value());
	const std::optional<FMovieLocation> Location = FindMovie(*Game->Index, 13);
	REQUIRE(Location);
	FMoviePlayback Playback;
	// About ten sectors a frame: reading starts some frames before the line.
	Playback.StartSector = 9100;
	Playback.EndFrame = 1000;
	FMoviePlayer Player(Disc->Image(), *Location, Playback, *Game->Session->Sound().Sound(), std::move(*Subtitles));
	REQUIRE(Player.Start());
	REQUIRE(Player.FrameNumber() < 931);
	std::shared_ptr<const FMovieSubtitlePicture> First;
	while (Player.FrameNumber() < 975)
	{
		const uint32 Frame = Player.FrameNumber();
		INFO("frame " << Frame);
		if (Frame < 931 || Frame >= 971)
		{
			CHECK(Player.Subtitle() == nullptr);
		}
		else
		{
			REQUIRE(Player.Subtitle() != nullptr);
			if (!First)
			{
				First = Player.Subtitle();
				CHECK(Frame == 931);
			}
			CHECK(Player.Subtitle() == First);
		}
		const auto Playing = Player.Vblank({});
		REQUIRE(Playing);
		REQUIRE(*Playing);
	}
	REQUIRE(First);
	CHECK(First->X == 32);
	CHECK(First->Y == 189);
	CHECK(First->Width == 256);
	CHECK(First->Height == 14);
}
