// Vita3K emulator project
// Copyright (C) 2025 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <shader/usse_types.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

using UniformBufferSizes = std::array<std::uint32_t, 15>;

struct SceGxmProgram;
enum SceGxmParameterType : uint8_t;

namespace shader::usse {
/**
 * \brief Check if an instruction is a branch.
 *
 * If the instruction is a branch, the passed references will be set
 * with the predicate and branch offset.
 *
 * \param inst   Instruction.
 * \param pred   Reference to the value which will contains predicate.
 * \param br_off Reference to the value which will contains branch offset.
 *
 * \returns True on instruction being a branch.
 */
bool is_kill(const std::uint64_t inst);
bool is_branch(const std::uint64_t inst, std::uint8_t &pred, std::int32_t &br_off);
bool is_buffer_fetch_or_store(const std::uint64_t inst, int &base, int &cursor, int &offset, int &size);
bool does_write_to_predicate(const std::uint64_t inst, std::uint8_t &pred);
std::uint8_t get_predicate(const std::uint64_t inst);

enum USSENodeType {
    USSE_ABSTRACT_NODE,
    USSE_BLOCK_NODE,
    USSE_CODE_NODE,
    USSE_CONDITIONAL_NODE,
    USSE_LOOP_NODE,
    USSE_BREAK_NODE,
    USSE_CONTINUE_NODE
};

class USSEBaseNode;
using USSEBaseNodeInstance = std::unique_ptr<USSEBaseNode>;

class USSEBaseNode {
private:
    USSENodeType type = USSE_ABSTRACT_NODE;

protected:
    std::vector<USSEBaseNodeInstance> children;
    USSEBaseNode *parent;

    USSEBaseNode *add_children_protected(USSEBaseNodeInstance &instance);

public:
    explicit USSEBaseNode(USSEBaseNode *parent, const USSENodeType type)
        : type(type)
        , parent(parent) {
    }

    USSENodeType node_type() const {
        return type;
    }

    USSEBaseNode *get_parent() const {
        return parent;
    }
    virtual ~USSEBaseNode() = default;

    std::size_t children_count() const {
        return children.size();
    }

    const USSEBaseNode *children_at(const std::size_t index) const {
        return children[index].get();
    }

    void reset() {
        children.clear();
    }
};

class USSEBlockNode : public USSEBaseNode {
private:
    std::uint32_t offset; // This is like metadata

public:
    explicit USSEBlockNode(USSEBaseNode *parent, const std::uint32_t start);

    USSEBaseNode *add_children(USSEBaseNodeInstance &instance);
    std::uint32_t start_offset() const {
        return offset;
    }
};

class USSECodeNode : public USSEBaseNode {
public:
    std::uint32_t offset;
    std::uint32_t size;

    std::uint8_t condition;

    explicit USSECodeNode(USSEBaseNode *parent);
};

class USSEConditionalNode : public USSEBaseNode {
protected:
    std::uint8_t neg_condition;
    std::uint32_t merge_point;

public:
    explicit USSEConditionalNode(USSEBaseNode *parent, const std::uint32_t merge_point);

    USSEBlockNode *if_block() const;
    USSEBlockNode *else_block() const;

    void set_if_block(USSEBaseNodeInstance &node);
    void set_else_block(USSEBaseNodeInstance &node);

    std::uint8_t negif_condition() const {
        return neg_condition;
    }

    void set_negif_condition(const std::uint8_t condition) {
        neg_condition = condition;
    }

    std::uint32_t get_merge_point() const {
        return merge_point;
    }
};

class USSELoopNode : public USSEBaseNode {
protected:
    std::uint32_t loop_end_offset;

public:
    explicit USSELoopNode(USSEBaseNode *parent, const std::uint32_t loop_end_offset);

    USSEBlockNode *content_block() const;
    void set_content_block(USSEBaseNodeInstance &node);

    std::uint32_t get_loop_end_offset() const {
        return loop_end_offset;
    }
};

class USSEBreakNode : public USSEBaseNode {
protected:
    std::uint8_t condition;

public:
    explicit USSEBreakNode(USSEBaseNode *parent, const std::uint8_t condition)
        : USSEBaseNode(parent, USSE_BREAK_NODE)
        , condition(condition) {
    }

    std::uint8_t get_condition() const {
        return condition;
    }
};

class USSEContinueNode : public USSEBaseNode {
protected:
    std::uint8_t condition;

public:
    explicit USSEContinueNode(USSEBaseNode *parent, const std::uint8_t condition)
        : USSEBaseNode(parent, USSE_CONTINUE_NODE)
        , condition(condition) {
    }

    std::uint8_t get_condition() const {
        return condition;
    }
};

struct AttributeInformation {
    uint16_t location;
    SceGxmParameterType gxm_type;
    uint8_t component_count;
    // this is needed for Vulkan as it doesn't implicitly convert between integers and floats and between signed and unsigned
    bool is_integer;
    // only meaningful is is_integer is true
    bool is_signed;
    bool regformat;

    explicit AttributeInformation()
        : location(0)
        , gxm_type(static_cast<SceGxmParameterType>(0))
        , component_count(0)
        , is_integer(false)
        , is_signed(false)
        , regformat(false) {
    }

    explicit AttributeInformation(uint16_t loc, SceGxmParameterType type, uint8_t count, bool is_integer, bool is_signed, bool regformat)
        : location(loc)
        , gxm_type(type)
        , component_count(count)
        , is_integer(is_integer)
        , is_signed(is_signed)
        , regformat(regformat) {
    }
};

using USSEOffset = uint32_t;

using UniformBufferMap = std::map<int, UniformBuffer>;
using AttributeInformationMap = std::map<int, AttributeInformation>;
using AnalyzeReadFunction = std::function<std::uint64_t(USSEOffset)>;

void get_attribute_informations(const SceGxmProgram &program, AttributeInformationMap &locmap);
// return the max used buffer index + 1
int get_uniform_buffer_sizes(const SceGxmProgram &program, UniformBufferSizes &sizes);

void analyze(USSEBlockNode &root, USSEOffset end_offset, const AnalyzeReadFunction &read_func);
} // namespace shader::usse
