﻿// 変数データテキスト生成クラス

#include <numeric>	// for accumulate
#include <algorithm>

#include "module/strf.h"
#include "module/ptr_cast.h"
#include "module/map_iterator.h"
#include "module/CStrBuf.h"

#include "main.h"
#include "SysvarData.h"
#include "CVarTree.h"
#include "CVarinfoText.h"
#include "CVardataString.h"

#ifdef with_WrapCall
# include "WrapCall/ResultNodeData.h"
using namespace WrapCall;
#endif

CVarinfoText::CVarinfoText()
	: writer_(std::make_shared<CStrBuf>())
{
	writer_.getBuf()->limit(std::max(0x100, g_config->maxlenVarinfo));
}

string const& CVarinfoText::getString() const {
	return getBuf()->get();
}
string&& CVarinfoText::getStringMove() {
	return getBuf()->getMove();
}

//------------------------------------------------
// 変数データから生成
//------------------------------------------------
void CVarinfoText::addVar( PVal* pval, char const* name )
{
	auto const hvp = hpimod::getHvp(pval->flag);
	int bufsize;
	void const* const pMemBlock =
		hvp->GetBlockSize(pval, ptr_cast<PDAT*>(pval->pt), ptr_cast<int*>(&bufsize));

	// 変数に関する情報
	getWriter().catln(strf("変数名：%s", name));
	getWriter().catln(strf("変数型：%s", getVartypeString(pval).c_str()));
	if ( g_config->showsVariableAddress ) {
		getWriter().catln(strf("アドレス：0x%08X, 0x%08X", address_cast(pval->pt), address_cast(pval->master)));
	}
	if ( g_config->showsVariableSize ) {
		getWriter().catln(strf("サイズ：using %d of %d [byte]", pval->size, bufsize));
	}
	getWriter().catCrlf();

	// 変数の内容に関する情報
	{
		auto vardat = CVardataStrWriter::create<CTreeformedWriter>(getBuf());
		vardat.addVar(name, pval);
	}
	getWriter().catCrlf();

	// メモリダンプ
	if ( g_config->showsVariableDump ) {
		 getWriter().catDump(pMemBlock, static_cast<size_t>(bufsize));
	}
}

//------------------------------------------------
// システム変数データから生成
//------------------------------------------------
void CVarinfoText::addSysvar(Sysvar::Id id)
{
	getWriter().catln(strf("変数名：%s\t(システム変数)", Sysvar::List[id].name));
	getWriter().catCrlf();
	{
		CVardataStrWriter::create<CTreeformedWriter>(getBuf())
			.addSysvar(id);
	}
	getWriter().catCrlf();

	// メモリダンプ
	if ( g_config->showsVariableDump ) {
		auto const&& dump = Sysvar::dump(id);
		getWriter().catDump(dump.first, dump.second);
	}
}

#if with_WrapCall
//------------------------------------------------
// 呼び出しデータから生成
// 
// @prm prmstk: nullptr => 引数未確定
//------------------------------------------------
void CVarinfoText::addCall(ModcmdCallInfo const& callinfo)
{
	auto const stdat = callinfo.stdat;
	auto const name = hpimod::STRUCTDAT_getName(stdat);
	getWriter().catln(
		(callinfo.fname == nullptr)
			? strf("関数名：%s", name)
			: strf("関数名：%s (#%d of %s)", name, callinfo.line + 1, callinfo.fname)
	);

	// シグネチャ
	getWriter().catln(strf("仮引数：(%s)", getPrmlistString(stdat).c_str()));
	getWriter().catCrlf();

	auto const prmstk = callinfo.getPrmstk();

	CVardataStrWriter::create<CTreeformedWriter>(getBuf())
			.addCall( stdat, prmstk );

	if ( prmstk && g_config->showsVariableDump ) {
		getWriter().catCrlf();
		getWriter().catDump(prmstk, static_cast<size_t>(stdat->size));
	}
}

//------------------------------------------------
// 返値データから生成
//------------------------------------------------
void CVarinfoText::addResult( stdat_t stdat, string const& text, char const* name )
{
	getWriter().catln(strf("関数名：%s", name));
//	getWriter().catln(strf("仮引数：(%s)", getPrmlistString(stdat).c_str()));
	getWriter().catCrlf();

	getWriter().cat(text);
//	getWriter().catCrlf();

	// メモリダンプ
	//if ( g_config->showsVariableDump ) { getWriter().catDump( ptr, static_cast<size_t>(bufsize) ); }
}

#endif

//**********************************************************
//        概観の追加
//**********************************************************
//------------------------------------------------
// [add] モジュール概観
//------------------------------------------------
void CVarinfoText::addModuleOverview(char const* name, CStaticVarTree const& tree)
{
	getWriter().catln(strf("[%s]", name));

	tree.foreach(
		[&](CStaticVarTree const& module) {
			// (入れ子の)モジュールは名前だけ表示しておく
			getWriter().catln(module.getName());
		},
		[&](string const& varname) {
			auto const varname_raw = removeScopeResolution(varname);
			getWriter().cat(varname_raw + "\t= ");
			{
				CVardataStrWriter::create<CLineformedWriter>(getBuf())
					.addVar(varname.c_str(), hpimod::seekSttVar(varname.c_str()));
			}
			getWriter().catCrlf();
		}
	);
}

//------------------------------------------------
// [add] システム変数概観
//------------------------------------------------
void CVarinfoText::addSysvarsOverview()
{
	getWriter().catln("[システム変数]");

	for ( int i = 0; i < Sysvar::Count; ++i ) {
		getWriter().cat(Sysvar::List[i].name);
		getWriter().cat("\t= ");
		{
			CVardataStrWriter::create<CLineformedWriter>(getBuf())
				.addSysvar(static_cast<Sysvar::Id>(i));
		}
		getWriter().catCrlf();
	}
}

#ifdef with_WrapCall
//------------------------------------------------
// [add] 呼び出し概観
// 
// depends on WrapCall
//------------------------------------------------
void CVarinfoText::addCallsOverview(ResultNodeData const* pLastResult)
{
	getWriter().catln("[呼び出し履歴]");

	auto const range = WrapCall::getCallInfoRange();
	std::for_each(range.first, range.second, [&](stkCallInfo_t::value_type const& pCallInfo) {
		CVardataStrWriter::create<CLineformedWriter>(getBuf())
			.addCall(pCallInfo->stdat, pCallInfo->getPrmstk());
		getWriter().catCrlf();
	});

	// 最後の返値
	if ( pLastResult ) {
		getWriter().cat("-> ");
		getWriter().catln(pLastResult->valueString);
	}
}
#endif

//------------------------------------------------
// [add] 全般概観
//------------------------------------------------
void CVarinfoText::addGeneralOverview() {
	getWriter().catln("[全般]");
	for ( auto&& kv : g_dbginfo->fetchGeneralInfo() ) {
		if ( Sysvar::seek(kv.first.c_str()) == Sysvar::MAX ) {
			getWriter().catln(kv.first + "\t= " + kv.second);
		}
	}
}

//**********************************************************
//        下請け関数
//**********************************************************
//------------------------------------------------
// mptype の文字列を得る
// todo: hpimod に移動？
//------------------------------------------------
char const* getMPTypeString(int mptype)
{
	switch ( mptype ) {
		case MPTYPE_NONE:        return "none";
		case MPTYPE_STRUCTTAG:   return "structtag";

		case MPTYPE_LABEL:       return "label";
		case MPTYPE_DNUM:        return "double";
		case MPTYPE_STRING:
		case MPTYPE_LOCALSTRING: return "str";
		case MPTYPE_INUM:        return "int";
		case MPTYPE_VAR:
		case MPTYPE_PVARPTR:				// #dllfunc
		case MPTYPE_SINGLEVAR:   return "var";
		case MPTYPE_ARRAYVAR:    return "array";
		case MPTYPE_LOCALVAR:    return "local";
		case MPTYPE_MODULEVAR:   return "thismod";//"modvar";
		case MPTYPE_IMODULEVAR:  return "modinit";
		case MPTYPE_TMODULEVAR:  return "modterm";

#if 0
		case MPTYPE_IOBJECTVAR:  return "comobj";
	//	case MPTYPE_LOCALWSTR:   return "";
	//	case MPTYPE_FLEXSPTR:    return "";
	//	case MPTYPE_FLEXWPTR:    return "";
		case MPTYPE_FLOAT:       return "float";
		case MPTYPE_PPVAL:       return "pval";
		case MPTYPE_PBMSCR:      return "bmscr";
		case MPTYPE_PTR_REFSTR:  return "prefstr";
		case MPTYPE_PTR_EXINFO:  return "pexinfo";
		case MPTYPE_PTR_DPMINFO: return "pdpminfo";
		case MPTYPE_NULLPTR:     return "nullptr";
#endif
		default: return "unknown";
	}
}

//------------------------------------------------
// 仮引数リストの文字列
//------------------------------------------------
string getPrmlistString(stdat_t stdat)
{
#if 0
	string s = "";

	//if ( stdat->prmmax == 0 ) s = "void";
	std::for_each(hpimod::STRUCTDAT_getStPrm(stdat), hpimod::STRUCTDAT_getStPrmEnd(stdat), [&](STRUCTPRM const& stprm) {
		if ( !s.empty() ) s += ", ";
		s += getMPTypeString(stprm.mptype);
	});
	return s;
#else
	auto const range = make_mapped_range(hpimod::STRUCTDAT_getStPrm(stdat), hpimod::STRUCTDAT_getStPrmEnd(stdat),
		[&](STRUCTPRM const& stprm) { return getMPTypeString(stprm.mptype); });
	return join(range.begin(), range.end(), ", ");
#endif
}

static char const* typeQualifierStringFromVarmode(varmode_t mode)
{
	return (mode == HSPVAR_MODE_NONE) ? "!" :
		(mode == HSPVAR_MODE_MALLOC) ? "" :
		(mode == HSPVAR_MODE_CLONE) ? "&" : "<err>";
}

// 変数の型を表す文字列
string getVartypeString(PVal const* pval)
{
	size_t const maxDim = hpimod::PVal_maxDim(pval);

	string const arrayType =
		(maxDim == 0) ? "(empty)" :
		(maxDim == 1) ? makeArrayIndexString(1, &pval->len[1]) :
		strf("%s (%d in total)", makeArrayIndexString(maxDim, &pval->len[1]).c_str(), hpimod::PVal_cntElems(pval))
	;

	return strf("%s%s %s",
		hpimod::getHvp(pval->flag)->vartype_name,
		typeQualifierStringFromVarmode(pval->mode),
		arrayType.c_str()
	);
}
