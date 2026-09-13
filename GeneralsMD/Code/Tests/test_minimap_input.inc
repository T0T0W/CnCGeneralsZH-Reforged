// Exercise upstream camera input and GUI routing with lightweight subsystem fixtures.
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
class MinimapInputKeyboard : public Keyboard
{
public:
	Bool getCapsState() { return FALSE; }
	void getKey(KeyboardIO *key) { key->key = KEY_NONE; }
	void modifiers(Int flags) { m_modifiers = flags; }
};

class MinimapInputMouse : public Mouse
{
public:
	Int cursorChanges = 0, captures = 0, releases = 0;
	void initCursorResources() {}
	void setCursor(MouseCursor cursor) { m_currentCursor = cursor; ++cursorChanges; }
	void capture() { ++captures; }
	void releaseCapture() { ++releases; }
	UnsignedByte getMouseEvent(MouseIO*, Bool) { return 0; }
};

class MinimapInputUI : public InGameUI
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

class MinimapInputView : public View
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

struct MinimapEngineFixture
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
	MinimapInputUI *ui;
	MinimapInputView view;
	MinimapInputKeyboard keyboard;
	MinimapInputMouse mouse;

	MinimapEngineFixture()
	{
		global = NEW GlobalData;
		TheWritableGlobalData = global;
		// Like bootOnce's subsystems, keep one UI until process exit. Its production destructor
		// requires a fully built control bar and a device, which this input test does not create.
		static MinimapInputUI *testUI = NEW MinimapInputUI;
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
	~MinimapEngineFixture()
	{
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

#include "GameClient/WindowXlat.h"
namespace
{
class MinimapWindow : public GameWindow
{
	MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE(MinimapWindow, "MinimapWindow", 16, 16)
public:
	void winDrawBorder() {}
	void winSetFont(GameFont*) {}
};
MinimapWindow::~MinimapWindow() {}

class MinimapWindows : public GameWindowManager
{
public:
	GameWindow *allocateNewWindow() { return newInstance(MinimapWindow)(); }
	GameFont *winFindFont(AsciiString, Int, Bool) { return NULL; }
	GameWinDrawFunc getPushButtonImageDrawFunc() { return NULL; }
	GameWinDrawFunc getPushButtonDrawFunc() { return NULL; }
	GameWinDrawFunc getCheckBoxImageDrawFunc() { return NULL; }
	GameWinDrawFunc getCheckBoxDrawFunc() { return NULL; }
	GameWinDrawFunc getRadioButtonImageDrawFunc() { return NULL; }
	GameWinDrawFunc getRadioButtonDrawFunc() { return NULL; }
	GameWinDrawFunc getTabControlImageDrawFunc() { return NULL; }
	GameWinDrawFunc getTabControlDrawFunc() { return NULL; }
	GameWinDrawFunc getListBoxImageDrawFunc() { return NULL; }
	GameWinDrawFunc getListBoxDrawFunc() { return NULL; }
	GameWinDrawFunc getComboBoxImageDrawFunc() { return NULL; }
	GameWinDrawFunc getComboBoxDrawFunc() { return NULL; }
	GameWinDrawFunc getHorizontalSliderImageDrawFunc() { return NULL; }
	GameWinDrawFunc getHorizontalSliderDrawFunc() { return NULL; }
	GameWinDrawFunc getVerticalSliderImageDrawFunc() { return NULL; }
	GameWinDrawFunc getVerticalSliderDrawFunc() { return NULL; }
	GameWinDrawFunc getProgressBarImageDrawFunc() { return NULL; }
	GameWinDrawFunc getProgressBarDrawFunc() { return NULL; }
	GameWinDrawFunc getStaticTextImageDrawFunc() { return NULL; }
	GameWinDrawFunc getStaticTextDrawFunc() { return NULL; }
	GameWinDrawFunc getTextEntryImageDrawFunc() { return NULL; }
	GameWinDrawFunc getTextEntryDrawFunc() { return NULL; }
};

struct MinimapMessages
{
	MessageStream stream;
	CommandList commands;
	MessageStream *savedStream = TheMessageStream;
	CommandList *savedCommands = TheCommandList;
	MinimapMessages()
	{
		TheMessageStream = &stream;
		TheCommandList = &commands;
		stream.attachTranslator(NEW MetaEventTranslator, 20);
	}
	~MinimapMessages()
	{
		TheMessageStream = savedStream;
		TheCommandList = savedCommands;
	}
};
}
// Drive minimap gestures through the actual window and mouse message translators.
#include "Common/Radar.h"
#include "GameClient/GUICallbacks.h"

namespace
{
class MinimapTestRadar : public Radar
{
public:
	void configure(GameWindow *window, Real width = 1280, Real height = 1280)
	{
		m_radarWindow = window;
		m_mapExtent.lo.zero();
		m_mapExtent.hi.x = width;
		m_mapExtent.hi.y = height;
		m_xSample = width / RADAR_CELL_WIDTH;
		m_ySample = height / RADAR_CELL_HEIGHT;
		forceOn(TRUE);
	}
	void draw(Int, Int, Int, Int) {}
	void clearShroud() {}
	void setShroudLevel(Int, Int, CellShroudStatus) {}
};

class MinimapTestTerrain : public TerrainLogic
{
public:
	Real getGroundHeight(Real, Real, Coord3D* = NULL) const { return 25; }
};

class MinimapTestUI : public MinimapInputUI
{
public:
	DrawableList selected;
	const DrawableList *getAllSelectedLocalDrawables() { return &selected; }
};

struct MinimapInputFixture
{
	MinimapEngineFixture engine;
	MinimapWindows windows;
	MinimapTestRadar radar;
	MinimapTestTerrain terrain;
	Radar *savedRadar = TheRadar;
	TerrainLogic *savedTerrain = TheTerrainLogic;
	GameWindow *window;
	MinimapInputFixture()
	{
		TheWindowManager = &windows;
		TheRadar = &radar;
		TheTerrainLogic = &terrain;
		window = windows.winCreate(NULL, WIN_STATUS_ENABLED, 100, 200, 128, 128, NULL);
		window->winSetInputFunc(LeftHUDInput);
		radar.configure(window);
		static MinimapTestUI *ui = NEW MinimapTestUI;
		ui->selected.clear();
		ui->scrolling = ui->selecting = FALSE;
		ui->enabled = TRUE;
		TheInGameUI = ui;
	}
	~MinimapInputFixture()
	{
		windows.winDestroyAll();
		TheRadar = savedRadar;
		TheTerrainLogic = savedTerrain;
	}
	void event(MinimapMessages &messages, GameMessage::Type type, Int x, Int y)
	{
		GameMessage *message = messages.stream.appendMessage(type);
		ICoord2D pixel = {x, y};
		message->appendPixelArgument(pixel);
		if(type == GameMessage::MSG_RAW_MOUSE_LEFT_DRAG)
		{
			ICoord2D delta = {1, 1};
			message->appendPixelArgument(delta);
		}
		else
			message->appendIntegerArgument(0);
		message->appendIntegerArgument(100);
		messages.stream.propagateMessages();
	}
};
}

TEST(minimap_drag_tracks_held_left_click_and_stops_on_release_with_units_selected)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);
	// A selected unit must not turn left dragging into move orders.
	((MinimapTestUI*)TheInGameUI)->selected.push_back(NULL);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 220);
	CHECK(f.engine.view.lookAtCalls == 1);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 150, 250);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 190, 290);
	CHECK(f.engine.view.lookAtCalls == 3);
	CHECK(f.engine.view.getPosition()->x == 900);
	CHECK(f.engine.view.getPosition()->y == 380);
	CHECK(f.engine.view.getPosition()->z == 25);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 190, 290);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_POSITION, 130, 230);
	CHECK(f.engine.view.lookAtCalls == 3);
	CHECK(f.windows.winGetGrabWindow() == NULL);
	CHECK(messages.commands.getFirstMessage() == NULL);
}

TEST(minimap_drag_can_leave_and_reenter_then_release_outside_without_world_click)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 220);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 300, 400);
	CHECK(f.engine.view.lookAtCalls == 1);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 170, 270);
	CHECK(f.engine.view.lookAtCalls == 2);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 300, 400);
	CHECK(f.windows.winGetGrabWindow() == NULL);
	CHECK(messages.commands.getFirstMessage() == NULL);
	// A fresh world gesture must remain available after releasing the minimap.
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 300, 400);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 150, 250);
	CHECK(f.engine.view.lookAtCalls == 2);
}

TEST(minimap_drag_respects_map_letterboxing_and_works_during_keyboard_scroll)
{
	CHECK(bootOnce());
	MinimapInputFixture f;
	MinimapMessages messages;
	messages.stream.attachTranslator(NEW WindowTranslator, 10);
	f.radar.configure(f.window, 1280, 640);
	f.engine.view.setMouseLock(TRUE);
	TheInGameUI->setScrolling(TRUE);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 120, 240);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 170, 260);
	CHECK(f.engine.view.lookAtCalls == 2);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_DRAG, 170, 210);
	CHECK(f.engine.view.lookAtCalls == 2);
	f.event(messages, GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 300, 400);
	CHECK(f.windows.winGetGrabWindow() == NULL);
}
