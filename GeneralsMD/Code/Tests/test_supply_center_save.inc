#include "Common/ResourceGatheringManager.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"

namespace
{
class SupplyCenterSaveManager : public ResourceGatheringManager
{
public:
	void transfer(Xfer *xfer) { ResourceGatheringManager::xfer(xfer); }
	Object *search() { return findBestSupplyCenterInList(NULL); }
};

struct SupplyCenterSaveLogic
{
	GameLogic *saved;
	SupplyCenterSaveLogic() : saved(TheGameLogic)
	{
		// GameLogic destruction owns shared globals; keep this empty lookup alive with boot scaffolding.
		static GameLogic *emptyWorld = NEW GameLogic;
		TheGameLogic = emptyWorld;
	}
	~SupplyCenterSaveLogic() { TheGameLogic = saved; }
};
}

TEST(supply_center_cleanup_keeps_saved_counts_and_lists_consistent)
{
	CHECK(bootOnce());
	SupplyCenterSaveLogic world;
	const char *path = "test_supply_center_save_tmp.bin";
	// Exercise both ordinary warehouses and the empty-list underflow case.
	for (Int warehouseCount = 0; warehouseCount <= 2; warehouseCount += 2)
	{
		std::list<ObjectID> warehouses;
		for (Int i = 0; i < warehouseCount; ++i)
			warehouses.push_back((ObjectID)(11 + i));
		std::list<ObjectID> destroyedCenters;
		destroyedCenters.push_back((ObjectID)91);
		destroyedCenters.push_back((ObjectID)92);
		destroyedCenters.push_back((ObjectID)93);

		XferSave write;
		write.open(AsciiString(path));
		XferVersion version = 1;
		write.xferVersion(&version, 1);
		write.xferSTLObjectIDList(&warehouses);
		write.xferSTLObjectIDList(&destroyedCenters);
		write.close();

		SupplyCenterSaveManager manager;
		XferLoad read;
		read.open(AsciiString(path));
		manager.transfer(&read);
		read.close();
		CHECK(manager.search() == NULL);
		write.open(AsciiString(path));
		manager.transfer(&write);
		write.close();

		// The old erase removed center nodes but changed the warehouse count instead.
		// Inspect the wire counts before attempting a reload so that underflow fails cleanly.
		FILE *fp = fopen(path, "rb");
		CHECK(fp != NULL);
		if (!fp) return;
		UnsignedByte bytes[64] = {};
		const size_t length = fread(bytes, 1, sizeof(bytes), fp);
		fclose(fp);
		const size_t centerHeader = 4 + warehouseCount * sizeof(ObjectID);
		CHECK(length == centerHeader + 3);
		const Bool countsValid = bytes[2] == warehouseCount && bytes[3] == 0 &&
			bytes[centerHeader + 1] == 0 && bytes[centerHeader + 2] == 0;
		CHECK(countsValid);
		if (countsValid)
		{
			std::list<ObjectID> restoredWarehouses;
			std::list<ObjectID> restoredCenters;
			read.open(AsciiString(path));
			read.xferVersion(&version, 1);
			read.xferSTLObjectIDList(&restoredWarehouses);
			read.xferSTLObjectIDList(&restoredCenters);
			read.close();
			CHECK(restoredWarehouses == warehouses);
			CHECK(restoredCenters.empty());
		}
		remove(path);
	}
}
