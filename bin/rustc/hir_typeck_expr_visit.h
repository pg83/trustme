#pragma once

#include "hir_hir.h"
#include "hir_item_path.h"

class HIRTraitImpl;

struct WireBoard;

struct TypeckModuleState {
    const WireBoard& wb;
    const HIRCrate& crate;

    const HIRGenericPath* currentTrait;
    const HIRTraitImpl* currentTraitImpl;
    const HIRGenericParams* implGenerics;
    const HIRGenericParams* itemGenerics;

    ::std::vector<::std::pair<const HIRSimplePath*, const HIRTrait*>> traits;
    ::std::vector<HIRSimplePath> modPaths;

    TypeckModuleState(const WireBoard& wb);

    template <typename T>
    class NullOnDrop {
        T*& ptr;

    public:
        NullOnDrop(T*& ptr)
            : ptr(ptr)
        {
        }

        ~NullOnDrop() {
            ptr = nullptr;
        }
    };

    NullOnDrop<const HIRGenericPath> setCurrentTrait(const HIRGenericPath& p);

    NullOnDrop<const HIRTraitImpl> setCurrentTraitImpl(const HIRTraitImpl& impl);

    NullOnDrop<const HIRGenericParams> setImplGenerics(const HIRGenericParams& gps);

    NullOnDrop<const HIRGenericParams> setItemGenerics(const HIRGenericParams& gps);

    void prepareFromPath(const HIRItemPath& ip);

    void pushTraits(HIRItemPath p, const HIRModule& mod);

    void popTraits(const HIRModule& mod);
};

typedef ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> tArgs;
// Needs to mutate the pattern
// A null resultType means that the expression determines its own result type.
extern void TypecheckCode(const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr);
extern void TypecheckCodeCS(const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr);
extern void TypecheckCodeSimple(const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr);
