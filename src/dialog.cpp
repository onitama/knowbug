﻿
#pragma comment(lib, "comctl32.lib")

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <fstream>

#include "main.h"
#include "resource.h"
#include "hspwnd.h"
#include "module/GuiUtility.h"
#include "module/strf.h"
#include "WrapCall/WrapCall.h"

#include "dialog.h"
#include "vartree.h"
#include "config_mng.h"
#include "DebugInfo.h"

#include "module/supio/supio.h"

#ifdef _M_X64
# define KnowbugPlatformString "(x64)"
#else //defined(_M_X64)
# define KnowbugPlatformString "(x86)"
#endif //defined(_M_X64)
#define KnowbugAppName "Knowbug"
#define KnowbugVersion "1.22.2" " " KnowbugPlatformString
static char const* const KnowbugMainWindowTitle = KnowbugAppName " " KnowbugVersion;
static char const* const KnowbugViewWindowTitle = "Knowbug View";
static char const* const KnowbugRepoUrl = "https://github.com/vain0/knowbug";

namespace Dialog
{

static auto const countStepButtons = size_t { 5 };

static HWND hVarTree;
static HWND hSrcLine;
static HWND hViewEdit;

struct Resource
{
	window_handle_t mainWindow, viewWindow;
	menu_handle_t dialogMenu, nodeMenu, invokeMenu, logMenu;
	unique_ptr<VTView> tv;

	std::array<HWND, countStepButtons> stepButtons;
	gdi_obj_t font;
};
static auto g_res = unique_ptr<Resource> {};

auto getVarTreeHandle() -> HWND { return hVarTree; }

static auto windowHandles() -> std::vector<HWND>
{
	return std::vector<HWND> { g_res->mainWindow.get(), g_res->viewWindow.get() };
}

static void setEditStyle(HWND hEdit, int maxlen);

namespace View {

void setText(char const* text)
{
	HSPAPICHAR *hactmp1;
	SetWindowText(hViewEdit, chartoapichar(text,&hactmp1));
	freehac(&hactmp1);
};

void scroll(int y, int x)
{
	Edit_Scroll(hViewEdit, y, x);
}

void scrollBottom()
{
	scroll(Edit_GetLineCount(hViewEdit), 0);
}

void selectLine(size_t index)
{
	Edit_SetSel(hViewEdit
		, Edit_LineIndex(hViewEdit, index)
		, Edit_LineIndex(hViewEdit, index + 1));
}

void saveCurrentCaret()
{
	g_res->tv->saveCurrentViewCaret(Edit_GetFirstVisibleLine(hViewEdit));
}


void update()
{
	g_res->tv->updateViewWindow();
}

} // namespace View

bool logsCalling()
{
	return g_config->logsInvocation;
}

namespace LogBox {

	void clear()
	{
		HSPAPICHAR *hactmp1 = 0;
		if ( ! g_config->warnsBeforeClearingLog
			|| MessageBox(g_res->mainWindow.get()
					, TEXT("ログをすべて消去しますか？"), chartoapichar(KnowbugAppName,&hactmp1), MB_OKCANCEL
					) == IDOK
		) {
			VTRoot::log().clear();
		}
		freehac(&hactmp1);
	}

	void save(char const* filepath) {
		HSPAPICHAR *hactmp1 = 0;
		if ( ! VTRoot::log().save(filepath) ) {
			MessageBox(g_res->mainWindow.get()
				, TEXT("ログの保存に失敗しました。"), chartoapichar(KnowbugAppName,&hactmp1), MB_OK);
		}
		freehac(&hactmp1);
	}
	void save() {
		static auto const filter =
			"log text(*.txt;*.log)\0*.txt;*.log\0All files(*.*)\0*.*\0\0";
		auto path =
			Dialog_SaveFileName(g_res->mainWindow.get()
				, filter, "log", "hspdbg.log");
		if ( path ) {
			save(path->c_str());
		}
	}
} //namespace LogBox

// ソース小窓の更新
static void UpdateCurInfEdit(hpiutil::SourcePos const& spos)
{
	auto curinf = spos.toString();
	HSPAPICHAR *hactmp1;

	if ( auto p = VTRoot::script().fetchScriptLine(spos) ) {
		SetWindowText(hSrcLine, chartoapichar((curinf + "\r\n" + *p).c_str(),&hactmp1));

	} else {
		SetWindowText(hSrcLine, chartoapichar(curinf.c_str(),&hactmp1));
	}
}

static void CurrentUpdate()
{
	UpdateCurInfEdit(g_dbginfo->curPos());
}

// ツリーノードのコンテキストメニュー
void VarTree_PopupMenu(HTREEITEM hItem, POINT pt)
{
	struct GetPopMenu
		: public VTNodeData::Visitor
	{
		void fInvoke(VTNodeInvoke const&) override
		{
			hPop = g_res->invokeMenu.get();
		}
		void fLog(VTNodeLog const&) override
		{
			hPop = g_res->logMenu.get();
		}
		auto apply(VTNodeData const& node) -> HMENU
		{
			hPop = g_res->nodeMenu.get(); // default
			node.acceptVisitor(*this);
			return hPop;
		}
	private:
		HMENU hPop;
	};

	auto node = g_res->tv->tryGetNodeData(hItem);
	if ( ! node ) return;
	auto const hPop = GetPopMenu {}.apply(*node);

	// ポップアップメニューを表示する
	auto const idSelected =
		TrackPopupMenuEx
			( hPop, (TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY | TPM_RETURNCMD)
			, pt.x, pt.y, g_res->mainWindow.get(), nullptr);

	switch ( idSelected ) {
		case 0: break;
		case IDC_NODE_UPDATE: View::update(); break;
		case IDC_NODE_LOG: {
			Knowbug::logmes(g_res->tv->getItemVarText(hItem)->c_str());
			break;
		}
#ifdef with_WrapCall
		case IDC_NODE_STEP_OUT: {
			if ( auto nodeInvoke = dynamic_cast<VTNodeInvoke const*>(node) ) {
				// 対象が呼び出された階層まで進む
				Knowbug::runStepReturn(nodeInvoke->callinfo().sublev);
			}
			break;
		}
#endif //defined(with_WrapCall)
		case IDC_LOG_AUTO_SCROLL: {
			Menu_ToggleCheck(hPop, IDC_LOG_AUTO_SCROLL, g_config->scrollsLogAutomatically);
			break;
		}
		case IDC_LOG_INVOCATION: {
			Menu_ToggleCheck(hPop, IDC_LOG_INVOCATION, g_config->logsInvocation);
			break;
		}
		case IDC_LOG_SAVE: LogBox::save(); break;
		case IDC_LOG_CLEAR: LogBox::clear(); break;
		default: assert_sentinel;
	}
}

static void resizeMainWindow(size_t cx, size_t cy, bool repaints)
{
	if ( ! g_res ) return;

	auto const sourceLineBoxSizeY = 50;
	auto const buttonSizeX = cx / countStepButtons;
	auto const buttonSizeY = 20;
	auto const tvSizeY     = cy - (sourceLineBoxSizeY + buttonSizeY);

	MoveWindow(hVarTree
		, 0, 0
		, cx, tvSizeY
		, repaints);
	MoveWindow(hSrcLine
		, 0, tvSizeY
		, cx, sourceLineBoxSizeY
		, repaints);

	for ( auto i = size_t { 0 }; i < countStepButtons; ++ i ) {
		MoveWindow(g_res->stepButtons[i]
			, i * buttonSizeX
			, tvSizeY + sourceLineBoxSizeY
			, buttonSizeX, buttonSizeY
			, repaints);
	}
}

// メインウィンドウのコールバック関数
LRESULT CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
	HSPAPICHAR *hactmp1;
	switch ( msg ) {
		case WM_COMMAND:
			switch ( LOWORD(wp) ) {
				case IDC_BTN1: Knowbug::run();         break;
				case IDC_BTN2: Knowbug::runStepIn();   break;
				case IDC_BTN3: Knowbug::runStop();     break;
				case IDC_BTN4: Knowbug::runStepOver(); break;
				case IDC_BTN5: Knowbug::runStepOut();  break;

				case IDC_TOPMOST: {
					Menu_ToggleCheck(g_res->dialogMenu.get(), IDC_TOPMOST, g_config->bTopMost);
					for ( auto&& hwnd : windowHandles() ) {
						Window_SetTopMost(hwnd, g_config->bTopMost);
					}
					break;
				}
				case IDC_OPEN_CURRENT_SCRIPT: {
					if ( auto p = VTRoot::script().resolveRefName(g_dbginfo->curPos().fileRefName()) ) {
						ShellExecute(nullptr, TEXT("open")
							, chartoapichar(p->c_str(), &hactmp1) , nullptr, TEXT(""), SW_SHOWDEFAULT);
						freehac(&hactmp1);
					}
					break;
				}
				case IDC_OPEN_INI: {
					//create empty file if not exist
					auto of = std::ofstream { g_config->selfPath(), std::ios::app };
					ShellExecute(nullptr, TEXT("open")
						, chartoapichar(g_config->selfPath().c_str(),&hactmp1), nullptr, TEXT(""), SW_SHOWDEFAULT);
					freehac(&hactmp1);
					break;
				}
				case IDC_UPDATE: {
					View::update();
					break;
				}
				case IDC_OPEN_KNOWBUG_REPOS: {
					ShellExecute(nullptr, TEXT("open")
						, chartoapichar(KnowbugRepoUrl,&hactmp1), nullptr, TEXT(""), SW_SHOWDEFAULT);
					freehac(&hactmp1);
					break;
				}
				case IDC_GOTO_LOG: {
					g_res->tv->selectNode(VTRoot::log());
					break;
				}
				case IDC_GOTO_SCRIPT: {
					g_res->tv->selectNode(VTRoot::script());
					break;
				}
			}
			break;

		case WM_CONTEXTMENU: {
			if ( (HWND)wp == hVarTree ) {
				auto pt = POINT { LOWORD(lp), HIWORD(lp) };
				if ( auto hItem = TreeView_GetItemAtPoint(hVarTree, pt) ) {
					VarTree_PopupMenu(hItem, pt);
					return TRUE;
				}
			}
			break;
		}
		case WM_NOTIFY: {
			auto const nmhdr = reinterpret_cast<LPNMHDR>(lp);
			if ( nmhdr->hwndFrom == hVarTree ) {
				switch ( nmhdr->code ) {
					case NM_DBLCLK:
					case NM_RETURN:
					case TVN_SELCHANGED:
						View::update();
						break;
					case TVN_SELCHANGING:
						View::saveCurrentCaret();
						break;
					case NM_CUSTOMDRAW: {
						if ( ! g_config->bCustomDraw ) break;
						auto const res =
							g_res->tv->customDraw(reinterpret_cast<LPNMTVCUSTOMDRAW>(nmhdr));
						SetWindowLongPtr(hDlg, DWLP_MSGRESULT, res);
						return TRUE;
					}
				}
			}
			break;
		}
		case WM_SIZE:
			resizeMainWindow(LOWORD(lp), HIWORD(lp), true);
			break;
		case WM_CREATE: return TRUE;
		case WM_CLOSE: return FALSE;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}
	return DefWindowProc(hDlg, msg, wp, lp);
}

LRESULT CALLBACK ViewDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch ( msg ) {
		case WM_CREATE: return TRUE;
		case WM_CLOSE: return FALSE;
		case WM_SIZE:
			MoveWindow(hViewEdit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
			break;
	}
	return DefWindowProc(hDlg, msg, wp, lp);
}

void Dialog::createMain()
{
	auto const dispx = GetSystemMetrics(SM_CXSCREEN);
	auto const dispy = GetSystemMetrics(SM_CYSCREEN);

	auto const mainSizeX = 234, mainSizeY = 380;
	auto const viewSizeX = g_config->viewSizeX, viewSizeY = g_config->viewSizeY;
	auto const viewPosX = !g_config->viewPosXIsDefault ? g_config->viewPosX : dispx - mainSizeX - viewSizeX;
	auto const viewPosY = !g_config->viewPosYIsDefault ? g_config->viewPosY : 0;

	//ビューウィンドウ
	auto hViewWnd = window_handle_t {
		Window_Create
			( "KnowbugViewWindow", ViewDialogProc
			, KnowbugViewWindowTitle, (WS_THICKFRAME)
			, viewSizeX, viewSizeY
			, viewPosX, viewPosY
			, Knowbug::getInstance()
			) };
	SetWindowLongPtr(hViewWnd.get(), GWL_EXSTYLE
		, GetWindowLongPtr(hViewWnd.get(), GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
	{
		auto const hPane =
			CreateDialog(Knowbug::getInstance()
				, (LPCTSTR)IDD_VIEW_PANE
				, hViewWnd.get(), (DLGPROC)ViewDialogProc);
		hViewEdit = GetDlgItem(hPane, IDC_VIEW);
		setEditStyle(hViewEdit, g_config->maxLength);

		ShowWindow(hPane, SW_SHOW);
	}

	//メインウィンドウ
	auto hDlgWnd = window_handle_t {
		Window_Create
			( "KnowbugMainWindow", DlgProc
			, KnowbugMainWindowTitle, WS_THICKFRAME
			, mainSizeX, mainSizeY
			, dispx - mainSizeX, 0
			, Knowbug::getInstance()
			) };
	{
		auto const hPane =
			CreateDialog(Knowbug::getInstance()
				, (LPCTSTR)IDD_MAIN_PANE
				, hDlgWnd.get(), (DLGPROC)DlgProc);
		ShowWindow(hPane, SW_SHOW);

		//メニューバー
		auto hDlgMenu = menu_handle_t { LoadMenu(Knowbug::getInstance(), (LPCTSTR)IDR_MAIN_MENU) };
		SetMenu(hDlgWnd.get(), hDlgMenu.get());

		//ポップメニュー
		auto const hNodeMenuBar = LoadMenu(Knowbug::getInstance(), (LPCTSTR)IDR_NODE_MENU);

		//いろいろ
		hVarTree = GetDlgItem(hPane, IDC_VARTREE);
		hSrcLine = GetDlgItem(hPane, IDC_SRC_LINE);

		// メンバの順番に注意
		g_res.reset(new Resource
			{ std::move(hDlgWnd)
			, std::move(hViewWnd)
			, std::move(hDlgMenu)
			, menu_handle_t { GetSubMenu(hNodeMenuBar, 0) } // node
			, menu_handle_t { GetSubMenu(hNodeMenuBar, 1) } // invoke
			, menu_handle_t { GetSubMenu(hNodeMenuBar, 2) } // log
			, std::make_unique<VTView>()
			, {{
				  GetDlgItem(hPane, IDC_BTN1)
				, GetDlgItem(hPane, IDC_BTN2)
				, GetDlgItem(hPane, IDC_BTN3)
				, GetDlgItem(hPane, IDC_BTN4)
				, GetDlgItem(hPane, IDC_BTN5) }}
			, gdi_obj_t {
					Font_Create
						( g_config->fontFamily.c_str()
						, g_config->fontSize
						, g_config->fontAntialias ) }
			});
	}

	if ( g_config->bTopMost ) {
		CheckMenuItem(g_res->dialogMenu.get(), IDC_TOPMOST, MF_CHECKED);
		for ( auto&& hwnd : windowHandles() ) {
			Window_SetTopMost(hwnd, true);
		}
	}
	if ( g_config->scrollsLogAutomatically ) {
		CheckMenuItem(g_res->logMenu.get(), IDC_LOG_AUTO_SCROLL, MF_CHECKED);
	}
	if ( g_config->logsInvocation ) {
		CheckMenuItem(g_res->logMenu.get(), IDC_LOG_INVOCATION, MF_CHECKED);
	}

	for ( auto&& hwnd : { hSrcLine, hViewEdit } ) {
		SendMessage(hwnd, WM_SETFONT
			, (WPARAM)(g_res->font.get())
			, /* repaints = */ FALSE);
	}

	{
		RECT rc; GetClientRect(g_res->mainWindow.get(), &rc);
		resizeMainWindow(rc.right, rc.bottom, false);
	}
	{
		RECT rc; GetClientRect(g_res->viewWindow.get(), &rc);
		MoveWindow(hViewEdit, 0, 0, rc.right, rc.bottom, FALSE);
	}

	for ( auto&& hwnd : windowHandles() ) {
		UpdateWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);
	}
}

void Dialog::destroyMain()
{
	g_res.reset();
}

// 一時停止時に dbgnotice から呼ばれる
void update()
{
	CurrentUpdate();
	g_res->tv->update();
}

void setEditStyle( HWND hEdit, int maxlen )
{
	Edit_SetTabLength(hEdit, g_config->tabwidth);
	SendMessage(hEdit, EM_SETLIMITTEXT, (WPARAM)maxlen, 0);
}

} // namespace Dialog

// 公開API

EXPORT auto WINAPI knowbug_hwnd() -> HWND
{
	return Dialog::g_res->mainWindow.get();
}

EXPORT auto WINAPI knowbug_hwndView() -> HWND
{
	return Dialog::g_res->viewWindow.get();
}
