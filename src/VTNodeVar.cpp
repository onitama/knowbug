
#include "main.h"
#include "VarTreeNodeData.h"
#include "module/strf.h"

bool VTNodeVar::updateSub(bool deep)
{
	if ( deep ) {
		// �^���ς�邩�A�v�f�������邩�A�᎟���̗v�f�����������Ȃ�A�č\�z���s��
		if ( pval_->flag != pvalBak_.flag
			|| memcmp(&pvalBak_.len[1], &pval_->len[1], hpiutil::ArrayDimMax * sizeof(int))
				> 0
			) {
			children_.clear();
		}

		// �����v�f�͍X�V���A�V�K�v�f�͑}������
		{
			size_t const oldLen = children_.size();
			size_t const newLen = hpiutil::PVal_cntElems(pval_);

			for ( size_t i = 0; i < newLen; i++ ) {
				PDAT const* const pdat = hpiutil::PVal_getPtr(pval_, i);

				if ( i < oldLen ) {
					children_[i]->resetPtr(pdat);

				} else {
					auto&& name =
						hpiutil::stringifyArrayIndex(hpiutil::PVal_indexesFromAptr(pval_, i));

					children_.emplace_back(std::make_shared<VTNodeValue>
						( this, std::move(name)
						, pdat, pval_->flag
						));
				}

				children_[i]->updateDownDeep();
			}
		}
	}
	pvalBak_ = *pval_;
	return true;
}

void VTNodeVector::addChild(int i)
{
	if ( dimIndex_ == 0 ) {
		// TODO: ������₷�����O������
		auto&& name = strf("(%d)", i);

		children_.emplace_back(std::make_shared<VTNodeValue>
			( this, std::move(name)
			, hpiutil::PVal_getPtr(pval_, aptr_ + i), pval_->flag
			));
	} else {
		// TODO: �������z�������
	}
}

bool VTNodeVector::updateSub(bool deep)
{
	if ( deep ) {
		int const newLen = pval_->len[1 + dimIndex_];

		// var �̎q�m�[�h�Ǘ��̎d�g�݂ɂ��A����̗v�f������������A�^���ς�邱�Ƃ͂��肦�Ȃ�
		// (�����Ȃ����炱�ꎩ�̂��j�������)
		assert(len_ <= newLen);

		for ( int i = len_; i < newLen; i++ ) {
			addChild(i);
		}

		for ( auto& e : children_ ) {
			e->updateDownDeep();
		}
	}
	return true;
}
