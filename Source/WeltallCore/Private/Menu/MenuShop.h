#pragma once

#include "Menu/MenuShopModule.h"

#include <array>
#include <vector>

namespace Weltall
{
	/** The kinds of goods a shop sells: in its stock's order, and the inventory lists they go to. */
	enum class EShopKind : uint8
	{
		Weapon = 0,
		Accessory = 1,
		Item = 2,
	};

	/**
	 * A shop (mode 4) as its module runs: the stock of the shop number asked for, bought into a cart
	 * and paid for, or the party's goods sold, the gold and the inventory changed on a Yes.
	 */
	class FMenuShop final : public FMenuShopModule
	{
	public:
		FMenuShop(const FMenuServices& InServices, const FMenuRequest& InRequest);

		TMenuTask<> Run() override;

	private:
		void DrawFrame() override;
		void Init();
		void LoadStock();
		TMenuTask<> Shutdown();
		TMenuTask<> RunTopMenu();
		/** The chosen command run: false for Exit. */
		TMenuTask<bool> RunChoice();
		void AddLabels();

		// The lists' scene (MenuShopList.cpp).
		void DrawGold(uint32 OnEntry, uint32 Total, uint32 Left);
		/** The characters wearing the goods: a bit by character. */
		uint16 WornMask(uint8 Id, EShopKind Kind) const;
		std::span<const uint8> GoodsName(uint8 Id, EShopKind Kind) const;
		/** A record of the goods' table (weapons, accessories, items: 0x10 bytes each). */
		std::span<const uint8> GoodsRecord(uint8 Id, EShopKind Kind) const;

		// Buying (MenuShopBuy.cpp).
		TMenuTask<bool> RunBuy();
		void RenderBuyRows(int32 Top, uint32 Left, std::array<uint8, 8>& Affordable);
		uint32 ShowBuyDetail(int32 Row, int32 Top);
		/** The change the goods would make to a character's attack and armor: their sizes, and whether each is a loss. */
		void CompareEquipment(uint8 Id, EShopKind Kind, uint8 Character, std::array<int32, 2>& Changes, std::array<bool, 2>& Losses) const;
		void ShowStoredCount(EShopKind Kind, uint8 Id);
		void CommitPurchase(uint32 Left);

		// Selling (MenuShopSell.cpp).
		TMenuTask<bool> RunSellMenu();
		TMenuTask<> RunSellChoice();
		/** The goods of a list sold: Equipment's (a character's accessories, bEquipped) or an inventory list's. */
		TMenuTask<> RunSellList(std::span<uint8> Ids, std::span<uint8> Counts, EShopKind Kind, bool bInventory, std::span<const uint8> Kinds, uint8 Character);
		TMenuTask<bool> SellEquipped();
		void RenderSellRows(int32 Top);
		uint32 ShowSellDetail(uint8 Id, EShopKind Kind);
		void CommitSale(uint32 Gold, std::span<uint8> Ids, std::span<uint8> Counts, bool bInventory, uint8 Character);
		void CloseList(bool bCloses, bool bBuying);

		// The stock of the shop, and what the cart holds of each.
		std::array<uint8, 48> Stock{};
		std::array<EShopKind, 48> StockKinds{};
		int32 StockCount = 0;
		std::array<uint8, 48> Cart{};
		/** The inventory's count of the highlighted goods. */
		uint16 StoredCount = 0;

		// A sell list, compacted: the goods, how many are owned and to be sold, their kinds.
		std::vector<uint8> SaleIds;
		std::vector<uint8> SaleOwned;
		std::vector<uint8> SaleSold;
		std::vector<uint8> SaleKinds;
		std::vector<bool> SaleListed;
		int32 SaleCount = 0;

		std::array<FMenuArcCommand, 3> MainCommands{};
		std::array<std::array<FMenuArcCommand, 4>, 3> SubRows{};
		/** The arc's places' heights as this shop draws them: the module's, its own three raised by the fix. */
		std::array<int32, 7> PlacesY{};
	};
}
