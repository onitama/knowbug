﻿
#include <Windows.h>
#include <CommCtrl.h>
#include "module/supio/supio.h"

#include "main.h"
#include "dialog.h"
#include "config_mng.h"

#include "vartree.h"
#include "CVarTree.h"
#include "CVarinfoText.h"
#include "CVardataString.h"

#include "SysvarData.h"
#include "DebugInfo.h"

#define hwndVarTree (Dialog::getVarTreeHandle())

namespace VarTree
{

static CStaticVarTree const& getSttVarTree();
static vartype_t getVartypeOfNode(HTREEITEM hItem);
static void AddNodeModule(HTREEITEM hParent, CVarTree const& tree);
static void AddNodeSysvar();

#ifdef with_WrapCall
static HTREEITEM g_hNodeDynamic;
static void AddNodeDynamic();

// ツリービューに含まれる返値ノードのデータ
using resultDataPtr_t = std::shared_ptr<ResultNodeData>;
static std::map<HTREEITEM, resultDataPtr_t> g_allResultData;
static HTREEITEM g_lastIndependedResultNode;	// 非依存な返値ノード

// 動的ノードの追加・除去の遅延の管理
static size_t g_cntWillAddCallNodes = 0;						// 次の更新で追加すべきノード数
static std::vector<resultDataPtr_t> g_willAddResultNodes;		// 次の更新で追加すべき返値ノード
static resultDataPtr_t g_willAddResultNodeIndepend = nullptr;	// 次の更新で追加すべき非依存な返値ノード

static void AddCallNodeImpl(ModcmdCallInfo const& callinfo);
static void AddResultNodeImpl(std::shared_ptr<ResultNodeData> pResult);
static HTREEITEM FindDependedCallNode(ResultNodeData* pResult);
static void RemoveDependingResultNodes(HTREEITEM hItem);
static void RemoveLastIndependedResultNode();
static ResultNodeData* FindLastIndependedResultData();
#endif

static HTREEITEM TreeView_MyInsertItem(HTREEITEM hParent, char const* name, bool sorts, LPARAM lp);

//------------------------------------------------
// 変数ツリーの初期化
//------------------------------------------------
void init()
{
	AddNodeModule(TVI_ROOT, getSttVarTree());
#ifdef with_WrapCall
	AddNodeDynamic();
#endif
	AddNodeSysvar();

	//すべてのトップレベルノードを開く
	HTREEITEM const hRoot = TreeView_GetRoot(hwndVarTree);
	for ( HTREEITEM hNode = hRoot
		; hNode != nullptr
		; hNode = TreeView_GetNextSibling(hwndVarTree, hNode)
	) {
		TreeView_Expand(hwndVarTree, hNode, TVE_EXPAND);
	}

	// トップを表示するように仕向ける
	TreeView_EnsureVisible(hwndVarTree, hRoot);

	//グローバルノードを選択
	TreeView_SelectItem(hwndVarTree, hRoot);
}

//------------------------------------------------
// 変数ツリー終了時
//------------------------------------------------
void term()
{
#ifdef with_WrapCall
	if ( auto const hVarTree = Dialog::getVarTreeHandle() ) {
		RemoveDependingResultNodes(g_hNodeDynamic);

		// dynamic 関連のデータを削除する (必要なさそう)
		if ( usesResultNodes() ) {
			g_willAddResultNodeIndepend = nullptr;
			g_willAddResultNodes.clear();
		}
	}
#endif
}

//------------------------------------------------
// 静的変数リストを取得する
//------------------------------------------------
static CStaticVarTree const& getSttVarTree()
{
	static std::unique_ptr<CStaticVarTree> stt_tree;
	if ( !stt_tree ) {
		stt_tree.reset(new CStaticVarTree(CStaticVarTree::ModuleName_Global));

		std::unique_ptr<char, void(*)(char*)> p(
			g_dbginfo->debug->get_varinf(nullptr, 0xFF),
			g_dbginfo->debug->dbg_close
		);
		strsp_ini();
		for ( ;; ) {
			char name[0x100];
			int const chk = strsp_get(p.get(), name, 0, sizeof(name) - 1);
			if ( chk == 0 ) break;
			stt_tree->pushVar(name);
		}
	}
	return *stt_tree;
}

//------------------------------------------------
// 変数ツリーにノードを追加する
//------------------------------------------------
void AddNodeModule(HTREEITEM hParent, CStaticVarTree const& tree)
{
	auto const hElem = TreeView_MyInsertItem(hParent, tree.getName().c_str(), true, (LPARAM)(&tree));
	tree.foreach(
		[&](CStaticVarTree const& module) {
			AddNodeModule(hElem, module);
		},
		[&](string const& varname) {
			PVal* const pval = hpimod::seekSttVar(varname.c_str());
			assert(!!pval);
			TreeView_MyInsertItem(hElem, varname.c_str(), true, (LPARAM)pval);
		}
	);
}

//------------------------------------------------
// 変数ツリーにシステム変数ノードを追加する
//------------------------------------------------
void AddNodeSysvar()
{
	HTREEITEM const hNodeSysvar = TreeView_MyInsertItem( TVI_ROOT, "+sysvar", false, (LPARAM)0 );

	// システム変数のリストを追加する
	for ( int i = 0; i < Sysvar::Count; ++ i ) {
		string const name = strf( "~%s", Sysvar::List[i].name );
		TreeView_MyInsertItem( hNodeSysvar, name.c_str(), false, (LPARAM)i );
	}
}

#ifdef with_WrapCall
//------------------------------------------------
// 変数ツリーに動的変数ノードを追加する
//------------------------------------------------
void AddNodeDynamic()
{
	g_hNodeDynamic = TreeView_MyInsertItem(TVI_ROOT, "+dynamic", false, (LPARAM)0);
}
#endif

//------------------------------------------------
// 変数ツリーの NM_CUSTOMDRAW を処理する
//------------------------------------------------
LRESULT customDraw( LPNMTVCUSTOMDRAW pnmcd )
{
	if ( pnmcd->nmcd.dwDrawStage == CDDS_PREPAINT ) {
		return CDRF_NOTIFYITEMDRAW;

	} else if ( pnmcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT ) {
		auto const hItem = reinterpret_cast<HTREEITEM>(pnmcd->nmcd.dwItemSpec);

		// 選択状態なら色分けしない
		if ( TreeView_GetItemState(hwndVarTree, hItem, 0) & TVIS_SELECTED ) {
			return 0;
		}

		string const sItem = TreeView_GetItemString( hwndVarTree, hItem );
		char const* const name = sItem.c_str();

		// 呼び出しノード
		if ( isCallNode(name) ) {
			auto const idx = static_cast<int>(TreeView_GetItemLParam(hwndVarTree, hItem));
			assert(idx >= 0);
			if ( auto const pCallInfo = WrapCall::getCallInfoAt(idx) ) {
				auto const&& iter = g_config->clrTextExtra.find(
					(pCallInfo->stdat->index == STRUCTDAT_INDEX_CFUNC)
					? "__func__" : "__sttm__"
				);
				if ( iter != g_config->clrTextExtra.end() ) {
					pnmcd->clrText = iter->second;
					return CDRF_NEWFONT;
				}
			}

		// その他
		} else {
			vartype_t const vtype = getVartypeOfNode(hItem);
			if ( 0 < vtype && vtype < HSPVAR_FLAG_USERDEF ) {
				pnmcd->clrText = g_config->clrText[vtype];
				return CDRF_NEWFONT;

			} else if ( vtype >= HSPVAR_FLAG_USERDEF ) {
				auto const&& iter = g_config->clrTextExtra.find(hpimod::getHvp(vtype)->vartype_name);
				pnmcd->clrText = (iter != g_config->clrTextExtra.end())
					? iter->second
					: g_config->clrText[HSPVAR_FLAG_NONE];
				return CDRF_NEWFONT;
			}
		}
	}
	return 0;
}

//------------------------------------------------
// 変数ツリーの要素の型を取得する
//------------------------------------------------
vartype_t getVartypeOfNode( HTREEITEM hItem )
{
	string const&& name = TreeView_GetItemString(hwndVarTree, hItem);

	if ( isVarNode(name.c_str()) ) {
		PVal* const pval = hpimod::seekSttVar( name.c_str() );
		if ( pval ) return pval->flag;

	} else if ( isSysvarNode(name.c_str()) ) {
		Sysvar::Id const id = static_cast<Sysvar::Id>(TreeView_GetItemLParam(hwndVarTree, hItem));
		assert(id == Sysvar::seek(&name[1]));
		if ( id != Sysvar::Id::MAX ) {
			return Sysvar::List[id].type;
		}

	} else if ( isResultNode(name.c_str()) ) {
		auto const&& iter = g_allResultData.find(hItem);
		if ( iter != g_allResultData.end() ) {
			return iter->second->vtype;
		}
	}
	return HSPVAR_FLAG_NONE;
}

//------------------------------------------------
// 変数情報のテキストを取得する
//------------------------------------------------
string getItemVarText( HTREEITEM hItem )
{
	string const&& itemText = TreeView_GetItemString( hwndVarTree, hItem );
	char const* const name = itemText.c_str();

	CVarinfoText varinf;

	// ノード
	if ( isModuleNode(name) || isSystemNode(name) ) {
		//auto const varinf = std::make_unique<CVarinfoLine>(g_config->maxlenVarinfo);
#ifdef with_WrapCall
		if ( strcmp(name, "+dynamic") == 0 ) {
			varinf.addCallsOverview(FindLastIndependedResultData());
		} else
#endif
		if ( strcmp(name, "+sysvar") == 0 ) {
			varinf.addSysvarsOverview();

		} else {
			assert(isModuleNode(name));
			auto const pTree = reinterpret_cast<CStaticVarTree*>(
				TreeView_GetItemLParam(hwndVarTree, hItem)
			);
			assert(pTree != nullptr);
			varinf.addModuleOverview(name, *pTree);
		}

	// リーフ
	} else {
		if ( isSysvarNode(name) ) {
			Sysvar::Id const id = static_cast<Sysvar::Id>(TreeView_GetItemLParam(hwndVarTree, hItem));
			assert(0 <= id && id < Sysvar::Count && Sysvar::seek(&name[1]) == id);
			varinf.addSysvar(id);

	#ifdef with_WrapCall
		} else if ( isCallNode(name) ) {
			auto const idx = static_cast<int>(
				TreeView_GetItemLParam( hwndVarTree, hItem )
			);
			assert(idx >= 0);
			if ( auto const pCallInfo = WrapCall::getCallInfoAt(idx) ) {
				varinf.addCall(*pCallInfo);
			}

		// 返値データ
		} else if ( usesResultNodes() && isResultNode(name) ) {
			auto const&& iter = g_allResultData.find(hItem);
			auto const pResult = (iter != g_allResultData.end() ? iter->second : nullptr);
			varinf.addResult( pResult->stdat, pResult->valueString, hpimod::STRUCTDAT_getName(pResult->stdat) );
	#endif
		// 静的変数
		} else {
			assert(isVarNode(name));
			auto const pval = reinterpret_cast<PVal*>(TreeView_GetItemLParam(hwndVarTree, hItem));
			assert(pval && ctx->mem_var <= pval && pval < &ctx->mem_var[hpimod::cntSttVars()]);
			varinf.addVar( pval, name );
		}
	}
	return varinf.getStringMove();
}

#ifdef with_WrapCall
//------------------------------------------------
// 呼び出しノードを追加
//------------------------------------------------
void AddCallNode(ModcmdCallInfo const& callinfo)
{
	// 非依存な返値ノードを除去
	if ( usesResultNodes() ) {
		RemoveLastIndependedResultNode();
	}

	if ( !Knowbug::isStepRunning() ) {
		// 次に停止したときにまとめて追加する
		++g_cntWillAddCallNodes;
	} else {
		AddCallNodeImpl(callinfo);
	}
	}

void AddCallNodeImpl(ModcmdCallInfo const& callinfo)
{
	char name[128] = "'";
	strcpy_s(&name[1], sizeof(name) - 1, hpimod::STRUCTDAT_getName(callinfo.stdat));
	HTREEITEM const hChild = TreeView_MyInsertItem(g_hNodeDynamic, name, false, (LPARAM)callinfo.idx);

	// 第一ノードなら自動的に開く
	if ( TreeView_GetChild( hwndVarTree, g_hNodeDynamic ) == hChild ) {
		TreeView_Expand( hwndVarTree, g_hNodeDynamic, TVE_EXPAND );
	}
}

//------------------------------------------------
// 最後の呼び出しノードを削除
//------------------------------------------------
void RemoveLastCallNode()
{
	if ( g_cntWillAddCallNodes > 0 ) {
		--g_cntWillAddCallNodes;		// やっぱり追加しない

	} else {
		// 末子に返値ノードがあれば削除する
		if ( usesResultNodes() ) {
			RemoveLastIndependedResultNode();
		}

		HTREEITEM const hLast = TreeView_GetChildLast(hwndVarTree, g_hNodeDynamic);
		if ( !hLast ) return;
		assert( isCallNode(TreeView_GetItemString(hwndVarTree, hLast).c_str()) );

		TreeView_EscapeFocus(hwndVarTree, hLast);
		RemoveDependingResultNodes(hLast);
		TreeView_DeleteItem(hwndVarTree, hLast);
	}
}

//------------------------------------------------
// 返値ノードを追加
/*
返値データ ptr の生存期間は今だけなので、今のうちに文字列化しなければいけない。
返値ノードも、呼び出しノードと同様に、次に実行が停止したときにまとめて追加する。

「A( B() )」のように、ユーザ定義コマンドの引数式の中でユーザ定義関数が呼ばれている状態を、
「A は B に依存する」と表現することにする。A もユーザ定義関数である場合のみ考える。
このとき B の実行が終了してから A の実行が始まる。
B の返値ノードは、A の呼び出しノードの子ノードとして追加される。

表示がごちゃごちゃしないように、返値ノードは近いうちに削除される。
具体的には、以下の通り：
1. 非依存な返値ノードは、次に呼び出しノードか非依存な返値ノードが追加される直前、
	または呼び出しノードが削除される直前に取り除かれる。
2. 依存する返値ノードは、その依存先の呼び出しノードが削除されるときに取り除かれる。
3. 実行が終了したとき、すべての返値ノードが取り除かれる。
*/
//------------------------------------------------
void AddResultNode(ModcmdCallInfo const& callinfo, std::shared_ptr<ResultNodeData> pResult)
{
	assert(!!pResult);

	// 実行中 => 次に停止したときに追加する
	if ( !Knowbug::isStepRunning() ) {
		if ( pResult->pCallInfoDepended ) {
			g_willAddResultNodes.push_back(pResult);
		} else {
			g_willAddResultNodeIndepend = pResult;
		}
	} else {
		AddResultNodeImpl(pResult);
	}
}

void AddResultNodeImpl(std::shared_ptr<ResultNodeData> pResult)
{
	HTREEITEM const hParent = FindDependedCallNode(pResult.get());
	if ( !hParent ) return;

	// 非依存な返値ノードは高々1個に限られる
	if ( hParent == g_hNodeDynamic ) {
		RemoveLastIndependedResultNode();
	}

	// 挿入
	char name[128] = "\"";
	strcpy_s( &name[1], sizeof(name) - 1, hpimod::STRUCTDAT_getName(pResult->stdat) );
	HTREEITEM const hChild = TreeView_MyInsertItem(hParent, name, false, (LPARAM)0);

	// 第一ノードなら自動的に開く
	if ( TreeView_GetChild( hwndVarTree, hParent ) == hChild ) {
		TreeView_Expand( hwndVarTree, hParent, TVE_EXPAND );
	}

	// 返値ノードデータを保存しておく
	g_allResultData.insert({ hChild, pResult });

	if ( hParent == g_hNodeDynamic ) {
		g_lastIndependedResultNode = hChild;
	}
}

//------------------------------------------------
// 依存元の呼び出しノードを探す (failure: nullptr)
//
// @ 依存元がツリービューになければ失敗とする。
//------------------------------------------------
HTREEITEM FindDependedCallNode(ResultNodeData* pResult)
{
	// 依存されているなら、その呼び出しノードを検索する
	if ( pResult->pCallInfoDepended ) {
		HTREEITEM hItem = nullptr;
		for ( hItem = TreeView_GetChild(hwndVarTree, g_hNodeDynamic)
			; hItem != nullptr
			; hItem = TreeView_GetNextSibling(hwndVarTree, hItem)
		) {
			int const idx = static_cast<int>(TreeView_GetItemLParam(hwndVarTree, hItem));
			assert(idx >= 0);
			if ( WrapCall::getCallInfoAt(idx) == pResult->pCallInfoDepended ) break;
		}
		return hItem;

	// 非依存なら、+dynamic 直下に追加する
	} else {
		return g_hNodeDynamic;
	}
}

//------------------------------------------------
// 返値ノードを削除
//------------------------------------------------
void RemoveResultNode(HTREEITEM hResult)
{
	// 現状返値ノードに依存する返値ノードは挿入されない
	//RemoveDependingResultNodes(hResult);

	TreeView_EscapeFocus(hwndVarTree, hResult);

	// 関連していた返値ノードデータを破棄
	{
		size_t const cnt = g_allResultData.erase(hResult);
		assert(cnt == 1);
	}

	TreeView_DeleteItem(hwndVarTree, hResult);
}

//------------------------------------------------
// 依存している返値ノードをすべて削除する
//------------------------------------------------
static void RemoveDependingResultNodes(HTREEITEM hItem)
{
	if ( !usesResultNodes() ) return;

	// +dynamic 直下の返値ノードは非依存なものであり、それは末子の高々1つに限られる
	if ( hItem == g_hNodeDynamic ) {
		RemoveLastIndependedResultNode();
		return;
	}

	DbgArea {
		string const nodeName = TreeView_GetItemString(hwndVarTree, hItem);
		assert(isCallNode(nodeName.c_str()) || isResultNode(nodeName.c_str()));
	};

	for ( HTREEITEM hChild = TreeView_GetChild(hwndVarTree, hItem)
		; hChild != nullptr
		;
	) {
		HTREEITEM const hNext = TreeView_GetNextSibling(hwndVarTree, hChild);
		string const nodeName = TreeView_GetItemString(hwndVarTree, hChild);
		if ( isResultNode(nodeName.c_str()) ) {
			RemoveResultNode(hChild);
		}
		hChild = hNext;
	}
}

//------------------------------------------------
// (最後の)非依存な返値ノードを削除する
//------------------------------------------------
void RemoveLastIndependedResultNode()
{
	if ( g_lastIndependedResultNode ) {
		RemoveResultNode(g_lastIndependedResultNode);
		g_lastIndependedResultNode = nullptr;
	}
	g_willAddResultNodeIndepend = nullptr;
}

//------------------------------------------------
// (最後の)非依存な返値ノードデータを探す
//------------------------------------------------
ResultNodeData* FindLastIndependedResultData()
{
	if ( !g_lastIndependedResultNode ) return nullptr;
	auto const&& iter = g_allResultData.find(g_lastIndependedResultNode);
	return (iter != g_allResultData.end())
		? iter->second.get()
		: nullptr;
}

//------------------------------------------------
// 呼び出しノード更新
//------------------------------------------------
void UpdateCallNode()
{
	// 追加予定の呼び出しノードを実際に追加する
	if ( g_cntWillAddCallNodes > 0 ) {
		auto const&& range = WrapCall::getCallInfoRange();
		size_t const lenStk = std::distance(range.first, range.second);
		for ( size_t i = lenStk - g_cntWillAddCallNodes; i < lenStk; ++i ) {
			AddCallNodeImpl(*(range.first[i]));
		}
		g_cntWillAddCallNodes = 0;
	}

	// 追加予定の返値ノードを実際に追加する
	if ( usesResultNodes() ) {
		// 非依存なもの
		if ( g_willAddResultNodeIndepend ) {
			AddResultNodeImpl(g_willAddResultNodeIndepend);
			g_willAddResultNodeIndepend = nullptr;
		}

		// 依存されているもの
		if ( !g_willAddResultNodes.empty() ) {
			for ( auto const& pResult : g_willAddResultNodes ) {
				AddResultNodeImpl(pResult);
			}
			g_willAddResultNodes.clear();
		}
	}
}
#endif

//------------------------------------------------
// ツリービューに要素を挿入する
//------------------------------------------------
static HTREEITEM TreeView_MyInsertItem(HTREEITEM hParent, char const* name, bool sorts, LPARAM lp)
{
	TVINSERTSTRUCT tvis {};
	tvis.hParent = hParent;
	tvis.hInsertAfter = (sorts ? TVI_SORT : TVI_LAST);
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
	tvis.item.pszText = const_cast<char*>(name);
	tvis.item.lParam = lp;
	return TreeView_InsertItem(hwndVarTree, &tvis);
}

} // namespace VarTree

//------------------------------------------------
// ResultNodeData コンストラクタ
//------------------------------------------------
#include "module/CStrBuf.h"
namespace WrapCall
{
	ResultNodeData::ResultNodeData(ModcmdCallInfo const& callinfo, PDAT* ptr, vartype_t vt)
		: stdat(callinfo.stdat)
		, vtype(vt)
		, pCallInfoDepended(callinfo.getDependedCallInfo())
	{
		auto p = std::make_shared<CStrBuf>();
		CVardataStrWriter::create<CLineformedWriter>(p)
			.addResult(hpimod::STRUCTDAT_getName(stdat), ptr, vt);
		valueString = p->getMove();
	}
}