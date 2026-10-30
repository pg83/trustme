#include "mir_helpers.h"

#include "output.h"
#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_type.h"
#include "trans_target.h"
#include "hir_encoded_literal.h"
#include "hir_typeck_monomorph.h"

#include <std/lib/vector.h>

#include <algorithm>

using namespace stl;

namespace {
    struct LValueCbVisitor: public MIRVisitor {
        const MIRLvalueCallback& cb;

        explicit LValueCbVisitor(const MIRLvalueCallback& cb);

        bool visitLvalue(const MIRLValue& lv, MIRValUsage u) override;
    };

    struct ValueLifetime {
        Vector<bool> stmtBitmap;

        ValueLifetime(size_t stmtCount);

        void fill(const Vector<size_t>& blockOffsets, size_t bb, size_t firstStmt, size_t lastStmt);

    };

    void MIRHelperGetLifetimesDetermineValueLifetime(MIRTypeResolve& state, const MIRFunction& fcn, size_t bbIdx, size_t stmtIdx, const MIRLValue& lv, const Vector<size_t>& blockOffsets, const Vector<bool>& useBitmap, ValueLifetime& vl);

    void MIRHelperGetLifetimesDetermineValueLifetime(MIRTypeResolve& localMirRes, const MIRFunction& fcn, size_t bbIdx, size_t stmtIdx, const MIRLValue& lv, const Vector<size_t>& blockOffsets, const Vector<bool>& useBitmap, ValueLifetime& vl) {
        struct State {
            const Vector<size_t>& blockOffsets;
            ValueLifetime& outVl;

            Vector<unsigned int> bbHistory;
            size_t lastReadOfs;
            bool isBorrowed_;

            State(const Vector<size_t>& blockOffsets, ValueLifetime& vl, size_t initBbIdx, size_t initStmtIdx)
                : blockOffsets(blockOffsets)
                , outVl(vl)
                , bbHistory()
                , lastReadOfs(initStmtIdx)
                , isBorrowed_(false)
            {
                bbHistory.pushBack(initBbIdx);
            }

            State(State&& x)
                : blockOffsets(x.blockOffsets)
                , outVl(x.outVl)
                , bbHistory(mv$(x.bbHistory))
                , lastReadOfs(x.lastReadOfs)
                , isBorrowed_(x.isBorrowed_)
            {
            }

            State& operator=(State&& x) {
                this->bbHistory = mv$(x.bbHistory);
                this->lastReadOfs = x.lastReadOfs;
                this->isBorrowed_ = x.isBorrowed_;
                return *this;
            }

            State clone() const {
                State rv{blockOffsets, outVl, 0, lastReadOfs};
                rv.bbHistory = bbHistory;
                rv.isBorrowed_ = isBorrowed_;
                return rv;
            }

            bool isBorrowed() const {
                return this->isBorrowed_;
            }

            void markBorrowed(size_t stmtIdx) {
                if (!isBorrowed_) {
                    isBorrowed_ = false;
                    this->fillTo(stmtIdx);
                }
                isBorrowed_ = true;
            }

            void markRead(size_t stmtIdx) {
                if (!isBorrowed_) {
                    this->fillTo(stmtIdx);
                } else {
                    isBorrowed_ = false;
                    this->fillTo(stmtIdx);
                    isBorrowed_ = true;
                }
            }

            void fmt(ZeroCopyOutput& os) const {
                os << StringView("BB") << bbHistory[0] << StringView("/") << lastReadOfs << StringView("--");
                os << StringView("[") << bbHistory << StringView("]");
            }

            void finalise(size_t stmtIdx) {
                if (isBorrowed_) {
                    isBorrowed_ = false;
                    this->fillTo(stmtIdx);
                    isBorrowed_ = true;
                }
            }

            void fillTo(size_t stmtIdx) {
                TRACE_FUNCTION_F(FMT_CB(ss, this->fmt(ss);));
                BUG_ASSERT(!isBorrowed_);
                BUG_ASSERT(bbHistory.length() > 0);
                if (bbHistory.length() == 1) {
                    outVl.fill(blockOffsets, bbHistory[0], lastReadOfs, stmtIdx);
                } else {
                    auto initBbIdx = bbHistory[0];
                    auto limit0 = blockOffsets[initBbIdx + 1] - blockOffsets[initBbIdx] - 1;
                    outVl.fill(blockOffsets, initBbIdx, lastReadOfs, limit0);

                    for (size_t i = 1; i < bbHistory.length() - 1; i++) {
                        size_t bbIdx = bbHistory[i];
                        BUG_ASSERT(bbIdx + 1 < blockOffsets.length());
                        size_t limit = blockOffsets[bbIdx + 1] - blockOffsets[bbIdx] - 1;
                        outVl.fill(blockOffsets, bbIdx, 0, limit);
                    }

                    auto bbIdx = bbHistory.back();
                    outVl.fill(blockOffsets, bbIdx, 0, stmtIdx);
                }

                lastReadOfs = stmtIdx;

                auto cur = this->bbHistory.back();
                this->bbHistory.clear();
                this->bbHistory.pushBack(cur);
            }
        };

        struct Runner {
            MIRTypeResolve& mirRes;
            const MIRFunction& fcn;
            size_t initBbIdx;
            size_t initStmtIdx;
            const MIRLValue& lv;
            const Vector<size_t>& blockOffsets;
            ValueLifetime& lifetimes;
            bool isCopy;

            Vector<bool> visitedStatements;

            std::vector<std::pair<size_t, State>> statesToDo;

            Runner(MIRTypeResolve& localMirRes, const MIRFunction& fcn, size_t initBbIdx, size_t initStmtIdx, const MIRLValue& lv, const Vector<size_t>& blockOffsets, ValueLifetime& vl)
                : mirRes(localMirRes)
                , fcn(fcn)
                , initBbIdx(initBbIdx)
                , initStmtIdx(initStmtIdx)
                , lv(lv)
                , blockOffsets(blockOffsets)
                , lifetimes(vl)
                , visitedStatements()
            {
                visitedStatements.zero(lifetimes.stmtBitmap.length());
                const HIRType* tmp;
                isCopy = mirRes.resolve.typeIsCopy(localMirRes.sp, mirRes.getLvalueType(lv));
            }

            void runBlock(size_t bbIdx, size_t stmtIdx, State state) {
                const auto& bb = fcn.blocks.at(bbIdx);
                BUG_ASSERT(stmtIdx <= bb.statements.size());

                bool wasMoved = false;
                bool wasUpdated = false;
                auto visitCb = [&](const auto& lv, auto vu) {
                    if (lv.root == this->lv.root) {
                        switch (vu) {
                            case MIRValUsage::Read:
                                DEBUG(mirRes << StringView("Used"));
                                state.markRead(stmtIdx);
                                wasUpdated = true;
                                break;
                            case MIRValUsage::Move:
                                if (lv.wrappers.size() == this->lv.wrappers.size()) {
                                    DEBUG(mirRes << (isCopy ? "Read" : "Moved"));
                                    state.markRead(stmtIdx);
                                    wasMoved = !isCopy;
                                } else {
                                    DEBUG(mirRes << StringView("Used (partial)"));
                                    state.markRead(stmtIdx);
                                    wasUpdated = true;
                                }
                                break;
                            case MIRValUsage::Borrow:
                                if (std::any_of(lv.wrappers.begin(), lv.wrappers.end(), [](const auto& w) { return w.is_Deref(); })) {
                                    DEBUG(mirRes << StringView("Read (borrow through a deref)"));
                                    state.markRead(stmtIdx);
                                } else {
                                    DEBUG(mirRes << StringView("Borrowed"));
                                    state.markBorrowed(stmtIdx);
                                }
                                wasUpdated = true;
                                break;
                            case MIRValUsage::Write:
                                break;
                        }
                    }
                    for (const auto& w : lv.wrappers) {
                        if (w.is_Index() && this->lv.is_Local() && w.as_Index() == this->lv.as_Local()) {
                            DEBUG(mirRes << StringView("Index used"));
                            state.markRead(stmtIdx);
                            wasUpdated = true;
                        }
                    }
                    return false;
                };

                for (; stmtIdx < bb.statements.size(); stmtIdx++) {
                    const auto& stmt = bb.statements[stmtIdx];
                    mirRes.setCurStmt(bbIdx, stmtIdx);
                    visitedStatements.mut(blockOffsets[bbIdx] + stmtIdx) = true;

                    wasUpdated = false;
                    visitMirLvalues(stmt, visitCb);
                    if (wasUpdated || wasMoved) {
                        DEBUG(mirRes << stmt);
                    }

                    if (wasMoved) {
                        DEBUG(mirRes << StringView("Moved, return"));
                        state.markRead(stmtIdx);
                        state.finalise(stmtIdx);
                        return;
                    }

                    switch (stmt.tag()) {
                        case MIRStatement::TAG_Assign: {
                            auto& se = stmt.as_Assign();
                            if (se.dst == lv) {
                                DEBUG(mirRes << StringView("- Assigned to, return"));
                                state.finalise(stmtIdx);
                                return;
                            }
                            break;
                        }
                        case MIRStatement::TAG_Asm: {
                            auto& se = stmt.as_Asm();
                            for (const auto& e : se.outputs) {
                                if (e.second == lv) {
                                    state.finalise(stmtIdx);
                                    return;
                                }
                            }
                            break;
                        }
                        case MIRStatement::TAG_Asm2: {
                            auto& se = stmt.as_Asm2();
                            for (const auto& p : se.params) {
                                switch (p.tag()) {
                                    case MIRAsmParam::TAG_Const: {
                                        break;
                                    }
                                    case MIRAsmParam::TAG_Sym: {
                                        break;
                                    }
                                    case MIRAsmParam::TAG_Reg: {
                                        auto& v = p.as_Reg();
                                        if (v.output) {
                                            if (*v.output == lv) {
                                                state.finalise(stmtIdx);
                                                return;
                                            }
                                        }
                                        break;
                                    }
                                    case MIRAsmParam::TAG_Label: {
                                        UNREACHABLE();
                                    }
                                }
                            }
                            break;
                        }
                        case MIRStatement::TAG_SetDropFlag: {
                            break;
                        }
                        case MIRStatement::TAG_SaveDropFlag: {
                            break;
                        }
                        case MIRStatement::TAG_LoadDropFlag: {
                            break;
                        }
                    }
                }
                mirRes.setCurStmtTerm(bbIdx);
                visitedStatements.mut(blockOffsets[bbIdx] + stmtIdx) = true;

                wasUpdated = false;
                visitMirLvalues(bb.terminator, visitCb);

                DEBUG(mirRes << bb.terminator << (wasUpdated ? " (used)" : ""));
                if (wasMoved) {
                    state.markRead(stmtIdx);
                    state.finalise(stmtIdx);
                    return;
                }

                switch (bb.terminator.tag()) {
                    case MIRTerminator::TAG_Incomplete: {
                        // TODO: Isn't this a bug?
                        DEBUG(mirRes << StringView("Incomplete"));
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_Return: {
                        DEBUG(mirRes << StringView("Return"));
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_UnwindResume: {
                        DEBUG(mirRes << StringView("UnwindResume"));
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_UnwindTerminate: {
                        DEBUG(mirRes << StringView("UnwindTerminate"));
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_Unreachable: {
                        DEBUG(mirRes << StringView("Unreachable"));
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_Goto: {
                        auto& te = bb.terminator.as_Goto();
                        statesToDo.push_back(std::make_pair(te, mv$(state)));
                        break;
                    }
                    case MIRTerminator::TAG_If: {
                        auto& te = bb.terminator.as_If();
                        statesToDo.push_back(std::make_pair(te.bbTrue, state.clone()));
                        statesToDo.push_back(std::make_pair(te.bbFalse, mv$(state)));
                        break;
                    }
                    case MIRTerminator::TAG_Switch: {
                        auto& te = bb.terminator.as_Switch();
                        for (size_t i = 0; i < te.targets.length(); i++) {
                            statesToDo.push_back(std::make_pair(te.targets[i], state.clone()));
                        }
                        if (te.validFlag != ~0u) {
                            statesToDo.push_back(std::make_pair(te.invalidTarget, mv$(state)));
                        }
                        break;
                    }
                    case MIRTerminator::TAG_SwitchValue: {
                        auto& te = bb.terminator.as_SwitchValue();
                        for (size_t i = 0; i < te.targets.length(); i++) {
                            statesToDo.push_back(std::make_pair(te.targets[i], state.clone()));
                        }
                        statesToDo.push_back(std::make_pair(te.defTarget, mv$(state)));
                        break;
                    }
                    case MIRTerminator::TAG_Drop: {
                        auto& te = bb.terminator.as_Drop();
                        if (te.slot == lv) {
                            DEBUG(mirRes << StringView("Dropped, return"));
                            state.markRead(stmtIdx);
                            state.finalise(stmtIdx);
                            return;
                        }
                        if (te.unwind.is_Cleanup()) {
                            auto& target = te.unwind.as_Cleanup();
                            statesToDo.push_back(std::make_pair(target, state.clone()));
                        }
                        statesToDo.push_back(std::make_pair(te.target, mv$(state)));
                        break;
                    }
                    case MIRTerminator::TAG_Call: {
                        auto& te = bb.terminator.as_Call();
                        if (te.retVal == lv) {
                            DEBUG(mirRes << StringView("Assigned (Call), return"));
                            state.finalise(stmtIdx);
                            return;
                        }
                        if (te.unwind.is_Cleanup()) {
                            auto& target = te.unwind.as_Cleanup();
                            statesToDo.push_back(std::make_pair(target, state.clone()));
                        }
                        statesToDo.push_back(std::make_pair(te.retBlock, mv$(state)));
                        break;
                    }
                    case MIRTerminator::TAG_TailCall: {
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_Asm2: {
                        auto& te = bb.terminator.as_Asm2();
                        for (const auto& p : te.params) {
                            if (const auto* reg = p.opt_Reg()) {
                                if (reg->output && *reg->output == lv) {
                                    state.finalise(stmtIdx);
                                    return;
                                }
                            }
                        }
                        if (te.retBlock != ~0u) {
                            statesToDo.push_back(std::make_pair(te.retBlock, state.clone()));
                        }
                        for (const auto& p : te.params) {
                            if (const auto* target = p.opt_Label()) {
                                statesToDo.push_back(std::make_pair(*target, state.clone()));
                            }
                        }
                        break;
                    }
                }
            }
        };

        Runner runner(localMirRes, fcn, bbIdx, stmtIdx, lv, blockOffsets, vl);
        std::vector<std::pair<size_t, State>> postCheckList;

        // TODO: Have a bitmap of visited statements. If a visted statement is hit, stop the current state

        runner.runBlock(bbIdx, stmtIdx, State(blockOffsets, vl, bbIdx, stmtIdx));

        while (!runner.statesToDo.empty()) {
            auto bbIdx = runner.statesToDo.back().first;
            auto state = mv$(runner.statesToDo.back().second);
            runner.statesToDo.pop_back();

            DEBUG(StringView("state.bb_history=[") << state.bbHistory << StringView("], -> BB") << bbIdx);
            state.bbHistory.pushBack(bbIdx);

            if (runner.visitedStatements[blockOffsets[bbIdx]]) {
                if (vl.stmtBitmap[blockOffsets[bbIdx]]) {
                    DEBUG(StringView("Looped (to already valid)"));
                    state.markRead(0);
                    state.finalise(0);
                    continue;
                } else if (state.isBorrowed()) {
                    DEBUG(StringView("Looped (borrowed)"));
                    state.markRead(0);
                    state.finalise(0);
                    continue;
                } else {
                    DEBUG(StringView("Looped (after last read), push for later"));
                    postCheckList.push_back(std::make_pair(bbIdx, mv$(state)));
                    continue;
                }
            }

            if (vl.stmtBitmap[blockOffsets[bbIdx]]) {
                DEBUG(StringView("Already valid in BB") << bbIdx);
                state.markRead(0);
                state.finalise(0);
                continue;
            }

            runner.runBlock(bbIdx, 0, mv$(state));
        }

        while (!postCheckList.empty()) {
            bool change = false;
            for (auto it = postCheckList.begin(); it != postCheckList.end();) {
                auto bbIdx = it->first;
                auto& state = it->second;
                if (vl.stmtBitmap[blockOffsets[bbIdx]]) {
                    change = true;
                    DEBUG(StringView("Looped (now valid)"));
                    state.markRead(0);
                    state.finalise(0);

                    it = postCheckList.erase(it);
                } else {
                    ++it;
                }
            }
            if (!change) {
                break;
            }
        }
    }
}

void MIRTypeResolve::fmtPos(ZeroCopyOutput& os, bool includePath /*=false*/) const {
    if (includePath) {
        this->path_.write(os);
        os << StringView(" ");
    }
    os << StringView("BB") << this->bbIdx << StringView("/");
    if (this->stmtIdx == STMT_TERM) {
        os << StringView("TERM");
    } else {
        os << this->stmtIdx;
    }
    os << StringView(": ");
}

void MIRTypeResolve::printMsgCb(const char* tag, SpanMessageCallback& cb) const {
    auto os = sysE;
    os << StringView("MIR ") << tag << StringView(": ");
    fmtPos(os, true);
    cb.write(os);
    os << endL << flsH;
    abort();
}

unsigned int MIRTypeResolve::getCurStmtOfs() const {
    if (this->stmtIdx == STMT_TERM) {
        return fcn.blocks.at(this->bbIdx).statements.size();
    } else {
        return this->stmtIdx;
    }
}

const MIRBasicBlock& MIRTypeResolve::getBlock(MIRBasicBlockId id) const {
    MIR_ASSERT(*this, id < fcn.blocks.size(), StringView("Block ID ") << id << StringView(" out of range"));
    return fcn.blocks[id];
}

const HIRType* MIRTypeResolve::getStaticType(const HIRPath& path) const {
    if (path.data.is_UfcsInherent() && path.data.as_UfcsInherent().item == "#type_id") {
        return crate.types.unit();
    }
    MonomorphState ms(crate.types);
    auto v = resolve.getValue(this->sp, path, ms, /*signature_only*/ true);
    MIR_ASSERT(*this, v.is_Static(), StringView("LValue::Static not a static - ") << path << StringView(" : ") << v.tagStr());
    MIR_ASSERT(*this, v.as_Static(), StringView("LValue::Static is null? - ") << path << StringView(" : ") << v.tagStr());
    if (ms.hasTypes()) {
        return resolve.expandAssociatedTypes(this->sp, ms.monomorphType(sp, v.as_Static()->type));
    } else {
        return v.as_Static()->type;
    }
}

const HIRType* MIRTypeResolve::getLvalueType(const MIRLValue& val, unsigned wrapperSkipCount /*=0*/) const {
    const HIRType* rv = nullptr;
    switch (val.root.tag()) {
        case MIRLValue::Storage::TAG_Return: {
            rv = monomorphedRettype ? monomorphedRettype : retType;
            break;
        }
        case MIRLValue::Storage::TAG_Argument: {
            decltype(val.root.as_Argument()) e = val.root.as_Argument();
            MIR_ASSERT(*this, e < args.size(), StringView("Argument ") << val << StringView(" out of range (") << args.size() << StringView(")"));
            rv = args.at(e).second;
            break;
        }
        case MIRLValue::Storage::TAG_Local: {
            decltype(val.root.as_Local()) e = val.root.as_Local();
            MIR_ASSERT(*this, e < fcn.locals.length(), StringView("Local ") << val << StringView(" out of range (") << fcn.locals.length() << StringView(")"));
            rv = monomorphedLocals ? (*monomorphedLocals)[e] : fcn.locals[e];
            break;
        }
        case MIRLValue::Storage::TAG_Static: {
            decltype(val.root.as_Static()) e = val.root.as_Static();
            rv = getStaticType(e);
            break;
        }
    }
    if (val.wrappers.size() > 0) {
        BUG_ASSERT(wrapperSkipCount <= val.wrappers.size());
        const auto* stopWrapper = val.wrappers.data() + (val.wrappers.size() - wrapperSkipCount);
        for (const auto& w : val.wrappers) {
            if (&w == stopWrapper) {
                break;
            }
            rv = this->getUnwrappedType(w, rv);
        }
    } else {
        BUG_ASSERT(wrapperSkipCount == 0);
    }
    return rv;
}

const HIRType* MIRTypeResolve::getUnwrappedType(const MIRLValue::Wrapper& w, const HIRType* ty) const {
    switch (w.tag()) {
        case MIRLValue::Wrapper::TAG_Field: {
            decltype(w.as_Field()) fieldIndex = w.as_Field();
            switch ((*ty).tag()) {
                default:
                    MIR_BUG(*this, StringView("Field access on unexpected type - ") << ty);
                    break;
                case HIRType::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    return te.inner;
                }
                case HIRType::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    return te.inner;
                }
                case HIRType::TAG_Tuple: {
                    auto& te = (*ty).as_Tuple();
                    MIR_ASSERT(*this, fieldIndex < te.length(), StringView("Field index out of range in tuple ") << fieldIndex << StringView(" >= ") << te.length());
                    return te[fieldIndex];
                }
                case HIRType::TAG_Path: {
                    auto& te = (*ty).as_Path();
                    // TODO: Cache result (to avoid needing to re-monomorph)
                    if (const auto* tep = te.binding.opt_Struct()) {
                        const auto& str = *tep;
                        auto maybeMonomorph = [&](const auto& fieldType) {
                            return resolve.monomorphExpandOpt(sp, fieldType, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                        };
                        switch (str.data.tag()) {
                            case HIRStructData::TAG_Unit: {
                                MIR_BUG(*this, StringView("Field on unit-like struct - ") << ty);
                                break;
                            }
                            case HIRStructData::TAG_Tuple: {
                                auto& se = str.data.as_Tuple();
                                MIR_ASSERT(*this, fieldIndex < se.size(), StringView("Field index out of range in tuple-struct ") << te.path);
                                return maybeMonomorph(se[fieldIndex].ent);
                            }
                            case HIRStructData::TAG_Named: {
                                auto& se = str.data.as_Named();
                                MIR_ASSERT(*this, fieldIndex < se.size(), StringView("Field index out of range in struct ") << te.path);
                                return maybeMonomorph(se[fieldIndex].ty);
                            }
                        }
                    } else if (const auto* tep = te.binding.opt_Union()) {
                        const auto& unm = *tep;
                        auto maybeMonomorph = [&](const HIRType* t) -> const HIRType* {
                            return resolve.monomorphExpandOpt(sp, t, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                        };
                        MIR_ASSERT(*this, fieldIndex < unm.variants.size(), StringView("Field index out of range for union"));
                        return maybeMonomorph(unm.variants.at(fieldIndex).ty);
                    } else {
                        MIR_BUG(*this, StringView("Field access on invalid type - ") << ty);
                    }
                    break;
                }
            }
            break;
        }
        case MIRLValue::Wrapper::TAG_Deref: {
            switch ((*ty).tag()) {
                default:
                    MIR_BUG(*this, StringView("Deref on unexpected type - ") << ty);
                    break;
                case HIRType::TAG_Path: {
                    if (const auto* innerPtr = this->isTypeOwnedBox(ty)) {
                        return innerPtr;
                    } else {
                        MIR_BUG(*this, StringView("Deref on unexpected type - ") << ty);
                    }
                    break;
                }
                case HIRType::TAG_Pointer: {
                    auto& te = (*ty).as_Pointer();
                    return te.inner;
                }
                case HIRType::TAG_Borrow: {
                    auto& te = (*ty).as_Borrow();
                    return te.inner;
                }
            }
            break;
        }
        case MIRLValue::Wrapper::TAG_Index: {
            switch ((*ty).tag()) {
                default:
                    MIR_BUG(*this, StringView("Index on unexpected type - ") << ty);
                    break;
                case HIRType::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    return te.inner;
                }
                case HIRType::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    return te.inner;
                }
            }
            break;
        }
        case MIRLValue::Wrapper::TAG_Downcast: {
            decltype(w.as_Downcast()) variantIndex = w.as_Downcast();
            switch ((*ty).tag()) {
                default:
                    MIR_BUG(*this, StringView("Downcast on unexpected type - ") << ty);
                    break;
                case HIRType::TAG_Path: {
                    auto& te = (*ty).as_Path();
                    MIR_ASSERT(*this, te.binding.is_Enum() || te.binding.is_Union(), StringView("Downcast on non-Enum"));
                    if (te.binding.is_Enum()) {
                        const auto& enm = *te.binding.as_Enum();
                        MIR_ASSERT(*this, enm.data.is_Data(), StringView("Downcast on non-data enum - ") << ty);
                        const auto& variants = enm.data.as_Data();
                        MIR_ASSERT(*this, variantIndex < variants.size(), StringView("Variant index out of range for ") << ty);
                        const auto& variant = variants[variantIndex];

                        const auto& varTy = variant.type;
                        return resolve.monomorphExpandOpt(sp, varTy, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                    } else {
                        const auto& unm = *te.binding.as_Union();
                        MIR_ASSERT(*this, variantIndex < unm.variants.size(), StringView("Variant index out of range"));
                        const auto& variant = unm.variants[variantIndex];
                        const auto& varTy = variant.ty;

                        return resolve.monomorphExpandOpt(sp, varTy, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                    }
                    break;
                }
            }
            break;
        }
    }
    UNREACHABLE();
}

const HIRType* MIRTypeResolve::getParamType(const MIRParam& val) const {
    switch (val.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = val.as_LValue();
            return getLvalueType(e);
        }
        case MIRParam::TAG_Constant: {
            auto& e = val.as_Constant();
            return getConstType(e);
        }
        case MIRParam::TAG_Borrow: {
            auto& e = val.as_Borrow();
            return crate.types.borrow(e.type, getLvalueType(e.val));
        }
    }
    UNREACHABLE();
}

const HIRType* MIRTypeResolve::getConstType(const MIRConstant& c) const {
    const auto* type = getConstTypeInBody(c);
    if (monomorphedParams && monomorphiseTypeNeeded(type)) {
        return resolve.monomorphExpand(this->sp, type, *monomorphedParams);
    }
    return type;
}

const HIRType* MIRTypeResolve::getConstTypeInBody(const MIRConstant& c) const {
    switch (c.tag()) {
        case MIRConstant::TAG_Int: {
            auto& e = c.as_Int();
            return crate.types.primitive(e.t);
        }
        case MIRConstant::TAG_Uint: {
            auto& e = c.as_Uint();
            return crate.types.primitive(e.t);
        }
        case MIRConstant::TAG_Float: {
            auto& e = c.as_Float();
            return crate.types.primitive(e.t);
        }
        case MIRConstant::TAG_Bool: {
            return crate.types.primitive(HIRCoreType::Bool);
        }
        case MIRConstant::TAG_Bytes: {
            auto& e = c.as_Bytes();
            return crate.types.borrow(HIRBorrowType::Shared, crate.types.array(crate.types.primitive(HIRCoreType::U8), e.length()));
        }
        case MIRConstant::TAG_StaticString: {
            return crate.types.borrow(HIRBorrowType::Shared, crate.types.primitive(HIRCoreType::Str));
        }
        case MIRConstant::TAG_Encoded: {
            auto& e = c.as_Encoded();
            return e.type;
        }
        case MIRConstant::TAG_Const: {
            auto& e = c.as_Const();
            MonomorphState p(crate.types);
            auto v = resolve.getValue(this->sp, *e.p, p, /*signature_only=*/true);
            if (const auto* ve = v.opt_Constant()) {
                const auto& ty = (*ve)->type;
                if (monomorphiseTypeNeeded(ty)) {
                    auto rv = p.monomorphType(this->sp, ty);
                    rv = resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                } else {
                    return ty;
                }
            } else {
                MIR_BUG(*this, StringView("get_const_type - Not a constant ") << *e.p);
            }
            break;
        }
        case MIRConstant::TAG_Generic: {
            auto& e = c.as_Generic();
            return resolve.getConstParamType(this->sp, e.binding);
        }
        case MIRConstant::TAG_Function: {
            auto& e = c.as_Function();
            MonomorphState p(crate.types);
            auto v = resolve.getValue(this->sp, *e.p, p, /*signature_only=*/true);
            switch (v.tag()) {
                default:
                    MIR_BUG(*this, StringView("get_const_type - Function points to bad type: ") << v.tagStr() << StringView(" - ") << c);
                    break;
                case TypeckValuePtr::TAG_NotFound: {
                    MIR_BUG(*this, StringView("get_const_type - ItemAddr points to unknown value - ") << c);
                    break;
                }
                case TypeckValuePtr::TAG_Function: {
                    auto& ve = v.as_Function();
                    return crate.types.intern(HIRType::make_NamedFunction({e.p->clone(), ve}));
                }
                case TypeckValuePtr::TAG_EnumConstructor: {
                    auto& ve = v.as_EnumConstructor();
                    return crate.types.intern(HIRType::make_NamedFunction({e.p->clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}));
                }
                case TypeckValuePtr::TAG_StructConstructor: {
                    auto& ve = v.as_StructConstructor();
                    return crate.types.intern(HIRType::make_NamedFunction({e.p->clone(), ve.s}));
                }
            }
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& e = c.as_ItemAddr();
            MonomorphState p(crate.types);
            ASSERT_BUG(sp, e, StringView("get_const_type - ") << c);
            auto v = resolve.getValue(this->sp, *e, p, /*signature_only=*/true);
            switch (v.tag()) {
                case TypeckValuePtr::TAG_Constant:
                case TypeckValuePtr::TAG_NotFound: {
                    MIR_BUG(*this, StringView("get_const_type - ItemAddr does not name an addressable item - ") << c);
                    break;
                }
                case TypeckValuePtr::TAG_NotYetKnown: {
                    if (e->data.is_UfcsKnown()) {
                        const auto& pe = e->data.as_UfcsKnown();
                        if (pe.item == "vtable#" && pe.trait.path == HIRSimplePath()) {
                            Vector<const HIRType*> fields;
                            fields.pushBack(crate.types.primitive(HIRCoreType::Usize));
                            fields.pushBack(crate.types.primitive(HIRCoreType::Usize));
                            fields.pushBack(crate.types.primitive(HIRCoreType::Usize));
                            return crate.types.borrow(HIRBorrowType::Shared, crate.types.tuple(mv$(fields)));
                        }
                    }
                    MIR_BUG(*this, StringView("get_const_type - get_value returned NotYetKnown with signature_only=true"));
                    break;
                }
                case TypeckValuePtr::TAG_Static: {
                    auto& ve = v.as_Static();
                    const auto& ty = ve->type;
                    const HIRType* rv;
                    if (monomorphiseTypeNeeded(ty)) {
                        rv = p.monomorphType(this->sp, ty);
                        rv = resolve.expandAssociatedTypes(this->sp, rv);
                    } else {
                        rv = ty;
                    }
                    return crate.types.borrow(HIRBorrowType::Shared, rv);
                }
                case TypeckValuePtr::TAG_Function: {
                    auto& ve = v.as_Function();
                    auto rv = crate.types.function((HIRType::Data_NamedFunction{e->clone(), ve}).decay(crate.types, this->sp));
                    rv = resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
                case TypeckValuePtr::TAG_EnumValue: {
                    MIR_BUG(*this, StringView("get_const_type - ItemAddr points to an enum value - ") << c);
                    break;
                }
                case TypeckValuePtr::TAG_EnumConstructor: {
                    auto& ve = v.as_EnumConstructor();
                    auto rv = crate.types.function((HIRType::Data_NamedFunction{e->clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}).decay(crate.types, this->sp));
                    rv = resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
                case TypeckValuePtr::TAG_StructConstant: {
                    MIR_BUG(*this, c << StringView(" pointing to a struct constant"));
                    break;
                }
                case TypeckValuePtr::TAG_StructConstructor: {
                    auto& ve = v.as_StructConstructor();
                    auto rv = crate.types.function((HIRType::Data_NamedFunction{e->clone(), ve.s}).decay(crate.types, this->sp));
                    rv = resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
            }
            break;
        }
    }
    UNREACHABLE();
}

bool MIRTypeResolve::lvalueIsCopy(const MIRLValue& val) const {
    const HIRType* tmp;
    return resolve.typeIsCopy(this->sp, getLvalueType(val));
}

const HIRType* MIRTypeResolve::isTypeOwnedBox(const HIRType* ty) const {
    return resolve.isTypeOwnedBox(ty);
}

size_t MIRTypeResolve::intrinsicOffsetOf(const HIRType* ty, const std::vector<MIRParam>& values) const {
    const auto* curTy = ty;
    size_t baseOfs = 0;
    for (size_t i = 0; i < values.size(); i++) {
        MIR_ASSERT(*this, values[i].is_Constant(), StringView("Arguments to `offset_of` must be constants"));
        size_t idx = 0;
        {
            auto& tuMatch = values[i].as_Constant();
            switch (tuMatch.tag()) {
                default:
                    MIR_TODO(*this, StringView("offset_of: field ") << values[i]);
                    break;
                case MIRConstant::TAG_Int: {
                    auto& fieldIdx = tuMatch.as_Int();
                    MIR_ASSERT(*this, fieldIdx.v.isI64() && fieldIdx.v >= S128(0), StringView("Invalid tuple field index ") << fieldIdx.v);
                    idx = static_cast<size_t>(fieldIdx.v.truncateI64());
                    break;
                }
                case MIRConstant::TAG_Uint: {
                    auto& fieldIdx = tuMatch.as_Uint();
                    MIR_ASSERT(*this, fieldIdx.v.isU64() && fieldIdx.v <= U128(SIZE_MAX), StringView("Invalid tuple field index ") << fieldIdx.v);
                    idx = static_cast<size_t>(fieldIdx.v.truncateU64());
                    break;
                }
                case MIRConstant::TAG_StaticString: {
                    auto& fieldName = tuMatch.as_StaticString();
                    char* end = nullptr;
                    auto numericIdx = std::strtoul(fieldName.c_str(), &end, 10);
                    if (end != fieldName.c_str() && *end == '\0') {
                        MIR_ASSERT(*this, numericIdx <= SIZE_MAX, StringView("Invalid tuple field index ") << fieldName);
                        idx = static_cast<size_t>(numericIdx);
                    } else if (const auto* tyPath = curTy->opt_Path()) {
                        if (const auto* bep = tyPath->binding.opt_Struct()) {
                            const auto& str = *bep;
                            switch (str.data.tag()) {
                                case HIRStructData::TAG_Named: {
                                    auto& fields = str.data.as_Named();
                                    idx = std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                                        return x.name == fieldName;
                                    }) - fields.begin();
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    MIR_BUG(*this, StringView("Named field on tuple struct: ") << curTy << StringView(" .") << fieldName);
                                    break;
                                }
                                case HIRStructData::TAG_Unit: {
                                    auto& _ = str.data.as_Unit();
                                    MIR_BUG(*this, StringView("Empty struct: ") << curTy << StringView(" .") << fieldName);
                                    break;
                                }
                            }
                        } else if (const auto* bep = tyPath->binding.opt_Union()) {
                            const auto& unm = *bep;
                            const auto& fields = unm.variants;
                            idx = std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                                return x.name == fieldName;
                            }) - fields.begin();
                        } else if (const auto* bep = tyPath->binding.opt_Enum()) {
                            const auto& enm = *bep;
                            MIR_ASSERT(*this, enm.data.is_Data(), StringView("Non-Data enum: ") << curTy << StringView(" .") << fieldName);
                            const auto& fields = enm.data.as_Data();
                            idx = std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                                return x.name == fieldName;
                            }) - fields.begin();
                        } else {
                            MIR_TODO(*this, StringView("offset_of: named field/variant - ") << fieldName);
                        }
                    } else {
                        MIR_TODO(*this, StringView("offset_of: named field/variant - ") << fieldName);
                    }
                    break;
                }
            }
        }
        auto* repr = TargetGetTypeRepr(this->sp, resolve, curTy);
        if (!repr) {
            MIR_BUG(*this, StringView("Calling `offset_of!` on type with non-defined repr: ") << curTy);
        }
        MIR_ASSERT(*this, idx < repr->fields.size(), StringView("Field index ") << idx << StringView(" out of range for ") << curTy);
        curTy = repr->fields[idx].ty;
        baseOfs += repr->fields[idx].offset;
    }
    return baseOfs;
}

MIRTypeResolve::TypeNameString MIRTypeResolve::typeNameForSimplePath(const HIRSimplePath& path) const {
    const HIRCrate* pathCrate = nullptr;
    if (path.crateName() == crate.crateName) {
        pathCrate = &crate;
    } else {
        auto crateIt = crate.extCrates.find(path.crateName());
        if (crateIt != crate.extCrates.end()) {
            pathCrate = crateIt->second.data;
        }
    }

    const HIRLocalItemTypeNamePath* localOwner = nullptr;
    size_t localModuleSize = 0;
    if (pathCrate) {
        const auto components = path.components();
        for (const auto* candidate = pathCrate->localItemTypeNamePaths; candidate; candidate = candidate->next) {
            const auto moduleComponents = candidate->modulePath.components();
            if (candidate->modulePath.crateName() != path.crateName() || moduleComponents.size() <= localModuleSize || moduleComponents.size() > components.size()) {
                continue;
            }
            size_t i = 0;
            while (i < moduleComponents.size() && moduleComponents[i] == components[i]) {
                i++;
            }
            if (i == moduleComponents.size()) {
                localOwner = candidate;
                localModuleSize = i;
            }
        }
    }

    std::string rv;
    if (localOwner) {
        rv = typeNameForItemPath(*localOwner->ownerPath, true);
    } else {
        const auto& crateName = path.crateName();
        if (crateName != "") {
            if (crateName == crate.crateName && crate.crateNameDisplay != "") {
                rv += crate.crateNameDisplay.c_str();
            } else {
                std::string name(crateName.c_str());
                const auto tag = name.rfind('-');
                rv += (tag == std::string::npos ? name : name.substr(0, tag));
            }
        }
    }

    const auto components = path.components();
    for (size_t i = localModuleSize; i < components.size(); i++) {
        if (!rv.empty()) {
            rv += "::";
        }
        auto text = FMT(components[i]);
        if (text.compare(0, strlen(CLOSURE_PATH_PREFIX), CLOSURE_PATH_PREFIX) == 0) {
            auto owner = text.substr(strlen(CLOSURE_PATH_PREFIX));
            const auto index = owner.rfind('_');
            if (index < owner.size() && owner.find_first_not_of("0123456789", index + 1) >= owner.size()) {
                owner = owner.substr(0, index);
            }
            rv += owner;
            rv += "::{{closure}}";
            continue;
        }
        rv += text;
    }
    return rv;
}

MIRTypeResolve::TypeNameString MIRTypeResolve::typeNameForPathArgs(const HIRPathParams& params, const HIRTraitPath::assocListT* typeBounds, bool genericPlaceholders) const {
    auto rv = FMT(StringView(""));
    auto add = [&rv](const auto& text) {
        rv += (rv.empty() ? "<" : ", ");
        rv += text;
    };
    for (const auto& t : params.types) {
        add(intrinsicTypeNameImpl(t, genericPlaceholders));
    }
    for (const auto& v : params.values) {
        if (genericPlaceholders && v.is_Generic()) {
            add("_");
        } else if (const auto* e = v.opt_Evaluated()) {
            add(FMT(EncodedLiteralSlice(**e).readUint()));
        } else {
            add(FMT(v));
        }
    }
    if (typeBounds) {
        for (const auto& b : *typeBounds) {
            add(FMT(b.first << StringView(" = ") << intrinsicTypeNameImpl(b.second.type, genericPlaceholders)));
        }
    }
    if (!rv.empty()) {
        rv += ">";
    }
    return rv;
}

HIRPathParams MIRTypeResolve::typeArgsWithoutDefaults(const HIRTypeDataPath& path) const {
    const auto& params = path.path.data.as_Generic().params;
    const HIRGenericParams* definition = nullptr;
    if (const auto* structure = path.binding.opt_Struct()) {
        definition = &structure->params;
    } else if (const auto* enumeration = path.binding.opt_Enum()) {
        definition = &enumeration->params;
    } else if (const auto* unionItem = path.binding.opt_Union()) {
        definition = &unionItem->params;
    }
    if (!definition || !params.values.empty() || params.types.size() != definition->types.size()) {
        return params;
    }
    const auto monomorph = MonomorphStatePtr(crate.types, nullptr, &params, nullptr);
    size_t count = params.types.size();
    while (count > 0) {
        const auto* defaultType = definition->types[count - 1].defaultValue;
        if (!defaultType || defaultType->is_Infer()) {
            break;
        }
        const auto* instantiated = resolve.expandAssociatedTypes(Span(), monomorph.monomorphType(Span(), defaultType));
        if (instantiated != params.types[count - 1]) {
            break;
        }
        count--;
    }
    return count == params.types.size() ? params : HIRPathParams::fromTypes(params.types.data(), count);
}

MIRTypeResolve::TypeNameString MIRTypeResolve::typeNameForItemPath(const HIRPath& path, bool genericPlaceholders) const {
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            const auto& pe = path.data.as_Generic();
            return typeNameForSimplePath(pe.path) + typeNameForPathArgs(pe.params, nullptr, genericPlaceholders);
        }
        case HIRPathData::TAG_UfcsInherent: {
            const auto& pe = path.data.as_UfcsInherent();
            return FMT(intrinsicTypeNameImpl(pe.type, genericPlaceholders) << StringView("::") << pe.item << typeNameForPathArgs(pe.params, nullptr, genericPlaceholders));
        }
        case HIRPathData::TAG_UfcsKnown: {
            const auto& pe = path.data.as_UfcsKnown();
            auto trait = typeNameForSimplePath(pe.trait.path) + typeNameForPathArgs(pe.trait.params, nullptr, genericPlaceholders);
            return FMT(StringView("<") << intrinsicTypeNameImpl(pe.type, genericPlaceholders) << StringView(" as ") << trait << StringView(">::") << pe.item << typeNameForPathArgs(pe.params, nullptr, genericPlaceholders));
        }
        case HIRPathData::TAG_UfcsUnknown:
            break;
    }
    return FMT(path);
}

MIRTypeResolve::TypeNameString MIRTypeResolve::intrinsicTypeName(const HIRType* ty) const {
    return intrinsicTypeNameImpl(ty, false);
}

MIRTypeResolve::TypeNameString MIRTypeResolve::intrinsicTypeNameImpl(const HIRType* ty, bool genericPlaceholders) const {
    if (genericPlaceholders && ty->is_Generic()) {
        return "_";
    }

    if (const auto* te = ty->opt_Tuple()) {
        auto rv = FMT(StringView("("));
        for (size_t i = 0; i < te->length(); i++) {
            if (i > 0) {
                rv += ", ";
            }
            rv += intrinsicTypeNameImpl((*te)[i], genericPlaceholders);
        }
        return rv + ")";
    }
    if (const auto* te = ty->opt_Slice()) {
        return "[" + intrinsicTypeNameImpl(te->inner, genericPlaceholders) + "]";
    }
    if (const auto* te = ty->opt_Array()) {
        auto size = te->size.is_Known() ? FMT(te->size.as_Known()) : (genericPlaceholders ? "_" : FMT(te->size.as_Unevaluated()));
        return FMT(StringView("[") << intrinsicTypeNameImpl(te->inner, genericPlaceholders) << StringView("; ") << size << StringView("]"));
    }
    if (const auto* te = ty->opt_Borrow()) {
        const char* prefix = te->type == HIRBorrowType::Shared ? "&" : (te->type == HIRBorrowType::Unique ? "&mut " : "&move ");
        return prefix + intrinsicTypeNameImpl(te->inner, genericPlaceholders);
    }
    if (const auto* te = ty->opt_Pointer()) {
        const char* prefix = te->type == HIRBorrowType::Shared ? "*const " : (te->type == HIRBorrowType::Unique ? "*mut " : "*move ");
        return prefix + intrinsicTypeNameImpl(te->inner, genericPlaceholders);
    }
    if (const auto* te = ty->opt_Function()) {
        auto rv = FMT((te->isUnsafe ? "unsafe " : ""));
        if (te->abi != "" && te->abi != "Rust") {
            rv += FMT(StringView("extern \"") << te->abi << StringView("\" "));
        }
        rv += "fn(";
        for (size_t i = 0; i < te->argTypes.length(); i++) {
            if (i > 0) {
                rv += ", ";
            }
            rv += intrinsicTypeNameImpl(te->argTypes[i], genericPlaceholders);
        }
        rv += ")";
        if (!(te->rettype->is_Tuple() && te->rettype->as_Tuple().empty())) {
            rv += " -> ";
            rv += intrinsicTypeNameImpl(te->rettype, genericPlaceholders);
        }
        return rv;
    }
    if (const auto* te = ty->opt_NamedFunction()) {
        const char* suffix = te->def.is_Function() ? "" : "::{{constructor}}";
        return typeNameForItemPath(te->path, genericPlaceholders) + suffix;
    }
    if (ty->is_Path() && ty->as_Path().path.data.is_Generic()) {
        const auto& gp = ty->as_Path().path.data.as_Generic();
        return typeNameForSimplePath(gp.path) + typeNameForPathArgs(typeArgsWithoutDefaults(ty->as_Path()), nullptr, genericPlaceholders);
    }
    if (const auto* te = ty->opt_TraitObject()) {
        std::vector<std::string> bounds;
        if (te->trait.path.path.crateName() != "" || !te->trait.path.path.components().empty()) {
            auto principal = typeNameForSimplePath(te->trait.path.path);
            const auto& comps = te->trait.path.path.components();
            const auto& last = comps.empty() ? RcString() : comps.back();
            const auto& params = te->trait.path.params;
            if ((last == "Fn" || last == "FnMut" || last == "FnOnce") && params.types.size() == 1 && params.types[0]->is_Tuple()) {
                principal += "(";
                const auto& args = params.types[0]->as_Tuple();
                for (size_t i = 0; i < args.length(); i++) {
                    if (i > 0) {
                        principal += ", ";
                    }
                    principal += intrinsicTypeNameImpl(args[i], genericPlaceholders);
                }
                principal += ")";
                auto it = te->trait.typeBounds.find(RcString::newInterned("Output"));
                if (it != te->trait.typeBounds.end() && !(it->second.type->is_Tuple() && it->second.type->as_Tuple().empty())) {
                    principal += " -> ";
                    principal += intrinsicTypeNameImpl(it->second.type, genericPlaceholders);
                }
            } else {
                principal += typeNameForPathArgs(params, &te->trait.typeBounds, genericPlaceholders);
            }
            bounds.push_back(std::move(principal));
        }
        for (const auto& marker : te->markers) {
            bounds.push_back(typeNameForSimplePath(marker.path));
        }
        auto rv = FMT(StringView("dyn "));
        for (size_t i = 0; i < bounds.size(); i++) {
            if (i > 0) {
                rv += " + ";
            }
            rv += bounds[i];
        }
        return rv;
    }
    return FMT(ty);
}

bool visitMirLvalueWith(const MIRLValue& lv, MIRValUsage u, const MIRLvalueCallback& cb) {
    LValueCbVisitor v{cb};
    return v.visitLvalue(lv, u);
}

bool visitMirLvalueWith(const MIRParam& p, MIRValUsage u, const MIRLvalueCallback& cb) {
    LValueCbVisitor v{cb};
    return v.visitParam(p, u);
}

bool visitMirLvaluesWith(const MIRRValue& rval, const MIRLvalueCallback& cb) {
    LValueCbVisitor v{cb};
    return v.visitRvalue(rval);
}

bool visitMirLvaluesWith(const MIRStatement& stmt, const MIRLvalueCallback& cb) {
    LValueCbVisitor v{cb};
    return v.visitStmt(stmt);
}

bool visitMirLvaluesWith(const MIRTerminator& term, const MIRLvalueCallback& cb) {
    LValueCbVisitor v{cb};
    return v.visitTerminator(term);
}

void visitTerminatorTargetMut(MIRTerminator& term, MIRTargetVisitorMut& cb) {
    struct TermCbVisitorMut: public MIRVisitorMut {
        MIRTargetVisitorMut& cb;

        explicit TermCbVisitorMut(MIRTargetVisitorMut& cb)
            : cb(cb)
        {
        }

        bool visitBlockId(MIRBasicBlockId& x) override {
            cb.visitTarget(x);
            return false;
        }
    } v{cb};

    v.visitTerminator(term);
}

void visitTerminatorTarget(const MIRTerminator& term, MIRTargetVisitor& cb) {
    struct TermCbVisitor final: public MIRVisitor {
        MIRTargetVisitor& cb;

        explicit TermCbVisitor(MIRTargetVisitor& cb)
            : cb(cb)
        {
        }

        bool visitLvalue(const MIRLValue&, MIRValUsage) override {
            return false;
        }

        bool visitBlockId(const MIRBasicBlockId& target) override {
            cb.visitTarget(target);
            return false;
        }
    } visitor{cb};
    visitor.visitTerminator(term);
}

// TODO: Improved algorithm

MIRValueLifetimes MIRHelperGetLifetimes(MIRTypeResolve& state, const MIRFunction& fcn) {
    TRACE_FUNCTION_F(state);
    size_t statementCount = 0;
    Vector<size_t> blockOffsets;
    blockOffsets.grow(fcn.blocks.size());
    for (const auto& bb : fcn.blocks) {
        blockOffsets.pushBack(statementCount);
        statementCount += bb.statements.size() + 1;
    }
    blockOffsets.pushBack(statementCount);

    std::vector<ValueLifetime> slotLifetimes(fcn.locals.length(), ValueLifetime(statementCount));

    std::vector<Vector<bool>> slotReadBitmaps(fcn.locals.length());
    {
        for (auto& b : slotReadBitmaps) {
            b.zero(statementCount);
        }
        size_t pos = 0;
        auto useCb = [&](const MIRLValue& tlv, MIRValUsage vu) {
            if (tlv.root.is_Local()) {
                if (vu != MIRValUsage::Write) {
                    slotReadBitmaps[tlv.root.as_Local()].mut(pos) = true;
                }
            }
            for (const auto& w : tlv.wrappers) {
                if (w.is_Index()) {
                    slotReadBitmaps[w.as_Index()].mut(pos) = true;
                }
            }
            return false;
        };
        for (const auto& bb : fcn.blocks) {
            for (const auto& stmt : bb.statements) {
                visitMirLvalues(stmt, useCb);
                pos++;
            }
            visitMirLvalues(bb.terminator, useCb);
            pos++;
        }
    }

    for (size_t bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        auto assignedLvalue = [&](size_t bbIdx, size_t stmtIdx, const MIRLValue& lv) {
            if (lv.is_Local()) {
                auto de = lv.root.as_Local();
                MIRHelperGetLifetimesDetermineValueLifetime(state, fcn, bbIdx, stmtIdx, lv, blockOffsets, slotReadBitmaps[de], slotLifetimes[de]);
                slotLifetimes[de].fill(blockOffsets, bbIdx, stmtIdx, stmtIdx);
            }
        };

        const auto& bb = fcn.blocks[bbIdx];
        for (size_t stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
            state.setCurStmt(bbIdx, stmtIdx);
            const auto& stmt = bb.statements[stmtIdx];
            visitMirLvalues(stmt, [&](const MIRLValue& lvalue, MIRValUsage usage) {
                if (usage == MIRValUsage::Write) {
                    assignedLvalue(bbIdx, stmtIdx + 1, lvalue);
                }
                return false;
            });
        }
        state.setCurStmtTerm(bbIdx);

        if (bb.terminator.is_Call()) {
            auto& te = bb.terminator.as_Call();
            assignedLvalue(te.retBlock, 0, te.retVal);
        } else if (const auto* te = bb.terminator.opt_Asm2()) {
            for (const auto& parameter : te->params) {
                const auto* reg = parameter.opt_Reg();
                if (!reg || !reg->output) {
                    continue;
                }
                if (te->retBlock != ~0u) {
                    assignedLvalue(te->retBlock, 0, *reg->output);
                }
                for (const auto& target : te->params) {
                    if (const auto* label = target.opt_Label()) {
                        assignedLvalue(*label, 0, *reg->output);
                    }
                }
            }
        }
    }

    MIRValueLifetimes rv;
    rv.blockOffsets = mv$(blockOffsets);
    rv.slots.reserve(slotLifetimes.size());
    for (auto& lft : slotLifetimes) {
        rv.slots.push_back(MIRValueLifetime(mv$(lft.stmtBitmap)));
    }
    return rv;
}

MIRTypeResolve::MIRTypeResolve(const Span& sp, const ::StaticTraitResolve& resolve, const MIRPathCallback& path, const HIRType* retType, const argsT& args, const MIRFunction& fcn)
    : sp(sp)
    , resolve(resolve)
    , crate(resolve.hirCrate())
    , path_(path)
    , retType(retType)
    , args(args)
    , fcn(fcn)
    , monomorphedRettype(nullptr)
    , monomorphedLocals(nullptr) {
    if (crate.langItems.count("owned_box") > 0) {
        langBox_ = &crate.langItems.at("owned_box");
    }
}

void MIRTypeResolve::setCurStmt(const MIRBasicBlock& bb, const MIRStatement& stmt) {
    BUG_ASSERT(&stmt >= &bb.statements.front());
    BUG_ASSERT(&stmt <= &bb.statements.back());
    this->setCurStmt(bb, &stmt - bb.statements.data());
}

void MIRTypeResolve::setCurStmt(const MIRBasicBlock& bb, unsigned int stmtIdx) {
    BUG_ASSERT(&bb >= &fcn.blocks.front());
    BUG_ASSERT(&bb <= &fcn.blocks.back());
    this->setCurStmt(&bb - fcn.blocks.data(), stmtIdx);
}

void MIRTypeResolve::setCurStmt(unsigned int bbIdx, unsigned int stmtIdx) {
    this->bbIdx = bbIdx;
    this->stmtIdx = stmtIdx;
}

void MIRTypeResolve::setCurStmtTerm(const MIRBasicBlock& bb) {
    BUG_ASSERT(&bb >= &fcn.blocks.front());
    BUG_ASSERT(&bb <= &fcn.blocks.back());
    this->setCurStmtTerm(&bb - fcn.blocks.data());
}

void MIRTypeResolve::setCurStmtTerm(unsigned int bbIdx) {
    this->bbIdx = bbIdx;
    this->stmtIdx = STMT_TERM;
}

MIRValueLifetime::MIRValueLifetime(Vector<bool> stmts)
    : statements(mv$(stmts))
{
}

bool MIRValueLifetime::overlaps(const MIRValueLifetime& x) const {
    BUG_ASSERT(statements.length() == x.statements.length());
    for (unsigned int i = 0; i < statements.length(); i++) {
        if (statements[i] && x.statements[i]) {
            return true;
        }
    }
    return false;
}

bool MIRVisitor::visitLvalue(const MIRLValue& lv, MIRValUsage u) {
    if (lv.root.is_Static()) {
        visitPath(lv.root.as_Static());
    }

    for (auto& w : lv.wrappers) {
        if (w.is_Index()) {
            if (visitLvalue(MIRLValue::newLocal(w.as_Index()), MIRValUsage::Read)) {
                return true;
            }
        }
    }
    return false;
}

bool MIRVisitorMut::visitLvalue(MIRLValue& lv, MIRValUsage u) {
    if (lv.root.is_Static()) {
        visitPath(lv.root.as_Static());
    }
    for (auto& w : lv.wrappers) {
        if (w.is_Index()) {
            auto lv = MIRLValue::newLocal(w.as_Index());
            bool rv = visitLvalue(lv, MIRValUsage::Read);
            ASSERT_BUG(Span(), lv.is_Local(), StringView("visit_lvalue on Index mutated the index to a non-local"));
            w = MIRLValue::Wrapper::newIndex(lv.as_Local());
            if (rv) {
                return true;
            }
        }
    }
    return false;
}

LValueCbVisitor::LValueCbVisitor(const MIRLvalueCallback& cb)
    : cb(cb)
{
}

auto LValueCbVisitor::visitLvalue(const MIRLValue& lv, MIRValUsage u) -> bool {
    if (cb.visitLvalue(lv, u)) {
        return true;
    }
    return MIRVisitor::visitLvalue(lv, u);
}

ValueLifetime::ValueLifetime(size_t stmtCount)
    : stmtBitmap()
{
    stmtBitmap.zero(stmtCount);
}

auto ValueLifetime::fill(const Vector<size_t>& blockOffsets, size_t bb, size_t firstStmt, size_t lastStmt) -> void {
    size_t limit = blockOffsets[bb + 1] - blockOffsets[bb] - 1;
    DEBUG(StringView("bb") << bb << StringView(" : ") << firstStmt << StringView("--") << lastStmt);
    BUG_ASSERT(firstStmt <= limit);
    BUG_ASSERT(lastStmt <= limit);
    for (size_t stmt = firstStmt; stmt <= lastStmt; stmt++) {
        stmtBitmap.mut(blockOffsets[bb] + stmt) = true;
    }
}

template <>
void stl::output<ZeroCopyOutput, MIRTypeResolve>(ZeroCopyOutput& os, const MIRTypeResolve& x) {
    x.fmtPos(os);
    return;
}

HIRPath MIRPinNewUnchecked(const HIRType* pinTy) {
    auto path = HIRPath(pinTy, "new_unchecked");
    path.data.as_UfcsInherent().implParams = pinTy->as_Path().path.data.as_Generic().params.clone();
    return path;
}
