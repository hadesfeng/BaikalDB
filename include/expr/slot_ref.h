// Copyright (c) 2018-present Baidu, Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "expr_node.h"

namespace baikaldb {
class SlotRef : public ExprNode {
public:
    SlotRef()  {
        _is_constant = false;
    }
    virtual int init(const pb::ExprNode& node) {
        ExprNode::init(node);
        _tuple_id = node.derive_node().tuple_id();
        _slot_id = node.derive_node().slot_id();
        return 0;
    }
    virtual ExprValue get_value(MemRow* row) {
        if (row == NULL) {
            return ExprValue::Null();
        }
        return row->get_value(_tuple_id, _slot_id).cast_to(_col_type);
    }

    SlotRef* clone() {
        SlotRef* s = new SlotRef;
        s->_tuple_id = _tuple_id;
        s->_slot_id = _slot_id;
        s->_node_type = _node_type;
        s->_col_type = _col_type;
        return s;
    }

    int32_t tuple_id() {
        return _tuple_id;
    }
    int32_t slot_id() {
        return _slot_id;
    }
    int32_t field_id() {
        return _field_id;
    }
    void set_field_id(int32_t field_id) {
        _field_id = field_id;
    }
    virtual void transfer_pb(pb::ExprNode* pb_node) {
        ExprNode::transfer_pb(pb_node);
        pb_node->mutable_derive_node()->set_tuple_id(_tuple_id);
        pb_node->mutable_derive_node()->set_slot_id(_slot_id);
    }

private:
    int32_t _tuple_id;
    int32_t _slot_id;
    int32_t _field_id; //ICP 时用

    friend ExprNode;
};
}

/* vim: set ts=4 sw=4 sts=4 tw=100 */
