#include "Graphics/Vram.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

TEST_CASE("Port VRAM revisions distinguish extra-row writes from console tile changes", "[Graphics][Vram]")
{
	FVram Vram;
	CHECK(Vram.PortRevision() == 0);
	Vram.Upload({{0, 0, 1, 1}, {1}});
	CHECK(Vram.PortRevision() == 0);
	const auto ConsoleRevision = Vram.Revision();
	Vram.UploadToPortRows({{0, 512, 1, 1}, {2}});
	CHECK(Vram.PortRevision() == 1);
	CHECK(Vram.TilesChangedSince(ConsoleRevision) == FVramTiles{});
	FVram Snapshot = Vram;
	Vram.UploadToPortRows({{1023, 1023, 1, 1}, {3}});
	CHECK(Vram.PortRevision() == 2);
	CHECK(Snapshot.PortRevision() == 1);
	CHECK(Snapshot.Lineage() == Vram.Lineage());
}

TEST_CASE("Texture page revisions ignore unrelated VRAM uploads and include changed palettes", "[Graphics][Vram]")
{
	FVram Vram;
	const FVramRect Page{640, 0, 64, 256};
	const FVramRect Clut{0, 480, 16, 1};
	CHECK(Vram.RevisionOver(Page) == 0);
	Vram.Upload({{640, 20, 1, 1}, {1}});
	const uint32 PageRevision = Vram.Revision();
	CHECK(Vram.RevisionOver(Page) == PageRevision);
	CHECK(Vram.RevisionOver(Clut) == 0);
	Vram.Upload({{0, 480, 16, 1}, std::vector<uint16>(16, 2)});
	CHECK(Vram.RevisionOver(Page) == PageRevision);
	CHECK(Vram.RevisionOver(Clut) == Vram.Revision());
	Vram.Upload({{400, 300, 1, 1}, {3}});
	CHECK(Vram.RevisionOver(Page) == PageRevision);
	CHECK(Vram.RevisionOver({0, 0, 0, 20}) == 0);
}

TEST_CASE("VRAM rectangle revisions follow wrapping and invalidate port textures", "[Graphics][Vram]")
{
	FVram Vram;
	Vram.Upload({{1023, 511, 2, 2}, {1, 2, 3, 4}});
	const uint32 Wrapped = Vram.Revision();
	CHECK(Vram.RevisionOver({1023, 511, 2, 2}) == Wrapped);
	CHECK(Vram.RevisionOver({0, 0, 1, 1}) == Wrapped);
	CHECK(Vram.RevisionOver({100, 100, 1, 1}) == 0);
	Vram.UploadToPortRows({{0, 512, 1, 1}, {5}});
	CHECK(Vram.RevisionOver({1023, 511, 2, 2}) == Wrapped);
	CHECK(Vram.RevisionOver({0, 512, 64, 256}) == Vram.Revision());
}
