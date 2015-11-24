
#pragma once

#include <map>
#include <unordered_set>
#include "hpiutil.hpp"

namespace hpiutil {
	
static unsigned short wpeek(unsigned char const* p)
{
	return *reinterpret_cast<unsigned short const*>(p);
}

static unsigned int tripeek(unsigned char const* p)
{
	return (p[0] | p[1] << 8 | p[2] << 16);
}

class DInfo
{
	DInfo() { parse(); }

public:
	static DInfo& instance();

private:
	using ident_table_t = std::map<int, char const*>;
	using cs_map_t = std::map<std::pair<char const*, int>, csptr_t>;

	std::unordered_set<std::string> fileRefNames_;
	ident_table_t labelNames_;
	ident_table_t paramNames_;
	cs_map_t csMap_;

public:
	char const* tryFindIdent(ident_table_t const& table, int iparam) const
	{
		auto const iter = table.find(iparam);
		return (iter != table.end()) ? iter->second : nullptr;
	}

	char const* tryFindLabelName(int otIndex) const
	{
		return tryFindIdent(labelNames_, otIndex);
	}

	char const* tryFindParamName(int stprmIndex) const
	{
		return tryFindIdent(paramNames_, stprmIndex);
	}

	auto fileRefNames() const -> decltype(fileRefNames_) const&
	{
		return fileRefNames_;
	}

	void parse()
	{
		auto&& tryFindIdentTableFromCtx = [&](int dictx) -> ident_table_t* {
			switch ( dictx ) {
				case 0: return nullptr; // �ϐ����͋L�^���Ȃ�
				case 1: return &labelNames_;
				case 2: return &paramNames_;
				default: throw; //unreachable
			}
		};

		csptr_t cur_cs = ctx->mem_mcs;
		char const* cur_fname;
		int cur_line;

		auto&& pushPoint = [&]() {
			csMap_.emplace(std::make_pair(cur_fname, cur_line), cur_cs);
		};

		int dictx = 0; // Default context

		for ( int i = 0; i < ctx->hsphed->max_dinfo; ) {
			switch ( ctx->mem_di[i] ) {
				case 0xFF: // �����̋�؂�
					dictx++;
					i++;
					break;

				case 0xFE: // �\�[�X�t�@�C���w��
				{
					int const idxDs = tripeek(&ctx->mem_di[i + 1]);
					int const line = wpeek(&ctx->mem_di[i + 4]);

					if ( idxDs != 0 ) {
						cur_fname = strData(idxDs);
						fileRefNames_.emplace(cur_fname);
					}
					cur_line = line;
					i += 6;
					break;
				}
				// ���ʎq�w��
				case 0xFD:
				case 0xFB:
					if ( auto const tbl = tryFindIdentTableFromCtx(dictx) ) {
						auto const ident = strData(tripeek(&ctx->mem_di[i + 1]));
						int const iparam = wpeek(&ctx->mem_di[i + 4]);
						tbl->emplace(iparam, ident);
					}
					i += 6;
					break;

				case 0xFC: // ���̖��߂܂ł�CS�I�t�Z�b�g�l
					cur_cs += *reinterpret_cast<csptr_t>(&ctx->mem_di[i + 1]);
					pushPoint();
					i += 3;
					break;
				default:
					cur_cs += ctx->mem_di[i];
					pushPoint();
					i++;
					break;
			}
		}
	}
};

} // namespace hpiutil