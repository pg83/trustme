#include "mir_helpers.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_type.h"
#include "trans_target.h"
#include "hir_encoded_literal.h"

#include <algorithm>

namespace {
    struct LValueCbVisitor: public MIRVisitor {
        const MIRLvalueCallback& cb;

        explicit LValueCbVisitor(const MIRLvalueCallback& cb);

        bool visitLvalue(const MIRLValue& lv, MIRValUsage u) override;
    };

    struct ValueLifetime {
        std::vector<bool> stmtBitmap;

        ValueLifetime(size_t stmtCount);

        void fill(const std::vector<size_t>& blockOffsets, size_t bb, size_t firstStmt, size_t lastStmt);

        void dumpDebug(const char* suffix, unsigned i, const std::vector<size_t>& blockOffsets);
    };

    void MIRHelperGetLifetimesDetermineValueLifetime(MIRTypeResolve& state, const MIRFunction& fcn, size_t bbIdx, size_t stmtIdx, const MIRLValue& lv, const std::vector<size_t>& blockOffsets, const std::vector<bool>& useBitmap, ValueLifetime& vl);

    void MIRHelperGetLifetimesDetermineValueLifetime(MIRTypeResolve& localMirRes, const MIRFunction& fcn, size_t bbIdx, size_t stmtIdx, const MIRLValue& lv, const std::vector<size_t>& blockOffsets, const std::vector<bool>& useBitmap, ValueLifetime& vl) {
        struct State {
            const std::vector<size_t>& blockOffsets;
            ValueLifetime& outVl;

            std::vector<unsigned int> bbHistory;
            size_t lastReadOfs;
            bool isBorrowed_;

            State(const std::vector<size_t>& blockOffsets, ValueLifetime& vl, size_t initBbIdx, size_t initStmtIdx)
                : blockOffsets(blockOffsets)
                , outVl(vl)
                , bbHistory()
                , lastReadOfs(initStmtIdx)
                , isBorrowed_(false)
            {
                bbHistory.push_back(initBbIdx);
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

            void fmt(std::ostream& os) const {
                os << "BB" << bbHistory.front() << "/" << lastReadOfs << "--";
                os << "[" << bbHistory << "]";
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
                BUG_ASSERT(bbHistory.size() > 0);
                if (bbHistory.size() == 1) {
                    outVl.fill(blockOffsets, bbHistory[0], lastReadOfs, stmtIdx);
                } else {
                    auto initBbIdx = bbHistory[0];
                    auto limit0 = blockOffsets[initBbIdx + 1] - blockOffsets[initBbIdx] - 1;
                    outVl.fill(blockOffsets, initBbIdx, lastReadOfs, limit0);

                    for (size_t i = 1; i < bbHistory.size() - 1; i++) {
                        size_t bbIdx = bbHistory[i];
                        BUG_ASSERT(bbIdx + 1 < blockOffsets.size());
                        size_t limit = blockOffsets[bbIdx + 1] - blockOffsets[bbIdx] - 1;
                        outVl.fill(blockOffsets, bbIdx, 0, limit);
                    }

                    auto bbIdx = bbHistory.back();
                    outVl.fill(blockOffsets, bbIdx, 0, stmtIdx);
                }

                lastReadOfs = stmtIdx;

                auto cur = this->bbHistory.back();
                this->bbHistory.clear();
                this->bbHistory.push_back(cur);
            }
        };

        struct Runner {
            MIRTypeResolve& mirRes;
            const MIRFunction& fcn;
            size_t initBbIdx;
            size_t initStmtIdx;
            const MIRLValue& lv;
            const std::vector<size_t>& blockOffsets;
            ValueLifetime& lifetimes;
            bool isCopy;

            std::vector<bool> visitedStatements;

            std::vector<std::pair<size_t, State>> statesToDo;

            Runner(MIRTypeResolve& localMirRes, const MIRFunction& fcn, size_t initBbIdx, size_t initStmtIdx, const MIRLValue& lv, const std::vector<size_t>& blockOffsets, ValueLifetime& vl)
                : mirRes(localMirRes)
                , fcn(fcn)
                , initBbIdx(initBbIdx)
                , initStmtIdx(initStmtIdx)
                , lv(lv)
                , blockOffsets(blockOffsets)
                , lifetimes(vl)
                , visitedStatements(lifetimes.stmtBitmap.size())
            {
                HIRTypeRef tmp;
                isCopy = mirRes.resolve.typeIsCopy(localMirRes.sp, mirRes.getLvalueType(tmp, lv));
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
                                DEBUG(mirRes << "Used");
                                state.markRead(stmtIdx);
                                wasUpdated = true;
                                break;
                            case MIRValUsage::Move:
                                if (lv.wrappers.size() == this->lv.wrappers.size()) {
                                    DEBUG(mirRes << (isCopy ? "Read" : "Moved"));
                                    state.markRead(stmtIdx);
                                    wasMoved = !isCopy;
                                } else {
                                    DEBUG(mirRes << "Used (partial)");
                                    state.markRead(stmtIdx);
                                    wasUpdated = true;
                                }
                                break;
                            case MIRValUsage::Borrow:
                                DEBUG(mirRes << "Borrowed");
                                state.markBorrowed(stmtIdx);
                                wasUpdated = true;
                                break;
                            case MIRValUsage::Write:
                                break;
                        }
                    }
                    for (const auto& w : lv.wrappers) {
                        if (w.is_Index() && this->lv.is_Local() && w.as_Index() == this->lv.as_Local()) {
                            DEBUG(mirRes << "Index used");
                            state.markRead(stmtIdx);
                            wasUpdated = true;
                        }
                    }
                    return false;
                };

                for (; stmtIdx < bb.statements.size(); stmtIdx++) {
                    const auto& stmt = bb.statements[stmtIdx];
                    mirRes.setCurStmt(bbIdx, stmtIdx);
                    visitedStatements[blockOffsets.at(bbIdx) + stmtIdx] = true;

                    wasUpdated = false;
                    visitMirLvalues(stmt, visitCb);
                    if (wasUpdated || wasMoved) {
                        DEBUG(mirRes << stmt);
                    }

                    if (wasMoved) {
                        DEBUG(mirRes << "Moved, return");
                        state.markRead(stmtIdx);
                        state.finalise(stmtIdx);
                        return;
                    }

                    switch (stmt.tag()) {
                        case MIRStatement::TAG_Assign: {
                            auto& se = stmt.as_Assign();
                            if (se.dst == lv) {
                                DEBUG(mirRes << "- Assigned to, return");
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
                                        break;
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
                        case MIRStatement::TAG_ScopeEnd: {
                            break;
                        }
                    }
                }
                mirRes.setCurStmtTerm(bbIdx);
                visitedStatements[blockOffsets.at(bbIdx) + stmtIdx] = true;

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
                        DEBUG(mirRes << "Incomplete");
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_Return: {
                        DEBUG(mirRes << "Return");
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_UnwindResume: {
                        DEBUG(mirRes << "UnwindResume");
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_UnwindTerminate: {
                        DEBUG(mirRes << "UnwindTerminate");
                        state.finalise(stmtIdx);
                        break;
                    }
                    case MIRTerminator::TAG_Unreachable: {
                        DEBUG(mirRes << "Unreachable");
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
                        for (size_t i = 0; i < te.targets.size(); i++) {
                            statesToDo.push_back(std::make_pair(te.targets[i], state.clone()));
                        }
                        if (te.validFlag != ~0u) {
                            statesToDo.push_back(std::make_pair(te.invalidTarget, mv$(state)));
                        }
                        break;
                    }
                    case MIRTerminator::TAG_SwitchValue: {
                        auto& te = bb.terminator.as_SwitchValue();
                        for (size_t i = 0; i < te.targets.size(); i++) {
                            statesToDo.push_back(std::make_pair(te.targets[i], state.clone()));
                        }
                        statesToDo.push_back(std::make_pair(te.defTarget, mv$(state)));
                        break;
                    }
                    case MIRTerminator::TAG_Drop: {
                        auto& te = bb.terminator.as_Drop();
                        if (te.slot == lv) {
                            DEBUG(mirRes << "Dropped, return");
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
                            DEBUG(mirRes << "Assigned (Call), return");
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
                        bool hasTarget = false;
                        if (te.retBlock != ~0u) {
                            statesToDo.push_back(std::make_pair(te.retBlock, state.clone()));
                            hasTarget = true;
                        }
                        for (const auto& p : te.params) {
                            if (const auto* target = p.opt_Label()) {
                                statesToDo.push_back(std::make_pair(*target, state.clone()));
                                hasTarget = true;
                            }
                        }
                        if (!hasTarget) {
                            state.finalise(stmtIdx);
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

            DEBUG("state.bb_history=[" << state.bbHistory << "], -> BB" << bbIdx);
            state.bbHistory.push_back(bbIdx);

            if (runner.visitedStatements.at(blockOffsets.at(bbIdx) + 0)) {
                if (vl.stmtBitmap.at(blockOffsets.at(bbIdx) + 0)) {
                    DEBUG("Looped (to already valid)");
                    state.markRead(0);
                    state.finalise(0);
                    continue;
                } else if (state.isBorrowed()) {
                    DEBUG("Looped (borrowed)");
                    state.markRead(0);
                    state.finalise(0);
                    continue;
                } else {
                    DEBUG("Looped (after last read), push for later");
                    postCheckList.push_back(std::make_pair(bbIdx, mv$(state)));
                    continue;
                }
            }

            if (vl.stmtBitmap.at(blockOffsets.at(bbIdx) + 0)) {
                DEBUG("Already valid in BB" << bbIdx);
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
                if (vl.stmtBitmap.at(blockOffsets.at(bbIdx) + 0)) {
                    change = true;
                    DEBUG("Looped (now valid)");
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

void MIRTypeResolve::fmtPos(std::ostream& os, bool includePath /*=false*/) const {
    if (includePath) {
        this->path_.write(os);
        os << " ";
    }
    os << "BB" << this->bbIdx << "/";
    if (this->stmtIdx == STMT_TERM) {
        os << "TERM";
    } else {
        os << this->stmtIdx;
    }
    os << ": ";
}

void MIRTypeResolve::printMsgCb(const char* tag, SpanMessageCallback& cb) const {
    auto& os = std::cerr;
    os << "MIR " << tag << ": ";
    fmtPos(os, true);
    cb.write(os);
    os << std::endl;
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
    MIR_ASSERT(*this, id < fcn.blocks.size(), "Block ID " << id << " out of range");
    return fcn.blocks[id];
}

const HIRTypeData* MIRTypeResolve::getStaticType(HIRTypeRef& tmp, const HIRPath& path) const {
    if (path.data.is_UfcsInherent() && path.data.as_UfcsInherent().item == "#type_id") {
        tmp = crate.types.unit();
        return tmp;
    }
    MonomorphState ms(crate.types);
    auto v = resolve.getValue(this->sp, path, ms, /*signature_only*/ true);
    MIR_ASSERT(*this, v.is_Static(), "LValue::Static not a static - " << path << " : " << v.tagStr());
    MIR_ASSERT(*this, v.as_Static(), "LValue::Static is null? - " << path << " : " << v.tagStr());
    if (ms.hasTypes()) {
        tmp = ms.monomorphType(sp, v.as_Static()->type);
        resolve.expandAssociatedTypes(this->sp, tmp);
        return tmp;
    } else {
        return v.as_Static()->type;
    }
}

const HIRTypeData* MIRTypeResolve::getLvalueType(HIRTypeRef& tmp, const MIRLValue& val, unsigned wrapperSkipCount /*=0*/) const {
    const HIRTypeData* rv = nullptr;
    switch (val.root.tag()) {
        case MIRLValue::Storage::TAG_Return: {
            rv = monomorphedRettype ? monomorphedRettype : retType;
            break;
        }
        case MIRLValue::Storage::TAG_Argument: {
            decltype(val.root.as_Argument()) e = val.root.as_Argument();
            MIR_ASSERT(*this, e < args.size(), "Argument " << val << " out of range (" << args.size() << ")");
            rv = args.at(e).second;
            break;
        }
        case MIRLValue::Storage::TAG_Local: {
            decltype(val.root.as_Local()) e = val.root.as_Local();
            MIR_ASSERT(*this, e < fcn.locals.size(), "Local " << val << " out of range (" << fcn.locals.size() << ")");
            rv = monomorphedLocals ? monomorphedLocals->at(e) : fcn.locals.at(e);
            break;
        }
        case MIRLValue::Storage::TAG_Static: {
            decltype(val.root.as_Static()) e = val.root.as_Static();
            rv = getStaticType(tmp, e);
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
            rv = this->getUnwrappedType(tmp, w, rv);
        }
    } else {
        BUG_ASSERT(wrapperSkipCount == 0);
    }
    return rv;
}

const HIRTypeData* MIRTypeResolve::getUnwrappedType(HIRTypeRef& tmp, const MIRLValue::Wrapper& w, const HIRTypeData* ty) const {
    switch (w.tag()) {
        case MIRLValue::Wrapper::TAG_Field: {
            decltype(w.as_Field()) fieldIndex = w.as_Field();
            switch ((*ty).tag()) {
                default:
                    MIR_BUG(*this, "Field access on unexpected type - " << ty);
                    break;
                case HIRTypeData::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    return te.inner;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    return te.inner;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& te = (*ty).as_Tuple();
                    MIR_ASSERT(*this, fieldIndex < te.size(), "Field index out of range in tuple " << fieldIndex << " >= " << te.size());
                    return te[fieldIndex];
                }
                case HIRTypeData::TAG_Path: {
                    auto& te = (*ty).as_Path();
                    // TODO: Cache result (to avoid needing to re-monomorph)
                    if (const auto* tep = te.binding.opt_Struct()) {
                        const auto& str = **tep;
                        auto maybeMonomorph = [&](const auto& fieldType) {
                            return resolve.monomorphExpandOpt(sp, tmp, fieldType, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                        };
                        switch (str.data.tag()) {
                            case HIRStructData::TAG_Unit: {
                                MIR_BUG(*this, "Field on unit-like struct - " << ty);
                                break;
                            }
                            case HIRStructData::TAG_Tuple: {
                                auto& se = str.data.as_Tuple();
                                MIR_ASSERT(*this, fieldIndex < se.size(), "Field index out of range in tuple-struct " << te.path);
                                return maybeMonomorph(se[fieldIndex].ent);
                                break;
                            }
                            case HIRStructData::TAG_Named: {
                                auto& se = str.data.as_Named();
                                MIR_ASSERT(*this, fieldIndex < se.size(), "Field index out of range in struct " << te.path);
                                return maybeMonomorph(se[fieldIndex].ty);
                                break;
                            }
                        }
                    } else if (const auto* tep = te.binding.opt_Union()) {
                        const auto& unm = **tep;
                        auto maybeMonomorph = [&](const HIRTypeData* t) -> const HIRTypeData* {
                            return resolve.monomorphExpandOpt(sp, tmp, t, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                        };
                        MIR_ASSERT(*this, fieldIndex < unm.variants.size(), "Field index out of range for union");
                        return maybeMonomorph(unm.variants.at(fieldIndex).ty);
                    } else {
                        MIR_BUG(*this, "Field access on invalid type - " << ty);
                    }
                    break;
                }
            }
            break;
        }
        case MIRLValue::Wrapper::TAG_Deref: {
            switch ((*ty).tag()) {
                default:
                    MIR_BUG(*this, "Deref on unexpected type - " << ty);
                    break;
                case HIRTypeData::TAG_Path: {
                    if (const auto* innerPtr = this->isTypeOwnedBox(ty)) {
                        return innerPtr;
                    } else {
                        MIR_BUG(*this, "Deref on unexpected type - " << ty);
                    }
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& te = (*ty).as_Pointer();
                    return te.inner;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& te = (*ty).as_Borrow();
                    return te.inner;
                }
            }
            break;
        }
        case MIRLValue::Wrapper::TAG_Index: {
            switch ((*ty).tag()) {
                default:
                    MIR_BUG(*this, "Index on unexpected type - " << ty);
                    break;
                case HIRTypeData::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    return te.inner;
                }
                case HIRTypeData::TAG_Array: {
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
                    MIR_BUG(*this, "Downcast on unexpected type - " << ty);
                    break;
                case HIRTypeData::TAG_Path: {
                    auto& te = (*ty).as_Path();
                    MIR_ASSERT(*this, te.binding.is_Enum() || te.binding.is_Union(), "Downcast on non-Enum");
                    if (te.binding.is_Enum()) {
                        const auto& enm = *te.binding.as_Enum();
                        MIR_ASSERT(*this, enm.data.is_Data(), "Downcast on non-data enum - " << ty);
                        const auto& variants = enm.data.as_Data();
                        MIR_ASSERT(*this, variantIndex < variants.size(), "Variant index out of range for " << ty);
                        const auto& variant = variants[variantIndex];

                        const auto& varTy = variant.type;
                        return resolve.monomorphExpandOpt(sp, tmp, varTy, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                    } else {
                        const auto& unm = *te.binding.as_Union();
                        MIR_ASSERT(*this, variantIndex < unm.variants.size(), "Variant index out of range");
                        const auto& variant = unm.variants[variantIndex];
                        const auto& varTy = variant.ty;

                        return resolve.monomorphExpandOpt(sp, tmp, varTy, MonomorphStatePtr(crate.types, ty, &te.path.data.as_Generic().params, nullptr));
                    }
                    break;
                }
            }
            break;
        }
    }
    UNREACHABLE();
}

const HIRTypeData* MIRTypeResolve::getParamType(HIRTypeRef& tmp, const MIRParam& val) const {
    switch (val.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = val.as_LValue();
            return getLvalueType(tmp, e);
        }
        case MIRParam::TAG_Constant: {
            auto& e = val.as_Constant();
            return tmp = getConstType(e);
        }
        case MIRParam::TAG_Borrow: {
            auto& e = val.as_Borrow();
            HIRTypeRef tmp2;
            return tmp = crate.types.borrow(e.type, getLvalueType(tmp2, e.val));
        }
    }
    UNREACHABLE();
}

HIRTypeRef MIRTypeResolve::getConstType(const MIRConstant& c) const {
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
            return crate.types.borrow(HIRBorrowType::Shared, crate.types.array(crate.types.primitive(HIRCoreType::U8), e.size()));
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
                    resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                } else {
                    return ty;
                }
            } else {
                MIR_BUG(*this, "get_const_type - Not a constant " << *e.p);
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
                    MIR_BUG(*this, "get_const_type - Function points to bad type: " << v.tagStr() << " - " << c);
                    break;
                case TypeckValuePtr::TAG_NotFound: {
                    MIR_BUG(*this, "get_const_type - ItemAddr points to unknown value - " << c);
                    break;
                }
                case TypeckValuePtr::TAG_Function: {
                    auto& ve = v.as_Function();
                    return crate.types.intern(HIRTypeData::make_NamedFunction({e.p->clone(), ve}));
                }
                case TypeckValuePtr::TAG_EnumConstructor: {
                    auto& ve = v.as_EnumConstructor();
                    return crate.types.intern(HIRTypeData::make_NamedFunction({e.p->clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}));
                }
                case TypeckValuePtr::TAG_StructConstructor: {
                    auto& ve = v.as_StructConstructor();
                    return crate.types.intern(HIRTypeData::make_NamedFunction({e.p->clone(), ve.s}));
                }
            }
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& e = c.as_ItemAddr();
            MonomorphState p(crate.types);
            ASSERT_BUG(sp, e, "get_const_type - " << c);
            auto v = resolve.getValue(this->sp, *e, p, /*signature_only=*/true);
            switch (v.tag()) {
                case TypeckValuePtr::TAG_NotFound: {
                    MIR_BUG(*this, "get_const_type - ItemAddr points to unknown value - " << c);
                    break;
                }
                case TypeckValuePtr::TAG_NotYetKnown: {
                    if (e->data.is_UfcsKnown()) {
                        const auto& pe = e->data.as_UfcsKnown();
                        if (pe.item == "vtable#" && pe.trait.path == HIRSimplePath()) {
                            std::vector<HIRTypeRef> fields;
                            fields.push_back(crate.types.primitive(HIRCoreType::Usize));
                            fields.push_back(crate.types.primitive(HIRCoreType::Usize));
                            fields.push_back(crate.types.primitive(HIRCoreType::Usize));
                            return crate.types.borrow(HIRBorrowType::Shared, crate.types.tuple(mv$(fields)));
                        }
                    }
                    MIR_BUG(*this, "get_const_type - get_value returned NotYetKnown with signature_only=true");
                    break;
                }
                case TypeckValuePtr::TAG_Constant: {
                    auto& ve = v.as_Constant();
                    const auto& ty = ve->type;
                    HIRTypeRef rv;
                    if (monomorphiseTypeNeeded(ty)) {
                        rv = p.monomorphType(this->sp, ty);
                        resolve.expandAssociatedTypes(this->sp, rv);
                    } else {
                        rv = ty;
                    }
                    return crate.types.borrow(HIRBorrowType::Shared, rv);
                }
                case TypeckValuePtr::TAG_Static: {
                    auto& ve = v.as_Static();
                    const auto& ty = ve->type;
                    HIRTypeRef rv;
                    if (monomorphiseTypeNeeded(ty)) {
                        rv = p.monomorphType(this->sp, ty);
                        resolve.expandAssociatedTypes(this->sp, rv);
                    } else {
                        rv = ty;
                    }
                    return crate.types.borrow(HIRBorrowType::Shared, rv);
                }
                case TypeckValuePtr::TAG_Function: {
                    auto& ve = v.as_Function();
                    auto rv = crate.types.function((HIRTypeData::Data_NamedFunction{e->clone(), ve}).decay(crate.types, this->sp));
                    resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
                case TypeckValuePtr::TAG_EnumValue: {
                    MIR_BUG(*this, "get_const_type - ItemAddr points to an enum value - " << c);
                    break;
                }
                case TypeckValuePtr::TAG_EnumConstructor: {
                    auto& ve = v.as_EnumConstructor();
                    auto rv = crate.types.function((HIRTypeData::Data_NamedFunction{e->clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}).decay(crate.types, this->sp));
                    resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
                case TypeckValuePtr::TAG_StructConstant: {
                    MIR_BUG(*this, c << " pointing to a struct constant");
                    break;
                }
                case TypeckValuePtr::TAG_StructConstructor: {
                    auto& ve = v.as_StructConstructor();
                    auto rv = crate.types.function((HIRTypeData::Data_NamedFunction{e->clone(), ve.s}).decay(crate.types, this->sp));
                    resolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
            }
            break;
        }
    }
    UNREACHABLE();
}

bool MIRTypeResolve::lvalueIsCopy(const MIRLValue& val) const {
    HIRTypeRef tmp;
    return resolve.typeIsCopy(this->sp, getLvalueType(tmp, val));
}

const HIRTypeData* MIRTypeResolve::isTypeOwnedBox(const HIRTypeData* ty) const {
    return resolve.isTypeOwnedBox(ty);
}

size_t MIRTypeResolve::intrinsicOffsetOf(const HIRTypeData* ty, const std::vector<MIRParam>& values) const {
    const auto* curTy = ty;
    size_t baseOfs = 0;
    for (size_t i = 0; i < values.size(); i++) {
        MIR_ASSERT(*this, values[i].is_Constant(), "Arguments to `offset_of` must be constants");
        size_t idx = 0;
        {
            auto& tuMatch = values[i].as_Constant();
            switch (tuMatch.tag()) {
                default:
                    MIR_TODO(*this, "offset_of: field " << values[i]);
                    break;
                case MIRConstant::TAG_Int: {
                    auto& fieldIdx = tuMatch.as_Int();
                    MIR_ASSERT(*this, fieldIdx.v.isI64() && fieldIdx.v >= S128(0), "Invalid tuple field index " << fieldIdx.v);
                    idx = static_cast<size_t>(fieldIdx.v.truncateI64());
                    break;
                }
                case MIRConstant::TAG_Uint: {
                    auto& fieldIdx = tuMatch.as_Uint();
                    MIR_ASSERT(*this, fieldIdx.v.isU64() && fieldIdx.v <= U128(SIZE_MAX), "Invalid tuple field index " << fieldIdx.v);
                    idx = static_cast<size_t>(fieldIdx.v.truncateU64());
                    break;
                }
                case MIRConstant::TAG_StaticString: {
                    auto& fieldName = tuMatch.as_StaticString();
                    char* end = nullptr;
                    auto numericIdx = std::strtoul(fieldName.c_str(), &end, 10);
                    if (end != fieldName.c_str() && *end == '\0') {
                        MIR_ASSERT(*this, numericIdx <= SIZE_MAX, "Invalid tuple field index " << fieldName);
                        idx = static_cast<size_t>(numericIdx);
                    } else if (const auto* tyPath = curTy->opt_Path()) {
                        if (const auto* bep = tyPath->binding.opt_Struct()) {
                            const auto& str = **bep;
                            switch (str.data.tag()) {
                                case HIRStructData::TAG_Named: {
                                    auto& fields = str.data.as_Named();
                                    idx = std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                                        return x.name == fieldName;
                                    }) - fields.begin();
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    MIR_BUG(*this, "Named field on tuple struct: " << curTy << " ." << fieldName);
                                    break;
                                }
                                case HIRStructData::TAG_Unit: {
                                    auto& _ = str.data.as_Unit();
                                    MIR_BUG(*this, "Empty struct: " << curTy << " ." << fieldName);
                                    break;
                                }
                            }
                        } else if (const auto* bep = tyPath->binding.opt_Union()) {
                            const auto& unm = **bep;
                            const auto& fields = unm.variants;
                            idx = std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                                return x.name == fieldName;
                            }) - fields.begin();
                        } else if (const auto* bep = tyPath->binding.opt_Enum()) {
                            const auto& enm = **bep;
                            MIR_ASSERT(*this, enm.data.is_Data(), "Non-Data enum: " << curTy << " ." << fieldName);
                            const auto& fields = enm.data.as_Data();
                            idx = std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                                return x.name == fieldName;
                            }) - fields.begin();
                        } else {
                            MIR_TODO(*this, "offset_of: named field/variant - " << fieldName);
                        }
                    } else {
                        MIR_TODO(*this, "offset_of: named field/variant - " << fieldName);
                    }
                    break;
                }
            }
        }
        auto* repr = TargetGetTypeRepr(this->sp, resolve, curTy);
        if (!repr) {
            MIR_BUG(*this, "Calling `offset_of!` on type with non-defined repr: " << curTy);
        }
        MIR_ASSERT(*this, idx < repr->fields.size(), "Field index " << idx << " out of range for " << curTy);
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
    auto rv = FMT("");
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
            add(FMT(b.first << " = " << intrinsicTypeNameImpl(b.second.type, genericPlaceholders)));
        }
    }
    if (!rv.empty()) {
        rv += ">";
    }
    return rv;
}

MIRTypeResolve::TypeNameString MIRTypeResolve::typeNameForItemPath(const HIRPath& path, bool genericPlaceholders) const {
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            const auto& pe = path.data.as_Generic();
            return typeNameForSimplePath(pe.path) + typeNameForPathArgs(pe.params, nullptr, genericPlaceholders);
        }
        case HIRPathData::TAG_UfcsInherent: {
            const auto& pe = path.data.as_UfcsInherent();
            return FMT(intrinsicTypeNameImpl(pe.type, genericPlaceholders) << "::" << pe.item << typeNameForPathArgs(pe.params, nullptr, genericPlaceholders));
        }
        case HIRPathData::TAG_UfcsKnown: {
            const auto& pe = path.data.as_UfcsKnown();
            auto trait = typeNameForSimplePath(pe.trait.path) + typeNameForPathArgs(pe.trait.params, nullptr, genericPlaceholders);
            return FMT("<" << intrinsicTypeNameImpl(pe.type, genericPlaceholders) << " as " << trait << ">::" << pe.item << typeNameForPathArgs(pe.params, nullptr, genericPlaceholders));
        }
        case HIRPathData::TAG_UfcsUnknown:
            break;
    }
    return FMT(path);
}

MIRTypeResolve::TypeNameString MIRTypeResolve::intrinsicTypeName(const HIRTypeData* ty) const {
    return intrinsicTypeNameImpl(ty, false);
}

MIRTypeResolve::TypeNameString MIRTypeResolve::intrinsicTypeNameImpl(const HIRTypeData* ty, bool genericPlaceholders) const {
    if (genericPlaceholders && ty->is_Generic()) {
        return "_";
    }

    if (const auto* te = ty->opt_Tuple()) {
        auto rv = FMT("(");
        for (size_t i = 0; i < te->size(); i++) {
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
        return FMT("[" << intrinsicTypeNameImpl(te->inner, genericPlaceholders) << "; " << te->size << "]");
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
            rv += FMT("extern \"" << te->abi << "\" ");
        }
        rv += "fn(";
        for (size_t i = 0; i < te->argTypes.size(); i++) {
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
        return typeNameForSimplePath(gp.path) + typeNameForPathArgs(gp.params, nullptr, genericPlaceholders);
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
                for (size_t i = 0; i < args.size(); i++) {
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
        auto rv = FMT("dyn ");
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
    struct ConstAdapter final: public MIRTargetVisitorMut {
        MIRTargetVisitor& cb;

        explicit ConstAdapter(MIRTargetVisitor& cb)
            : cb(cb)
        {
        }

        void visitTarget(MIRBasicBlockId& target) override {
            cb.visitTarget(target);
        }
    } adapter{cb};

    visitTerminatorTargetMut(const_cast<MIRTerminator&>(term), adapter);
}

#if 1

// TODO: Improved algorithm

MIRValueLifetimes MIRHelperGetLifetimes(MIRTypeResolve& state, const MIRFunction& fcn, bool dumpDebug, const std::vector<bool>* mask /*=nullptr*/) {
    TRACE_FUNCTION_F(state);
    size_t statementCount = 0;
    std::vector<size_t> blockOffsets;
    blockOffsets.reserve(fcn.blocks.size());
    for (const auto& bb : fcn.blocks) {
        blockOffsets.push_back(statementCount);
        statementCount += bb.statements.size() + 1;
    }
    blockOffsets.push_back(statementCount);

    std::vector<ValueLifetime> slotLifetimes(fcn.locals.size(), ValueLifetime(statementCount));

    std::vector<std::vector<bool>> slotReadBitmaps(fcn.locals.size());
    {
        for (auto& b : slotReadBitmaps) {
            b.resize(statementCount);
        }
        size_t pos = 0;
        auto useCb = [&](const MIRLValue& tlv, MIRValUsage vu) {
            if (tlv.root.is_Local()) {
                if (vu != MIRValUsage::Write) {
                    slotReadBitmaps[tlv.root.as_Local()][pos] = true;
                }
            }
            for (const auto& w : tlv.wrappers) {
                if (w.is_Index()) {
                    slotReadBitmaps[w.as_Index()][pos] = true;
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
                if (!mask || mask->at(de)) {
                    MIRHelperGetLifetimesDetermineValueLifetime(state, fcn, bbIdx, stmtIdx, lv, blockOffsets, slotReadBitmaps[de], slotLifetimes[de]);
                    slotLifetimes[de].fill(blockOffsets, bbIdx, stmtIdx, stmtIdx);
                }
            }
        };

        const auto& bb = fcn.blocks[bbIdx];
        for (size_t stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
            state.setCurStmt(bbIdx, stmtIdx);
            const auto& stmt = bb.statements[stmtIdx];
            if (const auto* se = stmt.opt_Assign()) {
                assignedLvalue(bbIdx, stmtIdx + 1, se->dst);
            } else if (const auto* se = stmt.opt_Asm()) {
                for (const auto& e : se->outputs) {
                    assignedLvalue(bbIdx, stmtIdx + 1, e.second);
                }
            }
        }
        state.setCurStmtTerm(bbIdx);

        if (bb.terminator.is_Call()) {
            auto& te = bb.terminator.as_Call();
            assignedLvalue(te.retBlock, 0, te.retVal);
        }
    }

    if (dumpDebug) {
        for (size_t i = 0; i < slotLifetimes.size(); i++) {
            slotLifetimes[i].dumpDebug("_", i, blockOffsets);
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

#else

MIRValueLifetimes MIRHelperGetLifetimes(MIRTypeResolve& state, const MIRFunction& fcn, bool dumpDebug) {
    TRACE_FUNCTION_F(state);

    // TODO: If a value is borrowed, assume it lives forevermore

    // TODO: Add a statement type StorageDead (or similar?) that indicates the point where a values scope ends

    struct Position {
        size_t pathIndex = 0;
        unsigned int stmtIdx = 0;

        bool operator==(const Position& x) const {
            return pathIndex == x.pathIndex && stmtIdx == x.stmtIdx;
        }
    };

    struct ProtoLifetime {
        Position start;
        Position end;

        bool isEmpty() const {
            return start == end;
        }

        bool isBorrowed() const {
            return this->end == Position{~0u, ~0u};
        }
    };

    static unsigned NEXT_INDEX = 0;

    struct State {
        unsigned int index = 0;
        std::vector<unsigned int> blockPath;
        std::vector<unsigned int> blockChangeIdx;
        unsigned int curChangeIdx = 0;

        std::vector<ProtoLifetime> tmpEnds;
        std::vector<ProtoLifetime> varEnds;

        State(const MIRFunction& fcn)
            : tmpEnds(fcn.temporaries.size(), ProtoLifetime())
            , varEnds(fcn.namedVariables.size(), ProtoLifetime())
        {
        }

        State clone() const {
            auto rv = *this;
            rv.index = ++NEXT_INDEX;
            return rv;
        }
    };

    NEXT_INDEX = 0;

    size_t statementCount = 0;
    std::vector<size_t> blockOffsets;
    blockOffsets.reserve(fcn.blocks.size());
    for (const auto& bb : fcn.blocks) {
        blockOffsets.push_back(statementCount);
        statementCount += bb.statements.size() + 1;
    }

    std::vector<ValueLifetime> temporaryLifetimes(fcn.temporaries.size(), ValueLifetime(statementCount));
    std::vector<ValueLifetime> variableLifetimes(fcn.namedVariables.size(), ValueLifetime(statementCount));

    struct BlockSeenLifetimes {
        bool hasState = false;
        const std::vector<size_t>& blockOffsets;
        std::vector<std::vector<unsigned int>> tmp;
        std::vector<std::vector<unsigned int>> var;

        BlockSeenLifetimes(const std::vector<size_t>& blockOffsets, const MIRFunction& fcn)
            : blockOffsets(blockOffsets)
            , tmp(fcn.temporaries.size())
            , var(fcn.namedVariables.size())
        {
        }

        bool hasState() const {
            return hasState;
        }

        bool tryMerge(const State& valState) const {
            // TODO: This logic isn't quite correct. Just becase a value's existing end is already marked as valid,

            auto tryMergeLft = [&](const ProtoLifetime& lft, const std::vector<unsigned int>& seen) -> bool {
                if (lft.isEmpty()) {
                    return false;
                }
                // TODO: What should be done for borrow flagged values
                if (lft.isBorrowed()) {
                    return false;
                }
                auto endIdx = blockOffsets.at(valState.blockPath.at(lft.end.pathIndex)) + lft.end.stmtIdx;

                auto it = std::find(seen.begin(), seen.end(), endIdx);
                return (it == seen.end());
            };
            for (size_t i = 0; i < valState.tmpEnds.size(); i++) {
                if (tryMergeLft(valState.tmpEnds[i], this->tmp[i])) {
                    return true;
                }
            }
            for (size_t i = 0; i < valState.varEnds.size(); i++) {
                if (tryMergeLft(valState.varEnds[i], this->var[i])) {
                    return true;
                }
            }
            return false;
        }

        bool merge(const State& valState) {
            bool rv = false;
            auto mergeLft = [&](const ProtoLifetime& lft, std::vector<unsigned int>& seen) -> bool {
                if (lft.isEmpty()) {
                    return false;
                }
                // TODO: What should be done for borrow flagged values
                if (lft.end == Position{~0u, ~0u}) {
                    return false;
                }
                auto endIdx = blockOffsets.at(valState.blockPath.at(lft.end.pathIndex)) + lft.end.stmtIdx;

                auto it = std::find(seen.begin(), seen.end(), endIdx);
                if (it == seen.end()) {
                    seen.push_back(endIdx);
                    return true;
                } else {
                    return false;
                }
            };
            for (size_t i = 0; i < valState.tmpEnds.size(); i++) {
                rv |= mergeLft(valState.tmpEnds[i], this->tmp[i]);
            }
            for (size_t i = 0; i < valState.varEnds.size(); i++) {
                rv |= mergeLft(valState.varEnds[i], this->var[i]);
            }
            hasState = true;
            return rv;
        }
    };

    std::vector<BlockSeenLifetimes> blockSeenLifetimes(fcn.blocks.size(), BlockSeenLifetimes(blockOffsets, fcn));

    State initState(fcn);

    std::vector<std::pair<unsigned int, State>> todoQueue;
    todoQueue.push_back(std::make_pair(0, mv$(initState)));

    while (!todoQueue.empty()) {
        auto bbIdx = todoQueue.back().first;
        auto valState = mv$(todoQueue.back().second);
        todoQueue.pop_back();
        state.setCurStmt(bbIdx, 0);

        // TODO: Maybe also store the range (as a sequence of {block,start,end})
        auto addLifetimeS = [&](State& valState, const MIRLValue& lv, const Position& start, const Position& end) {
            BUG_ASSERT(start.pathIndex <= end.pathIndex);
            BUG_ASSERT(start.pathIndex < end.pathIndex || start.stmtIdx <= end.stmtIdx);
            if (start.pathIndex == end.pathIndex && start.stmtIdx == end.stmtIdx) {
                return;
            }
            DEBUG("[add_lifetime] " << lv << " (" << start.pathIndex << "," << start.stmtIdx << ") -- (" << end.pathIndex << "," << end.stmtIdx << ")");
            ValueLifetime* lft;
            if (const auto* e = lv.opt_Temporary()) {
                lft = &temporaryLifetimes[e->idx];
            } else if (const auto* e = lv.opt_Variable()) {
                lft = &variableLifetimes[*e];
            } else {
                MIR_TODO(state, "[add_lifetime] " << lv);
                return;
            }

            bool didSet = false;
            unsigned int j = start.stmtIdx;
            unsigned int i = start.pathIndex;
            while (i <= end.pathIndex && i < valState.blockPath.size()) {
                auto bbIdx = valState.blockPath.at(i);
                const auto& bb = fcn.blocks[bbIdx];
                MIR_ASSERT(state, j <= bb.statements.size(), "");
                MIR_ASSERT(state, bbIdx < blockOffsets.size(), "");

                auto blockBase = blockOffsets.at(bbIdx);
                auto idx = blockBase + j;
                if (!lft->stmtBitmap.at(idx)) {
                    lft->stmtBitmap[idx] = true;
                    didSet = true;
                }

                if (i == end.pathIndex && j == (end.stmtIdx != ~0u ? end.stmtIdx : bb.statements.size())) {
                    break;
                }

                if (j == bb.statements.size()) {
                    j = 0;
                    i++;
                } else {
                    j++;
                }
            }

            if (didSet) {
                DEBUG("[add_lifetime] " << lv << " (" << start.pathIndex << "," << start.stmtIdx << ") -- (" << end.pathIndex << "," << end.stmtIdx << ") - New information");
                valState.curChangeIdx += 1;
            }
        };
        auto addLifetime = [&](const MIRLValue& lv, const Position& start, const Position& end) {
            addLifetimeS(valState, lv, start, end);
        };

        auto applyState = [&](State& state) {
            for (unsigned i = 0; i < fcn.temporaries.size(); i++) {
                addLifetimeS(state, MIRLValue::make_Temporary({i}), state.tmpEnds[i].start, state.tmpEnds[i].end);
            }
            for (unsigned i = 0; i < fcn.namedVariables.size(); i++) {
                addLifetimeS(state, MIRLValue::make_Variable({i}), state.varEnds[i].start, state.varEnds[i].end);
            }
        };
        auto addToVisit = [&](unsigned int newBbIdx, State newState) {
            auto& bbMemoryEnt = blockSeenLifetimes[newBbIdx];
            if (!bbMemoryEnt.hasState()) {
                DEBUG(state << " state" << newState.index << " -> bb" << newBbIdx << " (no existing state)");
            } else if (bbMemoryEnt.tryMerge(newState)) {
            } else {
                // TODO: Acquire from the target block the actual end of any active lifetimes, then apply them.

                DEBUG(state << " state" << newState.index << " -> bb" << newBbIdx << " - No new state, no push");
                auto bmIdx = blockOffsets[newBbIdx];
                Position curPos;
                curPos.pathIndex = valState.blockPath.size() - 1;
                curPos.stmtIdx = fcn.blocks[bbIdx].statements.size();
                for (unsigned i = 0; i < fcn.temporaries.size(); i++) {
                    if (!newState.tmpEnds[i].isEmpty() && temporaryLifetimes[i].stmtBitmap[bmIdx]) {
                        DEBUG("- tmp$" << i << " - Active in target, assume active");
                        newState.tmpEnds[i].end = curPos;
                    }
                }
                for (unsigned i = 0; i < fcn.namedVariables.size(); i++) {
                    if (!newState.varEnds[i].isEmpty() && variableLifetimes[i].stmtBitmap[bmIdx]) {
                        DEBUG("- var$" << i << " - Active in target, assume active");
                        newState.varEnds[i].end = curPos;
                    }
                }
                applyState(newState);
                return;
            }
            todoQueue.push_back(std::make_pair(newBbIdx, mv$(newState)));
        };

        {
            auto& bbMemoryEnt = blockSeenLifetimes[bbIdx];
            bool hadState = bbMemoryEnt.hasState();
            bool hasNew = bbMemoryEnt.merge(valState);

            if (!hasNew && hadState) {
                DEBUG(state << " state" << valState.index << " - No new entry state");
                applyState(valState);

                continue;
            }
        }

        {
            auto it = std::find(valState.blockPath.rbegin(), valState.blockPath.rend(), bbIdx);
            if (it != valState.blockPath.rend()) {
                auto idx = &*it - &valState.blockPath.front();
                if (valState.blockChangeIdx[idx] == valState.curChangeIdx) {
                    DEBUG(state << " " << valState.index << " Loop and no change");
                    continue;
                } else {
                    BUG_ASSERT(valState.blockChangeIdx[idx] < valState.curChangeIdx);
                    DEBUG(state << " " << valState.index << " --- Loop, " << valState.curChangeIdx - valState.blockChangeIdx[idx] << " changes");
                }
            } else {
                DEBUG(state << " " << valState.index << " ---");
            }
            valState.blockPath.push_back(bbIdx);
            valState.blockChangeIdx.push_back(valState.curChangeIdx);
        }

        Position curPos;
        curPos.pathIndex = valState.blockPath.size() - 1;
        curPos.stmtIdx = 0;
        auto lvalueRead = [&](const MIRLValue& lv) {
            ProtoLifetime* slot;
            if (const auto* e = lv.opt_Temporary()) {
                slot = &valState.tmpEnds.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &valState.varEnds.at(*e);
            } else {
                return;
            }
            slot->end = curPos;
        };
        auto lvalueSet = [&](const MIRLValue& lv) {
            ProtoLifetime* slot;
            if (const auto* e = lv.opt_Temporary()) {
                slot = &valState.tmpEnds.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &valState.varEnds.at(*e);
            } else {
                return;
            }
            slot->end = curPos;
            addLifetime(lv, slot->start, slot->end);
            slot->start = curPos;
        };
        auto lvalueBorrow = [&](const MIRLValue& lv) {
            ProtoLifetime* slot;
            if (const auto* e = lv.opt_Temporary()) {
                slot = &valState.tmpEnds.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &valState.varEnds.at(*e);
            } else {
                return;
            }
            // TODO: Flag this value as currently being borrowed (a flag that never clears)
            slot->end = Position{~0u, ~0u};
        };
        auto visitLvalCb = [&](const auto& lv, ValUsage vu) -> bool {
            if (vu == ValUsage::Read) {
                lvalueRead(lv);
            }
            if (vu == ValUsage::Borrow) {
                lvalueBorrow(lv);
            }
            if (vu == ValUsage::Write) {
                lvalueSet(lv);
            }
            return false;
        };

        for (const auto& stmt : fcn.blocks[bbIdx].statements) {
            auto stmtIdx = &stmt - &fcn.blocks[bbIdx].statements.front();
            curPos.stmtIdx = stmtIdx;
            state.setCurStmt(bbIdx, stmtIdx);

            DEBUG(state << " " << stmt);
            if (const auto* e = stmt.opt_Drop()) {
                visitMirLvalues(stmt, [&](const auto& lv, ValUsage vu) -> bool {
                    if (vu == ValUsage::Read) {
                        lvalueRead(lv);
                    }
                    return false;
                });
                lvalueRead(e->slot);
                lvalueSet(e->slot);
            } else {
                visitMirLvalues(stmt, visitLvalCb);
            }
        }
        curPos.stmtIdx = fcn.blocks[bbIdx].statements.size();

        state.setCurStmtTerm(bbIdx);
        DEBUG(state << "TERM " << fcn.blocks[bbIdx].terminator);
        switch (fcn.blocks[bbIdx].terminator.tag()) {
            case MIRTerminator::TAG_Incomplete: {
                auto& e = fcn.blocks[bbIdx].terminator.as_Incomplete();
                break;
            }
            case MIRTerminator::TAG_Return: {
                auto& e = fcn.blocks[bbIdx].terminator.as_Return();
                applyState(valState);
                break;
            }
            case MIRTerminator::TAG_UnwindResume: {
                auto& e = fcn.blocks[bbIdx].terminator.as_UnwindResume();
                applyState(valState);
                break;
            }
            case MIRTerminator::TAG_UnwindTerminate: {
                auto& e = fcn.blocks[bbIdx].terminator.as_UnwindTerminate();
                applyState(valState);
                break;
            }
            case MIRTerminator::TAG_Unreachable: {
                auto& e = fcn.blocks[bbIdx].terminator.as_Unreachable();
                applyState(valState);
                break;
            }
            case MIRTerminator::TAG_Goto: {
                auto& e = fcn.blocks[bbIdx].terminator.as_Goto();
                addToVisit(e, mv$(valState));
                break;
            }
            case MIRTerminator::TAG_If: {
                auto& e = fcn.blocks[bbIdx].terminator.as_If();
                visitMirLvalue(e.cond, ValUsage::Read, visitLvalCb);

                addToVisit(e.bb0, valState.clone());
                addToVisit(e.bb1, mv$(valState));
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& e = fcn.blocks[bbIdx].terminator.as_Switch();
                visitMirLvalue(e.val, ValUsage::Read, visitLvalCb);
                std::set<unsigned int> tgts;
                for (const auto& tgt : e.targets) {
                    tgts.insert(tgt);
                }

                for (const auto& tgt : tgts) {
                    auto vs = (tgt == *tgts.rbegin() ? mv$(valState) : valState.clone());
                    addToVisit(tgt, mv$(vs));
                }
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& e = fcn.blocks[bbIdx].terminator.as_Drop();
                visitMirLvalue(e.slot, ValUsage::Move, visitLvalCb);
                if (e.unwind.is_Cleanup()) {
                    auto& target = e.unwind.as_Cleanup();
                    addToVisit(target, valState.clone());
                }
                addToVisit(e.target, mv$(valState));
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& e = fcn.blocks[bbIdx].terminator.as_Call();
                if (const auto* f = e.fcn.opt_Value()) {
                    visitMirLvalue(*f, ValUsage::Read, visitLvalCb);
                }
                for (const auto& arg : e.args) {
                    if (const auto* e = arg.opt_LValue()) {
                        visitMirLvalue(*e, ValUsage::Read, visitLvalCb);
                    }
                }

                if (e.unwind.is_Cleanup()) {
                    auto& target = e.unwind.as_Cleanup();
                    addToVisit(target, valState.clone());
                }

                // TODO: If the function returns !, don't follow the ret_block
                lvalueSet(e.retVal);
                addToVisit(e.retBlock, mv$(valState));
                break;
            }
        }
    }

    if (dumpDebug) {
        for (unsigned int i = 0; i < temporaryLifetimes.size(); i++) {
            temporaryLifetimes[i].dumpDebug("tmp", i, blockOffsets);
        }
        for (unsigned int i = 0; i < variableLifetimes.size(); i++) {
            variableLifetimes[i].dumpDebug("var", i, blockOffsets);
        }
    }

    MIRValueLifetimes rv;
    rv.blockOffsets = mv$(blockOffsets);
    rv.temporaries.reserve(temporaryLifetimes.size());
    for (auto& lft : temporaryLifetimes) {
        rv.temporaries.push_back(MIRValueLifetime(mv$(lft.stmtBitmap)));
    }
    rv.variables.reserve(variableLifetimes.size());
    for (auto& lft : variableLifetimes) {
        rv.variables.push_back(MIRValueLifetime(mv$(lft.stmtBitmap)));
    }

    return rv;
}
#endif

MIRTypeResolve::MIRTypeResolve(const Span& sp, const ::StaticTraitResolve& resolve, const MIRPathCallback& path, const HIRTypeData* retType, const argsT& args, const MIRFunction& fcn)
    : sp(sp)
    , resolve(resolve)
    , crate(resolve.hirCrate())
    , path_(path)
    , retType(retType)
    , args(args)
    , fcn(fcn)
    , monomorphedRettype(nullptr)
    , monomorphedLocals(nullptr)
{
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

MIRValueLifetime::MIRValueLifetime(std::vector<bool> stmts)
    : statements(mv$(stmts))
{
}

bool MIRValueLifetime::isUsed() const {
    for (auto v : statements) {
        if (v) {
            return true;
        }
    }
    return false;
}

bool MIRValueLifetime::overlaps(const MIRValueLifetime& x) const {
    BUG_ASSERT(statements.size() == x.statements.size());
    for (unsigned int i = 0; i < statements.size(); i++) {
        if (statements[i] && x.statements[i]) {
            return true;
        }
    }
    return false;
}

void MIRValueLifetime::unify(const MIRValueLifetime& x) {
    BUG_ASSERT(statements.size() == x.statements.size());
    for (unsigned int i = 0; i < statements.size(); i++) {
        if (x.statements[i]) {
            statements[i] = true;
        }
    }
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
            ASSERT_BUG(Span(), lv.is_Local(), "visit_lvalue on Index mutated the index to a non-local");
            w = MIRLValue::Wrapper::newIndex(lv.as_Local());
            if (rv) {
                return true;
            }
        }
    }
    return false;
}

std::ostream& operator<<(std::ostream& os, const MIRTypeResolve& x) {
    x.fmtPos(os);
    return os;
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
    : stmtBitmap(stmtCount)
{
}

auto ValueLifetime::fill(const std::vector<size_t>& blockOffsets, size_t bb, size_t firstStmt, size_t lastStmt) -> void {
    size_t limit = blockOffsets[bb + 1] - blockOffsets[bb] - 1;
    DEBUG("bb" << bb << " : " << firstStmt << "--" << lastStmt);
    BUG_ASSERT(firstStmt <= limit);
    BUG_ASSERT(lastStmt <= limit);
    for (size_t stmt = firstStmt; stmt <= lastStmt; stmt++) {
        stmtBitmap[blockOffsets[bb] + stmt] = true;
    }
}

auto ValueLifetime::dumpDebug(const char* suffix, unsigned i, const std::vector<size_t>& blockOffsets) -> void {
    std::string name = FMT(suffix << "$" << i);
    while (name.size() < 3 + 1 + 3) {
        name += " ";
    }
    DEBUG(name << " : " << FMT_CB(os, for (unsigned int j = 0; j < this->stmtBitmap.size(); j++) {
              if (j != 0 && find(blockOffsets.begin(), blockOffsets.end(), j) != blockOffsets.end()) {
                  os << "|";
              }
              os << (this->stmtBitmap[j] ? "X" : " ");
          }));
}
