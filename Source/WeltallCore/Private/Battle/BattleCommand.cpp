#include "Battle/BattleCommand.h"

#include "Battle/BattleRecordBytes.h"

namespace Weltall
{
	FBattleCommand FBattleCommand::FromRecord(std::span<const uint8, RecordSize> Record)
	{
		FBattleCommand Command;
		VisitBattleCommandLayout(Command, BattleRecordBytes::FReader{Record});
		BattleRecordBytes::Read(Record, 0x04, Command.Bytes04);
		BattleRecordBytes::Read(Record, 0x12, Command.OwnStatusTurns);
		return Command;
	}

	FBattleItem FBattleItem::FromRecord(std::span<const uint8, RecordSize> Record)
	{
		FBattleItem Item;
		VisitBattleItemLayout(Item, BattleRecordBytes::FReader{Record});
		return Item;
	}
}
