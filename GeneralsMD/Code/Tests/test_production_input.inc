// Exercise the actual GUI hit testing/button callbacks and strip input translator.
#include "GameClient/PlaceEventTranslator.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/WindowXlat.h"

namespace
{
class ProductionInputWindow : public GameWindow
{
	MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE(ProductionInputWindow, "ProductionInputWindow", 16, 16)
public:
	void winDrawBorder() {}
	void winSetFont(GameFont*) {}
};
ProductionInputWindow::~ProductionInputWindow() {}

class ProductionInputWindows : public GameWindowManager
{
public:
	GameWindow *allocateNewWindow() { return newInstance(ProductionInputWindow)(); }
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

struct ProductionClickCount
{
	Int left = 0, right = 0;
	static WindowMsgHandledType system(GameWindow *window, UnsignedInt msg, WindowMsgData, WindowMsgData)
	{
		ProductionClickCount *count = (ProductionClickCount*)window->winGetUserData();
		if(msg == GBM_SELECTED) ++count->left;
		if(msg == GBM_SELECTED_RIGHT) ++count->right;
		return MSG_HANDLED;
	}
};

struct ProductionButtonFixture
{
	CameraInputFixture engine;
	ProductionInputWindows windows;
	ProductionClickCount count;
	GameWindow *parent, *button;
	ICoord2D pixel = {30, 30};
	ProductionButtonFixture(UnsignedInt layer = 0)
	{
		TheWindowManager = &windows;
		parent = windows.winCreate(NULL, WIN_STATUS_ENABLED | layer, 0, 0, 200, 200,
			ProductionClickCount::system);
		parent->winSetUserData(&count);
		button = windows.winCreate(parent, WIN_STATUS_RIGHT_CLICK | WIN_STATUS_CANCEL_WHEN_DISABLED,
			20, 20, 40, 40, GadgetPushButtonSystem);
		button->winSetInputFunc(GadgetPushButtonInput);
		button->winSetOwner(parent);
		button->winGetInstanceData()->m_status |= WIN_STATUS_RIGHT_CLICK;
	}
	~ProductionButtonFixture()
	{
		// Dispose of windows while their global manager and mouse are still installed.
		windows.winDestroyAll();
	}
	WinInputReturnCode click(GameWindowMessage msg) { return windows.winProcessMouseEvent(msg, &pixel, NULL); }
};

class ProductionStripTestUI : public CameraTestUI
{
public:
	void staleCameo()
	{
		for(Int row = 0; row < PRODUCTION_STRIP_ROWS; ++row) m_productionStripCount[row] = 0;
		m_productionStripCameoW = m_productionStripCameoH = 40;
		m_productionStripCount[0] = 1;
		ProductionStripSlot &slot = m_productionStrip[0][0];
		slot.producer = INVALID_ID; // producer died after the last draw; the strip still owns the click
		slot.pos.x = slot.pos.y = 20;
	}
};

struct ProductionStripFixture
{
	CameraInputFixture engine;
	PlaceEventTranslator strip;
	ProductionStripFixture()
	{
		static ProductionStripTestUI *ui = NEW ProductionStripTestUI;
		ui->staleCameo();
		TheInGameUI = ui;
	}
	GameMessageDisposition event(GameMessage::Type type, Int x = 30, Int y = 30)
	{
		GameMessage *message = newInstance(GameMessage)(type);
		ICoord2D pixel = {x, y};
		message->appendPixelArgument(pixel);
		GameMessageDisposition result = strip.translateGameMessage(message);
		message->deleteInstance();
		return result;
	}
};

struct ProductionMessageFixture
{
	MessageStream stream;
	CommandList commands;
	MessageStream *savedStream = TheMessageStream;
	CommandList *savedCommands = TheCommandList;
	ProductionMessageFixture()
	{
		TheMessageStream = &stream;
		TheCommandList = &commands;
		stream.attachTranslator(NEW MetaEventTranslator, 20);
		stream.attachTranslator(NEW PlaceEventTranslator, 30);
	}
	~ProductionMessageFixture()
	{
		TheMessageStream = savedStream;
		TheCommandList = savedCommands;
	}
	void append(GameMessage::Type type, Int x, Int y)
	{
		GameMessage *message = stream.appendMessage(type);
		ICoord2D pixel = {x, y};
		message->appendPixelArgument(pixel);
		if(type == GameMessage::MSG_RAW_MOUSE_RIGHT_DRAG)
			message->appendPixelArgument(pixel);
		else
			message->appendIntegerArgument(0); // modifiers
		message->appendIntegerArgument(100); // event time
	}
};
}

TEST(production_cancel_disabled_button_accepts_only_right_click_once)
{
	CHECK(bootOnce());
	ProductionButtonFixture f;
	CHECK(!(f.button->winGetStatus() & WIN_STATUS_ENABLED));
	CHECK(f.click(GWM_RIGHT_DOWN) == WIN_INPUT_USED);
	CHECK(f.count.right == 1 && f.count.left == 0);
	CHECK(f.click(GWM_RIGHT_UP) == WIN_INPUT_USED);
	CHECK(f.count.right == 1 && f.count.left == 0);
	f.click(GWM_LEFT_DOWN);
	f.click(GWM_LEFT_UP);
	CHECK(f.count.right == 1 && f.count.left == 0);
	CHECK(!(f.button->winGetStatus() & WIN_STATUS_ENABLED));
}

TEST(production_cancel_respects_hidden_disabled_parents_and_ordinary_buttons)
{
	CHECK(bootOnce());
	ProductionButtonFixture f;
	f.button->winClearStatus(WIN_STATUS_CANCEL_WHEN_DISABLED);
	f.click(GWM_RIGHT_DOWN); f.click(GWM_RIGHT_UP);
	CHECK(f.count.right == 0);
	f.button->winSetStatus(WIN_STATUS_CANCEL_WHEN_DISABLED);
	f.button->winHide(TRUE);
	f.click(GWM_RIGHT_DOWN); f.click(GWM_RIGHT_UP);
	CHECK(f.count.right == 0);
	f.button->winHide(FALSE);
	f.parent->winEnable(FALSE);
	f.click(GWM_RIGHT_DOWN); f.click(GWM_RIGHT_UP);
	CHECK(f.count.right == 0);
	f.parent->winEnable(TRUE);
	f.click(GWM_RIGHT_DOWN); f.click(GWM_RIGHT_UP);
	CHECK(f.count.right == 1);
}

TEST(production_cancel_works_in_above_below_modal_and_captured_windows)
{
	CHECK(bootOnce());
	for(UnsignedInt layer : {WIN_STATUS_ABOVE, WIN_STATUS_BELOW})
	{
		ProductionButtonFixture f(layer);
		CHECK(f.click(GWM_RIGHT_DOWN) == WIN_INPUT_USED);
		f.click(GWM_RIGHT_UP);
		CHECK(f.count.right == 1);
	}
	ProductionButtonFixture f;
	f.windows.winSetModal(f.parent);
	CHECK(f.click(GWM_RIGHT_DOWN) == WIN_INPUT_USED);
	f.click(GWM_RIGHT_UP);
	CHECK(f.count.right == 1);
	f.windows.winUnsetModal(f.parent);
	f.windows.winCapture(f.parent);
	CHECK(f.click(GWM_RIGHT_DOWN) == WIN_INPUT_USED);
	f.click(GWM_RIGHT_UP);
	CHECK(f.count.right == 2);
}

TEST(production_cancel_reusing_a_button_clears_disabled_cancellation)
{
	CHECK(bootOnce());
	ProductionButtonFixture f;
	GadgetButtonSetData(f.button, &f.count);
	CHECK(!(f.button->winGetStatus() & WIN_STATUS_CANCEL_WHEN_DISABLED));
	f.click(GWM_RIGHT_DOWN); f.click(GWM_RIGHT_UP);
	CHECK(f.count.right == 0);
}

TEST(production_cancel_strip_consumes_press_drag_release_and_double_click)
{
	CHECK(bootOnce());
	ProductionStripFixture f;
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_DRAG, 100, 100) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 100, 100) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_MOUSE_RIGHT_CLICK) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 100, 100) == KEEP_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_MOUSE_RIGHT_DOUBLE_CLICK) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_MOUSE_RIGHT_CLICK) == KEEP_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 60, 30) == KEEP_MESSAGE);
	CHECK(!TheInGameUI->handleProductionStripClick(NULL, TRUE));
}

TEST(production_cancel_strip_preserves_left_click_and_independent_button_releases)
{
	CHECK(bootOnce());
	ProductionStripFixture f;
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP, 100, 100) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_MOUSE_LEFT_CLICK) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 100, 100) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_MOUSE_RIGHT_CLICK) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP) == KEEP_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_LEFT_DOUBLE_CLICK) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP) == DESTROY_MESSAGE);
	CHECK(f.event(GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK) == DESTROY_MESSAGE);
}

TEST(production_cancel_strip_new_press_recovers_when_gui_consumed_the_previous_release)
{
	CHECK(bootOnce());
	ProductionStripFixture f;
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN) == DESTROY_MESSAGE);
	// The player released over a window, so WindowTranslator consumed the release before us.
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 100) == KEEP_MESSAGE);
	CHECK(f.event(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 100, 100) == KEEP_MESSAGE);
	CHECK(f.event(GameMessage::MSG_MOUSE_RIGHT_CLICK) == KEEP_MESSAGE);
}

TEST(production_cancel_strip_filters_real_meta_clicks_without_blocking_world_clicks)
{
	CHECK(bootOnce());
	ProductionStripFixture engine;
	ProductionMessageFixture f;
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 30, 30);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 30, 30);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK, 30, 30);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 30, 30);
	f.stream.propagateMessages();
	CHECK(f.commands.getFirstMessage() == NULL);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 100);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 100, 100);
	f.stream.propagateMessages();
	Int worldClicks = 0;
	for(GameMessage *m = f.commands.getFirstMessage(); m; m = m->next())
		if(m->getType() == GameMessage::MSG_MOUSE_RIGHT_CLICK) ++worldClicks;
	CHECK(worldClicks == 1);
}

TEST(production_cancel_disabled_button_release_outside_does_not_create_world_click)
{
	CHECK(bootOnce());
	ProductionButtonFixture engine;
	ProductionMessageFixture f;
	f.stream.attachTranslator(NEW WindowTranslator, 10);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 30, 30);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_DRAG, 300, 300);
	f.append(GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP, 300, 300);
	f.stream.propagateMessages();
	CHECK(engine.count.right == 1 && engine.count.left == 0);
	CHECK(f.commands.getFirstMessage() == NULL);
}
