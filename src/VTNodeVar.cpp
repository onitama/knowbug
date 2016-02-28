
#include "main.h"
#include "VarTreeNodeData.h"
#include "module/strf.h"

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
