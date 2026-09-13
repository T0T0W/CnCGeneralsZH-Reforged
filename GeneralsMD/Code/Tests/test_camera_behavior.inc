// Exercise upstream camera input and GUI routing with lightweight subsystem fixtures.
#include "GameClient/LookAtXlat.h"
#include "GameClient/CameraBoundary.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Keyboard.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "Common/StatsCollector.h"
#include "Common/PlayerList.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/GameLogic.h"

namespace
{
class CameraTestKeyboard : public Keyboard
{
public:
	Bool getCapsState() { return FALSE; }
	void getKey(KeyboardIO *key) { key->key = KEY_NONE; }
	void modifiers(Int flags) { m_modifiers = flags; }
};

class CameraTestMouse : public Mouse
{
public:
	Int cursorChanges = 0, captures = 0, releases = 0;
	void initCursorResources() {}
	void setCursor(MouseCursor cursor) { m_currentCursor = cursor; ++cursorChanges; }
	void capture() { ++captures; }
	void releaseCapture() { ++releases; }
	UnsignedByte getMouseEvent(MouseIO*, Bool) { return 0; }
};

class CameraTestUI : public InGameUI
{
public:
	void draw() {}
	View *createView() { return NULL; }
	Bool scrolling = FALSE, selecting = FALSE, enabled = TRUE;
	void setScrolling(Bool value) { InGameUI::setScrolling(value); scrolling = value; }
	Bool isScrolling() { return scrolling; }
	Bool isSelecting() { return selecting; }
	Bool getInputEnabled() { return enabled; }
};

class CameraTestView : public View
{
public:
	using View::getPosition;
	using View::setPosition;
	Coord2D last = {0, 0};
	Int samples = 0;
	Int constraintRecalcs = 0;
	Bool terrainHit = FALSE;
	Coord3D terrainPoint = {0, 0, 0};
	Int terrainPicks = 0;
	Int lookAtCalls = 0;
	void lookAt(const Coord3D *pos) { ++lookAtCalls; setPosition(pos); }
	void forceCameraConstraintRecalc() { ++constraintRecalcs; }
	void scrollBy(Coord2D *delta) { last = *delta; ++samples; }
	Drawable *pickDrawable(const ICoord2D*, Bool, PickType) { return NULL; }
	Int iterateDrawablesInRegion(IRegion2D*, Bool (*)(Drawable*, void*), void*) { return 0; }
	void forceRedraw() {}
	const Coord3D& get3DCameraPosition() const { return m_pos; }
	WorldToScreenReturn worldToScreenTriReturn(const Coord3D*, ICoord2D*) { return WTS_INSIDE_FRUSTUM; }
	void screenToWorld(const ICoord2D*, Coord3D*) {}
	Bool screenToTerrain(const ICoord2D*, Coord3D *world)
	{ ++terrainPicks; *world = terrainPoint; return terrainHit; }
	void screenToWorldAtZ(const ICoord2D*, Coord3D *world, Real z)
	{ *world = terrainPoint; world->z = z; }
	void drawView() {}
	void updateView() {}
	void setGuardBandBias(const Coord2D*) {}
};

struct CameraInputFixture
{
	GlobalData *savedGlobal = TheWritableGlobalData;
	InGameUI *savedUI = TheInGameUI;
	View *savedView = TheTacticalView;
	Keyboard *savedKeyboard = TheKeyboard;
	Mouse *savedMouse = TheMouse;
	Shell *savedShell = TheShell;
	GameWindowManager *savedWindows = TheWindowManager;
	StatsCollector *savedStats = TheStatsCollector;
	PlayerList *savedPlayers = ThePlayerList;
	RankInfoStore *savedRanks = TheRankInfoStore;
	GameLogic *savedLogic = TheGameLogic;
	GlobalData *global;
	CameraTestUI *ui;
	CameraTestView view;
	CameraTestKeyboard keyboard;
	CameraTestMouse mouse;
	LookAtTranslator camera;

	CameraInputFixture()
	{
		global = NEW GlobalData;
		TheWritableGlobalData = global;
		// Like bootOnce's subsystems, keep one UI until process exit. Its production destructor
		// requires a fully built control bar and a device, which this input test does not create.
		static CameraTestUI *testUI = NEW CameraTestUI;
		ui = testUI;
		ui->scrolling = ui->selecting = FALSE;
		ui->enabled = TRUE;
		TheInGameUI = ui;
		TheTacticalView = &view;
		TheKeyboard = &keyboard;
		TheMouse = &mouse;
		TheShell = NULL;
		TheWindowManager = NULL;
		TheStatsCollector = NULL;
		static RankInfoStore *ranks = NEW RankInfoStore;
		TheRankInfoStore = ranks;
		static PlayerList *players = NEW PlayerList;
		ThePlayerList = players;
		static GameLogic *logic = NEW GameLogic;
		TheGameLogic = logic;
		global->m_saveCameraInReplay = FALSE;
	}
	~CameraInputFixture()
	{
		camera.resetModes();
		TheInGameUI = savedUI;
		TheTacticalView = savedView;
		TheKeyboard = savedKeyboard;
		TheMouse = savedMouse;
		TheShell = savedShell;
		TheWindowManager = savedWindows;
		TheStatsCollector = savedStats;
		ThePlayerList = savedPlayers;
		TheRankInfoStore = savedRanks;
		TheGameLogic = savedLogic;
		TheWritableGlobalData = savedGlobal;
		delete global;
	}
};
}

TEST(camera_margin_allows_map_corners_but_bounds_large_pans)
{
	const Region3D maps[] = {
		{{0, 0, 0}, {3000, 2000, 100}},
		{{-500, 700, -20}, {-450, 730, 80}} // small map with a nonzero origin
	};
	for (Int m = 0; m < 2; ++m)
	{
		const Region3D& map = maps[m];
		const Region2D boundary = cameraBoundaryFromMap(map, 200.0f);
		for (Int x = 0; x < 2; ++x)
			for (Int y = 0; y < 2; ++y)
			{
				Coord3D corner = {x ? map.hi.x : map.lo.x, y ? map.hi.y : map.lo.y, 42.0f};
				Coord3D allowed = constrainCameraPosition(corner, boundary);
				CHECK(allowed == corner); // every map corner must remain reachable
				corner.x += x ? 100.0f : -100.0f;
				corner.y += y ? 100.0f : -100.0f;
				allowed = constrainCameraPosition(corner, boundary);
				CHECK(allowed == corner); // there is useful room outside both edges
				Coord3D outsideMap = {x ? 1e7f : -1e7f, y ? 1e7f : -1e7f, corner.z};
				Coord3D limited = constrainCameraPosition(outsideMap, boundary);
				CHECK_NEAR(limited.x, (x ? map.hi.x + 200.0f : map.lo.x - 200.0f), 0.00001f);
				CHECK_NEAR(limited.y, (y ? map.hi.y + 200.0f : map.lo.y - 200.0f), 0.00001f);
				CHECK_EQ(limited.z, corner.z);
				Coord3D inward = limited;
				inward.x += x ? -10.0f : 10.0f;
				inward.y += y ? -10.0f : 10.0f;
				allowed = constrainCameraPosition(inward, boundary);
				CHECK(allowed == inward); // reaching a corner must not trap the camera
			}
	}
}

TEST(camera_margin_preferences_load_and_rebuild_constraints)
{
	CHECK(bootOnce());
	CameraInputFixture f;
	UserPreferences pref;
	pref[AsciiString("UseCameraConstraints")] = AsciiString("Yes");
	pref[AsciiString("CameraBoundaryMargin")] = AsciiString("300");
	loadOptionsFromPreferences(pref);
	CHECK(f.global->m_useCameraConstraints);
	CHECK_EQ(f.global->m_cameraBoundaryMargin, 300);
	CHECK_EQ(f.view.constraintRecalcs, 1);
}
