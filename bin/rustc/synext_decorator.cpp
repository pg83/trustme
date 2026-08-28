#include "synext_decorator.h"

#include "common.h"
#include "synext.h"
#include "ast_ast.h"
#include "hir_hir.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "ast_generics.h"
#include "parse_common.h"
#include "expand_common.h"
#include "parse_ttstream.h"
#include "parse_parseerror.h"
#include "expand_proc_macro.h"
#include "parse_interpolated_fragment.h"

#include <std/sym/s_map.h>

using namespace stl;

namespace {
    enum eItemType {
        ITEM_TRAIT,
        ITEM_STRUCT,
        ITEM_ENUM,
        ITEM_UNION,
        ITEM_FN,
        ITEM_EXTERN_FN,
        ITEM_STATIC,
        ITEM_TYPE_ALIAS,
    };

    struct CommonFunction: public ExpandDecorator {
        virtual void handle(const ASTAttribute& mi, ASTFunction& fcn) const = 0;

        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;
    };

    struct CHandlerInline: public CommonFunction {
        void handle(const ASTAttribute& mi, ASTFunction& fcn) const override;
    };

    struct CHandlerCold: public CommonFunction {
        void handle(const ASTAttribute& mi, ASTFunction& fcn) const override;
    };

    struct CHandlerRustcAlign: public CommonFunction {
        void handle(const ASTAttribute& mi, ASTFunction& fcn) const override;
    };

    struct CHandlerRustcLegacyConstGenerics: public CommonFunction {
        void handle(const ASTAttribute& mi, ASTFunction& fcn) const override;
    };

    struct CHandlerRepr: public ExpandDecorator {
        AttrStage stage() const override;

        static RcString getReprName(TokenStream& lex);

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerRustcNonnullOptimizationGuaranteed: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerRustcLayoutScalarValidRangeStart: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerRustcLayoutScalarValidRangeEnd: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerLinkName: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerLinkSection: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerLink: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerLinkage: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerTargetFeature: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerRustcIntrinsic: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerTrackCaller: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override;
    };

    struct CHandlerUnsafe: public ExpandDecorator {
        AttrStage stage() const override;

        void handleItem(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, const RcString& name, ASTItem& i) const;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, ASTImpl&, const RcString& name, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, const ASTAbsolutePath& path, ASTTrait&, slice<const ASTAttribute>, ASTItem& i) const override;
    };

    struct DecoratorCrateType: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DecoratorCrateName: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DecoratorRecursionLimit: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DecoratorFeature: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DecoratorAllocator: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorPanicRuntime: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DecoratorNeedsPanicRuntime: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DeriveOpts {
        RcString coreName;
        bool derivesCopy = false;
        Settings::FmtDebug fmtDebug = Settings::FmtDebug::Full;
    };

    struct Deriver {
        virtual ~Deriver() = default;
        virtual const char* traitName() const = 0;
        virtual ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const = 0;
        virtual ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const = 0;

        virtual ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const;

        template <typename F>
        void iterateStructFields(const ASTStruct& str, F cb) const;

        ASTGenericParams getParamsWithBounds(ObjPool& pool, const Span& sp, const ASTGenericParams& p, const ASTPath& traitPath, std::vector<ASTType*> additionalBoundedTypes, bool boundTypeParams = true) const;

        std::vector<ASTType*> getFieldBounds(const ASTStruct& str) const;

        std::vector<ASTType*> getFieldBounds(const ASTEnum& enm) const;

        std::vector<ASTType*> getFieldBounds(const ASTUnion& unn) const;

        void addFieldBoundFromTy(const ASTGenericParams& params, std::vector<ASTType*>& outList, ASTType* ty) const;

        void addFieldBound(std::vector<ASTType*>& outList, ASTType* type) const;
    };

    struct DeriverDebug: public Deriver {
        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const;

        const char* traitName() const override;

        static ASTExprNodeP callPath(ASTPath path, const char* method, std::vector<ASTExprNodeP> args);

        static ASTExprNodeP builderRef();

        static ASTPattern builderPattern(const Span& sp);

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriverInnerCompare: public Deriver {
        virtual ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const = 0;
        virtual ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const = 0;
        virtual ASTExprNodeP equalValue(Span sp, const RcString& coreName) const = 0;
        virtual ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const = 0;
        virtual ASTExprNodeP compareFieldlessEnum(Span sp, const RcString& coreName) const;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriverPartialEq: public DeriverInnerCompare {
        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const override;

        ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const override;

        ASTExprNodeP equalValue(Span sp, const RcString& coreName) const override;

        ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const override;

        ASTExprNodeP compareFieldlessEnum(Span sp, const RcString& coreName) const override;

        const char* traitName() const override;
    };

    struct DeriverPartialOrd: public DeriverInnerCompare {
        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const override;

        ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const override;

        ASTExprNodeP equalValue(Span sp, const RcString& coreName) const override;

        ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const override;

        const char* traitName() const override;
    };

    struct DeriverEq: public Deriver {
        ASTPath getTraitPath(const RcString& coreName) const;

        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const;

        ASTExprNodeP assertIsEq(const ASTPath& methodPath, ASTExprNodeP val) const;

        ASTExprNodeP field(const std::string& name) const;

        ASTExprNodeP field(const RcString& name) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const override;
    };

    struct DeriverOrd: public DeriverInnerCompare {
        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const override;

        ASTExprNodeP compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const override;

        ASTExprNodeP equalValue(Span sp, const RcString& coreName) const override;

        ASTExprNodeP enumMismatch(Span sp, const RcString& coreName) const override;

        const char* traitName() const override;
    };

    struct DeriverClone: public Deriver {
        ASTPath getTraitPath(const RcString& coreName) const;

        ASTPath getMethodPath(const RcString& coreName) const;

        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const;

        ASTExprNodeP cloneValRef(const RcString& coreName, ASTExprNodeP val) const;

        ASTExprNodeP cloneValDirect(const RcString& coreName, ASTExprNodeP val) const;

        ASTExprNodeP field(const RcString& name) const;

        ASTExprNodeP field(const std::string& name) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const override;

        ASTImpl makeCopyClone(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> fieldBounds) const;
    };

    struct DeriverCopy: public Deriver {
        ASTPath getTraitPath(const RcString& coreName) const;

        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const override;
    };

    struct DeriverDefault: public Deriver {
        ASTPath getTraitPath(const RcString& coreName) const;

        ASTPath getMethodPath(ObjPool& pool, const RcString& coreName) const;

        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node, bool boundTypeParams = true) const;

        ASTExprNodeP defaultCall(ObjPool& pool, const RcString& coreName) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriverHash: public Deriver {
        ASTPath getTraitPath(const RcString& coreName) const;

        ASTPath getTraitPathHasher(const RcString& coreName) const;

        ASTPath getMethodPath(const RcString& coreName) const;

        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const;

        ASTExprNodeP hashValRef(const RcString& coreName, ASTExprNodeP val) const;

        ASTExprNodeP hashValDirect(const RcString& coreName, ASTExprNodeP val) const;

        ASTExprNodeP field(const RcString& name) const;

        ASTExprNodeP field(const std::string& name) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        static eCoreType discriminantCoreType(const ASTEnum& enm);

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriverRustcEncodable: public Deriver {
        ASTPath getTraitPath() const;

        ASTPath getTraitPathEncoder() const;

        ASTPath getMethodPath() const;

        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const;

        ASTExprNodeP encValDirect(ASTExprNodeP val) const;

        ASTExprNodeP encValRef(ASTExprNodeP val) const;

        ASTExprNodeP field(const RcString& name) const;

        ASTExprNodeP field(std::string name) const;

        ASTExprNodeP encClosure(ObjPool& pool, Span sp, ASTExprNodeP code) const;

        ASTExprNodeP getValOk(const RcString& coreName) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriverRustcDecodable: public Deriver {
        ASTPath getTraitPath() const;

        ASTPath getTraitPathDecoder() const;

        ASTPath getMethodPath() const;

        ASTImpl makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const;

        ASTExprNodeP decVal() const;

        ASTExprNodeP field(const std::string& name) const;

        ASTExprNodeP decClosure(ObjPool& pool, Span sp, ASTExprNodeP code) const;

        ASTExprNodeP getValErrStr(const RcString& coreName, std::string errStr) const;

        ASTExprNodeP getValOk(const RcString& coreName, ASTExprNodeP inner) const;

        ASTExprNodeP getValOkUnit(const RcString& coreName) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriverConstParamTy: public Deriver {
        ASTImpl handleGeneric(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriverUnsizedConstParamTy: public Deriver {
        ASTImpl handleGeneric(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound) const;

        const char* traitName() const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const override;

        ASTImpl handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const override;
    };

    struct DeriveRegistry {
        DeriverDebug debug;
        DeriverPartialEq partialEq;
        DeriverPartialOrd partialOrd;
        DeriverEq eq;
        DeriverOrd ord;
        DeriverClone clone;
        DeriverCopy copy;
        DeriverDefault default_;
        DeriverHash hash;
        DeriverRustcEncodable rustcEncodable;
        DeriverRustcDecodable rustcDecodable;
        DeriverConstParamTy constParamTy;
        DeriverUnsizedConstParamTy unsizedConstParamTy;

        const Deriver* find(const RcString& traitName) const;
    };

    struct DecoratorDerive: public ExpandDecorator {
        const DeriveRegistry& registry;

        explicit DecoratorDerive(const DeriveRegistry& registry);

        AttrStage stage() const override;

        bool wantsAllAttrs() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorDeriveConst: public DecoratorDerive {
        using DecoratorDerive::DecoratorDerive;
    };

    struct CDocHandler: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const override;
    };

    struct Handler {
        typedef void (*cbT)(const Span& sp, ASTCrate& crate, const std::string&, const ASTAbsolutePath&);
        eItemType type;
        cbT cb;

        Handler(eItemType type, cbT cb);
    };

    struct LangItemRegistry {
        SymbolMap<Handler> handlers;

        explicit LangItemRegistry(ObjPool* pool);

        const Handler* find(const char* name) const;
    };

    struct DecoratorLangItem: public ExpandDecorator {
        const LangItemRegistry& registry;

        explicit DecoratorLangItem(const LangItemRegistry& registry);

        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorMain: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorStart: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorPanicImplementation: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorPanicHandler: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorRustcStdInternalSymbol: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorAllocErrorHandler: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorGlobalAllocator: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute&, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& item) const override;
    };

    struct CMultiHandlerLint: public ExpandDecorator {
        virtual CfgLintLevel level() const = 0;

        AttrStage stage() const override;

        template <typename F>
        static void collectLintNames(const ASTAttribute& mi, const F& cb);

        void recordItemLevel(const ASTAttribute& mi, ASTItem& item) const;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& expr) const override;
    };

    struct CHandlerAllow: public CMultiHandlerLint {
        CfgLintLevel level() const override;
    };

    struct CHandlerWarn: public CMultiHandlerLint {
        CfgLintLevel level() const override;
    };

    struct CHandlerDeny: public CMultiHandlerLint {
        CfgLintLevel level() const override;
    };

    struct CHandlerForbid: public CMultiHandlerLint {
        CfgLintLevel level() const override;
    };

    struct CHandlerMustUse: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;
    };

    struct CHandlerNonExhaustive: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerPath: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerRustcPromotable: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;
    };

    struct CHandlerRustcInheritOverflowChecks: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override;
    };

    struct CHandlerRustcOnUnimiplemented: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CHandlerRustBox: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const override;
    };

    struct CMultiHandlerStability: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const override;
    };

    struct CHandlerStable: public CMultiHandlerStability {};

    struct CHandlerUnstable: public CMultiHandlerStability {};

    struct CHandlerRustcDeprecated: public CMultiHandlerStability {};

    struct CHandlerRustcConstUnstable: public CMultiHandlerStability {};

    struct CHandlerDeprecated: public CMultiHandlerStability {};

    struct CHandlerAllowInternalUnstable: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorNoStd: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DecoratorNoCore: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const override;
    };

    struct DecoratorNoMain: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span&, const ASTAttribute&, const WireBoard&, ASTCrate& crate) const override;
    };

    struct DecoratorNoPrelude: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorPreludeImport: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CTestHandler: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CTestHandlerSP: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct CTestHandlerIgnore: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    ASTType* mktypeSelf(ObjPool& pool, const Span& sp) {
        return mkType(pool, sp, RcString("Self"), 0xFFFF);
    }

    template <typename T>
    static inline std::vector<T> vec$(T v1) {
        std::vector<T> tmp;
        tmp.push_back(mv$(v1));
        return mv$(tmp);
    }

    template <typename T>
    static inline std::vector<T> vec$(T v1, T v2) {
        std::vector<T> tmp;
        tmp.reserve(2);
        tmp.push_back(mv$(v1));
        tmp.push_back(mv$(v2));
        return mv$(tmp);
    }

    template <typename T>
    static inline std::vector<T> vec$(T v1, T v2, T v3) {
        std::vector<T> tmp;
        tmp.reserve(3);
        tmp.push_back(mv$(v1));
        tmp.push_back(mv$(v2));
        tmp.push_back(mv$(v3));
        return mv$(tmp);
    }

    template <typename T>
    static inline std::vector<T> vec$(T v1, T v2, T v3, T v4) {
        std::vector<T> tmp;
        tmp.reserve(4);
        tmp.push_back(mv$(v1));
        tmp.push_back(mv$(v2));
        tmp.push_back(mv$(v3));
        tmp.push_back(mv$(v4));
        return mv$(tmp);
    }

    template <typename T>
    static inline std::vector<T> vec$(T v1, T v2, T v3, T v4, T v5) {
        std::vector<T> tmp;
        tmp.reserve(5);
        tmp.push_back(mv$(v1));
        tmp.push_back(mv$(v2));
        tmp.push_back(mv$(v3));
        tmp.push_back(mv$(v4));
        tmp.push_back(mv$(v5));
        return mv$(tmp);
    }

    static ASTPath getPath(const RcString& coreName, const char* c1, const char* c2) {
        return ASTAbsolutePath(coreName, {RcString::newInterned(c1), RcString::newInterned(c2)});
    }

    static ASTPath getPath(const RcString& coreName, const char* c1, const char* c2, const char* c3) {
        return ASTAbsolutePath(coreName, {RcString::newInterned(c1), RcString::newInterned(c2), RcString::newInterned(c3)});
    }

    static std::unique_ptr<ASTExprNodeBlock> newBlock(const Span& sp) {
        auto rv = std::make_unique<ASTExprNodeBlock>();
        rv->setSpan(sp);
        return rv;
    }

    static inline ASTExprNodeP mkExprnodep(ASTExprNode* en) {
        return ASTExprNodeP(en);
    }

#define NEWNODE(type, ...) mkExprnodep(new ASTExprNode##type(__VA_ARGS__))

    template <typename F>
    static void makeRefpatA(const Span& sp, ASTExprNodeBlock& block, std::vector<ASTPattern>& patsA, const std::vector<ASTTupleItem>& subTypes, F cb) {
        std::vector<ASTExprNodeBlock::Line> nodes;
        for (size_t idx = 0; idx < subTypes.size(); idx++) {
            auto nameA = RcString::newInterned(FMT("a" << idx));
            patsA.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF));
            block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA))));
        }
    }

    template <typename F>
    static void makeRefpatA(const Span& sp, ASTExprNodeBlock& block, std::vector<ASTStructPatternEntry>& patsA, const std::vector<ASTStructItem>& fields, F cb) {
        std::vector<ASTExprNodeBlock::Line> nodes;
        size_t idx = 0;
        for (const auto& fld : fields) {
            auto nameA = RcString::newInterned(FMT("a" << fld.name));
            patsA.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF)});
            block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA))));
            idx++;
        }
    }

    template <typename F>
    static void makeRefpatAb(const Span& sp, ASTExprNodeBlock& block, std::vector<ASTPattern>& patsA, std::vector<ASTPattern>& patsB, const std::vector<ASTTupleItem>& subTypes, F cb) {
        for (size_t idx = 0; idx < subTypes.size(); idx++) {
            auto nameA = RcString::newInterned(FMT("a" << idx));
            auto nameB = RcString::newInterned(FMT("b" << idx));
            patsA.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF));
            patsB.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameB, ASTPatternBinding::Type::REF));
            block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA)), NEWNODE(NamedValue, ASTPath(nameB))));
        }
    }

    template <typename F>
    static void makeRefpatAb(const Span& sp, ASTExprNodeBlock& block, std::vector<ASTStructPatternEntry>& patsA, std::vector<ASTStructPatternEntry>& patsB, const std::vector<ASTStructItem>& fields, F cb) {
        size_t idx = 0;
        for (const auto& fld : fields) {
            auto nameA = RcString::newInterned(FMT("a" << fld.name));
            auto nameB = RcString::newInterned(FMT("b" << fld.name));
            patsA.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF)});
            patsB.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameB, ASTPatternBinding::Type::REF)});
            block.pushStmt(cb(idx, NEWNODE(NamedValue, ASTPath(nameA)), NEWNODE(NamedValue, ASTPath(nameB))));
            idx++;
        }
    }

    static const Deriver* findBuiltinDerive(const DeriveRegistry& registry, const ASTPath& traitPath) {
        if (traitPath.isTrivial()) {
            return registry.find(traitPath.asTrivial());
        }
        if (const auto* path = traitPath.cls.opt_Relative()) {
            if (path->nodes.size() >= 2 && (path->nodes.front().name() == "core" || path->nodes.front().name() == "std")) {
                return registry.find(path->nodes.back().name());
            }
        }
        if (const auto* path = traitPath.cls.opt_Absolute()) {
            if (!path->nodes.empty() && (path->crate == "=core" || path->crate == "=std")) {
                return registry.find(path->nodes.back().name());
            }
        }
        return nullptr;
    }

    std::vector<ASTPath> getDeriveItems(const ASTAttribute& attr) {
        Token tok;
        std::vector<ASTPath> rv;

        TTStream lex(attr.span(), ParseState(), attr.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            if (lex.getTokenIf(TOK_DOUBLE_COLON)) {
                auto item = lex.lookahead(0) == TOK_STRING ? ASTPath(lex.getTokenCheck(TOK_STRING).str().c_str(), {}) : ASTPath((std::string("=") + lex.getTokenCheck(TOK_IDENT).ident().name.c_str()).c_str(), {});
                lex.getTokenCheck(TOK_DOUBLE_COLON);
                do {
                    item += ASTPathNode(lex.getTokenCheck(TOK_IDENT).ident().name);
                } while (lex.getTokenIf(TOK_DOUBLE_COLON));
                rv.push_back(std::move(item));
            } else if (lex.getTokenIf(TOK_INTERPOLATED_TYPE, tok)) {
                const auto& ty = tok.fragType();
                ASSERT_BUG(lex.pointSpan(), ty->isPath(), "TODO: No path :ty in derive, " << ty);
                ASSERT_BUG(lex.pointSpan(), ty->data.as_Path(), "" << ty);
                rv.push_back(*ty->data.as_Path());
            } else if (lex.getTokenIf(TOK_INTERPOLATED_META, tok)) {
                const auto& mi = tok.fragMeta();
                ASSERT_BUG(lex.pointSpan(), !mi.name().elems.empty(), "Empty meta item in derive");
                auto item = ASTPath::newRelative({}, {});
                for (const auto& e : mi.name().elems) {
                    item += ASTPathNode(e);
                }
                rv.push_back(std::move(item));
            } else {
                auto item = ASTPath::newRelative({}, {});
                do {
                    item += ASTPathNode(lex.getTokenCheck(TOK_IDENT).ident().name);
                } while (lex.getTokenIf(TOK_DOUBLE_COLON));
                rv.push_back(std::move(item));
            }

            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }
            lex.getTokenCheck(TOK_COMMA);
        }
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        return rv;
    }

    ASTType* makeType(ObjPool& pool, const Span& sp, const ASTAbsolutePath& path, const ASTGenericParams& params) {
        ASTType* type = mkType(pool, sp, path);
        auto& typesArgs = type->path().nodes().back().args();
        for (const auto& param : params.params) {
            if (const auto* pe = param.opt_Type()) {
                typesArgs.entries.push_back(mkType(pool, ASTTypeTags::Arg(), sp, pe->name()));
            }
            if (const auto* pe = param.opt_Value()) {
                auto p = ASTPath(pe->name().name);
                typesArgs.entries.push_back(ASTExprNodeP(new ASTExprNodeNamedValue(std::move(p))));
            }
        }
        return type;
    }

    bool substituteType(ASTType*& type, const RcString& from, ASTType* to);
    bool substitutePath(ASTPath& path, const RcString& from, ASTType* to);

    bool substitutePathParams(ASTPathParams& params, const RcString& from, ASTType* to) {
        bool changed = false;
        for (auto& param : params.entries) {
            switch (param.tag()) {
                case ASTPathParamEnt::TAG_Null: {
                    break;
                }
                case ASTPathParamEnt::TAG_Lifetime: {
                    break;
                }
                case ASTPathParamEnt::TAG_Type: {
                    auto& e = param.as_Type();
                    changed |= substituteType(e, from, to);
                    break;
                }
                case ASTPathParamEnt::TAG_Value: {
                    break;
                }
                case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                    auto& e = param.as_AssociatedTyEqual();
                    changed |= substitutePathParams(e.first.args(), from, to);
                    changed |= substituteType(e.second, from, to);
                    break;
                }
                case ASTPathParamEnt::TAG_AssociatedValueEqual: {
                    auto& e = param.as_AssociatedValueEqual();
                    changed |= substitutePathParams(e.first.args(), from, to);
                    break;
                }
                case ASTPathParamEnt::TAG_AssociatedTyBound: {
                    auto& e = param.as_AssociatedTyBound();
                    changed |= substitutePathParams(e.first.args(), from, to);
                    for (auto& trait : e.second) {
                        changed |= substitutePath(*trait.path, from, to);
                    }
                    break;
                }
            }
        }
        return changed;
    }

    bool substitutePath(ASTPath& path, const RcString& from, ASTType* to) {
        bool changed = false;
        if (!path.cls.is_Local() && !path.cls.is_Invalid()) {
            for (auto& node : path.nodes()) {
                changed |= substitutePathParams(node.args(), from, to);
            }
        }
        if (auto* ufcs = path.cls.opt_UFCS()) {
            changed |= substituteType(ufcs->type, from, to);
            if (ufcs->trait) {
                changed |= substitutePath(*ufcs->trait, from, to);
            }
        }
        return changed;
    }

    bool substituteType(ASTType*& type, const RcString& from, ASTType* to) {
        if (const auto* generic = type->data.opt_Generic()) {
            if (generic->name == from) {
                type = to->clone();
                return true;
            }
            return false;
        }
        if (const auto* path = type->data.opt_Path()) {
            if ((*path)->isTrivial() && (*path)->asTrivial() == from) {
                type = to->clone();
                return true;
            }
        }

        bool changed = false;
        switch (type->data.tag()) {
            case TypeData::TAG_None: {
                break;
            }
            case TypeData::TAG_Any: {
                break;
            }
            case TypeData::TAG_Bang: {
                break;
            }
            case TypeData::TAG_Unit: {
                break;
            }
            case TypeData::TAG_Macro: {
                break;
            }
            case TypeData::TAG_Primitive: {
                break;
            }
            case TypeData::TAG_Function: {
                auto& e = type->data.as_Function();
                changed |= substituteType(e.info.rettype, from, to);
                for (auto*& arg : e.info.argTypes) {
                    changed |= substituteType(arg, from, to);
                }
                break;
            }
            case TypeData::TAG_Tuple: {
                auto& e = type->data.as_Tuple();
                for (auto*& inner : e.innerTypes) {
                    changed |= substituteType(inner, from, to);
                }
                break;
            }
            case TypeData::TAG_Borrow: {
                auto& e = type->data.as_Borrow();
                changed |= substituteType(e.inner, from, to);
                break;
            }
            case TypeData::TAG_Pointer: {
                auto& e = type->data.as_Pointer();
                changed |= substituteType(e.inner, from, to);
                break;
            }
            case TypeData::TAG_Array: {
                auto& e = type->data.as_Array();
                changed |= substituteType(e.inner, from, to);
                break;
            }
            case TypeData::TAG_Slice: {
                auto& e = type->data.as_Slice();
                changed |= substituteType(e.inner, from, to);
                break;
            }
            case TypeData::TAG_Pattern: {
                auto& e = type->data.as_Pattern();
                changed |= substituteType(e.inner, from, to);
                break;
            }
            case TypeData::TAG_Generic: {
                break;
            }
            case TypeData::TAG_Path: {
                auto& e = type->data.as_Path();
                changed |= substitutePath(*e, from, to);
                break;
            }
            case TypeData::TAG_TraitObject: {
                auto& e = type->data.as_TraitObject();
                for (auto& trait : e.traits) {
                    changed |= substitutePath(*trait.path, from, to);
                }
                break;
            }
            case TypeData::TAG_ErasedType: {
                auto& e = type->data.as_ErasedType();
                for (auto& trait : e->traits) {
                    changed |= substitutePath(*trait.path, from, to);
                }
                for (auto& trait : e->maybeTraits) {
                    changed |= substitutePath(*trait.path, from, to);
                }
                if (e->use) {
                    changed |= substitutePathParams(*e->use, from, to);
                }
                break;
            }
        }
        return changed;
    }

    bool substituteBound(ASTGenericBound& bound, const RcString& from, ASTType* to) {
        bool changed = false;
        switch (bound.tag()) {
            case ASTGenericBound::TAG_None: {
                break;
            }
            case ASTGenericBound::TAG_Lifetime: {
                break;
            }
            case ASTGenericBound::TAG_TypeLifetime: {
                auto& e = bound.as_TypeLifetime();
                changed |= substituteType(e.type, from, to);
                break;
            }
            case ASTGenericBound::TAG_IsTrait: {
                auto& e = bound.as_IsTrait();
                changed |= substituteType(e.type, from, to);
                changed |= substitutePath(e.trait, from, to);
                break;
            }
            case ASTGenericBound::TAG_MaybeTrait: {
                auto& e = bound.as_MaybeTrait();
                changed |= substituteType(e.type, from, to);
                changed |= substitutePath(e.trait, from, to);
                break;
            }
            case ASTGenericBound::TAG_NotTrait: {
                auto& e = bound.as_NotTrait();
                changed |= substituteType(e.type, from, to);
                changed |= substitutePath(e.trait, from, to);
                break;
            }
            case ASTGenericBound::TAG_Equality: {
                auto& e = bound.as_Equality();
                changed |= substituteType(e.type, from, to);
                changed |= substituteType(e.replacement, from, to);
                break;
            }
        }
        return changed;
    }

    ASTGenericParams makeImplParams(ObjPool& pool, const Span& sp, const ASTGenericParams& source) {
        auto params = source.clone();
        for (auto& param : params.params) {
            if (auto* type = param.opt_Type()) {
                type->getDefault() = mkType(pool, sp);
            } else if (auto* value = param.opt_Value()) {
                value->defaultValue() = ASTExpr();
            }
        }
        return params;
    }

    bool isCoercePointee(const ASTPath& traitPath) {
        if (traitPath.isTrivial()) {
            return traitPath.asTrivial() == "CoercePointee";
        }
        if (const auto* path = traitPath.cls.opt_Relative()) {
            return path->nodes.size() == 3 && (path->nodes[0].name() == "core" || path->nodes[0].name() == "std") && path->nodes[1].name() == "marker" && path->nodes[2].name() == "CoercePointee";
        }
        if (const auto* path = traitPath.cls.opt_Absolute()) {
            return (path->crate == "=core" || path->crate == "=std") && path->nodes.size() == 2 && path->nodes[0].name() == "marker" && path->nodes[1].name() == "CoercePointee";
        }
        return false;
    }

    void addCoercePointeeImpl(const Span& sp, ASTModule& mod, ASTPath traitPath, ASTGenericParams params, ASTType* selfType) {
        mod.addItem(sp, ASTVisibility::makeBarePrivate(), "", ASTImpl(ASTImplDef(mv$(params), makeSpanned(sp, mv$(traitPath)), selfType)), {});
    }

    void deriveCoercePointee(const Span& sp, const DeriveOpts& opts, ASTModule& mod, const ASTGenericParams& sourceParams, ASTType* selfType, const ASTStruct& str) {
        bool hasField = false;
        switch (str.data.tag()) {
            case ASTStructData::TAG_Unit: {
                break;
            }
            case ASTStructData::TAG_Struct: {
                auto& e = str.data.as_Struct();
                hasField = !e.ents.empty();
                break;
            }
            case ASTStructData::TAG_Tuple: {
                auto& e = str.data.as_Tuple();
                hasField = !e.ents.empty();
                break;
            }
        }
        if (!hasField) {
            ERROR(sp, E0000, "CoercePointee can only be derived for a struct with fields");
        }

        size_t pointeeIndex = SIZE_MAX;
        size_t typeCount = 0;
        for (size_t i = 0; i < sourceParams.params.size(); i++) {
            if (const auto* type = sourceParams.params[i].opt_Type()) {
                typeCount++;
                if (type->attrs().has("pointee")) {
                    if (pointeeIndex != SIZE_MAX) {
                        ERROR(sp, E0000, "Only one CoercePointee type parameter can have #[pointee]");
                    }
                    pointeeIndex = i;
                }
            }
        }
        if (typeCount == 0) {
            ERROR(sp, E0000, "CoercePointee requires a generic type parameter");
        }
        if (typeCount == 1 && pointeeIndex == SIZE_MAX) {
            for (size_t i = 0; i < sourceParams.params.size(); i++) {
                if (sourceParams.params[i].is_Type()) {
                    pointeeIndex = i;
                    break;
                }
            }
        }
        if (pointeeIndex == SIZE_MAX) {
            ERROR(sp, E0000, "One CoercePointee type parameter must have #[pointee]");
        }

        const auto& pointeeName = sourceParams.params[pointeeIndex].as_Type().name();
        const auto targetName = RcString::newInterned("__S");
        auto* targetParamType = mkType(*selfType->pool, sp, targetName);
        auto* targetSelfType = selfType->clone();
        ASSERT_BUG(sp, substituteType(targetSelfType, pointeeName, targetParamType), "CoercePointee self type does not use " << pointeeName);

        addCoercePointeeImpl(sp, mod, getPath(opts.coreName, "marker", "CoercePointeeValidated"), makeImplParams(*selfType->pool, sp, sourceParams), selfType->clone());

        auto params = makeImplParams(*selfType->pool, sp, sourceParams);
        for (const auto& sourceBound : sourceParams.bounds) {
            auto targetBound = sourceBound.clone();
            if (substituteBound(targetBound, pointeeName, targetParamType)) {
                params.bounds.push_back(mv$(targetBound));
            }
        }
        params.addTyParam(ASTTypeParam(*selfType->pool, sp, ASTAttributeList(), targetName));

        auto unsizePath = getPath(opts.coreName, "marker", "Unsize");
        unsizePath.nodes().back().args().entries.push_back(targetParamType->clone());
        params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*selfType->pool, sp, pointeeName), {}, mv$(unsizePath)}));

        auto dispatchPath = getPath(opts.coreName, "ops", "DispatchFromDyn");
        dispatchPath.nodes().back().args().entries.push_back(targetSelfType->clone());
        addCoercePointeeImpl(sp, mod, mv$(dispatchPath), params.clone(), selfType->clone());

        auto coercePath = getPath(opts.coreName, "ops", "CoerceUnsized");
        coercePath.nodes().back().args().entries.push_back(targetSelfType);
        addCoercePointeeImpl(sp, mod, mv$(coercePath), mv$(params), selfType->clone());
    }

    template <typename T>
    void deriveCoercePointee(const Span& sp, const DeriveOpts&, ASTModule&, const ASTGenericParams&, ASTType*, const T&) {
        ERROR(sp, E0000, "CoercePointee can only be derived for structs");
    }

    std::vector<RcString> findMacro(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod, const ASTPath& traitPath) {
        std::vector<RcString> macPath;

        if (traitPath.isTrivial()) {
            auto macName = traitPath.asTrivial();

            for (const auto& macImport : mod.macroImports) {
                if (macImport.name == macName) {
                    switch (macImport.ref.tag()) {
                        default:
                            break;
                        case MacroRef::TAG_ExternalProcMacro: {
                            auto& pm = macImport.ref.as_ExternalProcMacro();
                            macPath.push_back(pm->path.crateName());
                            macPath.insert(macPath.end(), pm->path.components().begin(), pm->path.components().end());
                            break;
                        }
                    }
                    if (!macPath.empty()) {
                        break;
                    }
                }
            }
        }
        if (macPath.empty()) {
            auto mac = ExpandLookupMacro(sp, wb, crate, LList<const ASTModule*>(nullptr, &mod), traitPath);

            switch (mac.tag()) {
                case MacroRef::TAG_None: {
                    break;
                }
                case MacroRef::TAG_ExternalProcMacro: {
                    auto& extProcMac = mac.as_ExternalProcMacro();
                    macPath.push_back(extProcMac->path.crateName());
                    macPath.insert(macPath.end(), extProcMac->path.components().begin(), extProcMac->path.components().end());
                    break;
                }
                case MacroRef::TAG_BuiltinProcMacro: {
                    TODO(sp, "Handle builtin proc macro");
                    break;
                }
                case MacroRef::TAG_MacroRules: {
                    TODO(sp, "Custom derive using macro_rules?");
                    break;
                }
            }
        }
        return macPath;
    }

    template <typename T>
    static void deriveItem(const DeriveRegistry& registry, const Span& sp, const WireBoard& wb, const ASTCrate& crate, ASTModule& mod, const ASTAttribute& attr, const ASTAbsolutePath& path, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const T& item) {
        auto deriveItems = getDeriveItems(attr);
        if (deriveItems.empty()) {
            return;
        }
        const bool isConstDerive = attr.name() == "derive_const";

        auto type = makeType(*crate.pool, sp, path, item.params());

        DeriveOpts opts = {crate.extCratenameCore};
        opts.fmtDebug = wb.settings->fmtDebug;
        for (const auto& a : attrs) {
            if (a.name() != "derive") {
                continue;
            }
            for (const auto& derived : getDeriveItems(a)) {
                if (!derived.nodes().empty() && derived.nodes().back().name() == "Copy") {
                    opts.derivesCopy = true;
                }
            }
        }

        std::vector<ASTPath> missingHandlers;
        for (const auto& traitPath : deriveItems) {
            if (isCoercePointee(traitPath)) {
                deriveCoercePointee(sp, opts, mod, item.params(), type, item);
                continue;
            }

            if (auto dp = findBuiltinDerive(registry, traitPath)) {
                auto derivedImpl = dp->handleItem(sp, opts, item.params(), type, item);
                if (isConstDerive) {
                    derivedImpl.def().setIsConst();
                    const auto& implTrait = derivedImpl.def().trait().ent;
                    auto& bounds = derivedImpl.def().params().bounds;
                    for (size_t i = item.params().bounds.size(); i < bounds.size(); i++) {
                        if (auto* bound = bounds[i].opt_IsTrait(); bound && bound->trait == implTrait) {
                            bound->constness = ASTBoundConstness::Maybe;
                        }
                    }
                }
                mod.addItem(sp, ASTVisibility::makeBarePrivate(), "", mv$(derivedImpl), {});
                continue;
            }

            std::vector<RcString> macPath = findMacro(sp, wb, crate, mod, traitPath);
            if (!macPath.empty()) {
                auto lex = ProcMacroInvoke(sp, wb, crate, macPath, attrs, vis, path.nodes.back(), item);
                if (lex) {
                    lex->parseState().module = &mod;
                    ParseModRootItems(*lex, mod);
                } else {
                    ERROR(sp, E0000, "proc_macro derive failed");
                }
                continue;
            }

            missingHandlers.push_back(traitPath);
        }

        if (!missingHandlers.empty()) {
            ERROR(sp, E0000, "Failed to apply #[derive] - Missing handlers for " << missingHandlers);
        }
    }

    void handleSave(const Span& sp, ASTCrate& crate, const std::string& name, const ASTAbsolutePath& path) {
        auto rv = crate.langItems.insert(std::make_pair(name, path));
        if (!rv.second) {
            const auto& otherPath = rv.first->second;
            if (path != otherPath) {
                // HACK: Anon modules get visited twice, so can lead to duplicate annotations
                ERROR(sp, E0000, "Duplicate definition of language item '" << name << "' - " << otherPath << " and " << path);
            }
        } else {
        }
    }

    void handleLangItem(const LangItemRegistry& registry, const Span& sp, ASTCrate& crate, const ASTAbsolutePath& path, const std::string& name, eItemType type, ASTItem& item) {
        const char* realName = nullptr;
        if (const auto* handler = registry.find(name.c_str())) {
            if (type != handler->type) {
                ERROR(sp, E0000, "Language item '" << name << "' " << path << " - on incorrect item type " << type << " != " << handler->type);
            }
            handler->cb(sp, crate, name, path);
            return;
        }

        else if (name == "alloc_layout") {
        } else if (name == "panic_info") {
        } else if (name == "panic_location") {
        } else if (name == "manually_drop") {
        }

        else if (name == "arc") {
        } else if (name == "rc") {
        }

        else if (name == "maybe_uninit") {
        }

        else if (name == "unpin") {
        } else if (name == "pin") {
        } else if (name == "future_trait") {
        } else if (name == "from_generator") {
        } else if (name == "get_context") {
        }

        else if (name == "va_list") {
        }

        else if (name == "receiver") {
        } else if (name == "dispatch_from_dyn") {
        }

        else if (name == "generator") {
        } else if (name == "generator_state") {
        }

        else if (name == "Try") {
            realName = "try";
        }

        else if (name == "msvc_try_filter") {
        }

        else if (name == "panic_impl") {
        } else if (name == "oom") {
        }

        else if (name == "panic") {
        } else if (name == "panic_bounds_check") {
        } else if (name == "panic_fmt") {
            item.as_Function().markings.linkName = "rust_begin_unwind";
        } else if (name == "str_eq") {
        } else if (name == "drop_in_place") {
        } else if (name == "align_offset") {
        } else if (name == "begin_panic") {
        } else if (name == "panic_str") {
        } else if (name == "exchange_malloc") {
        } else if (name == "exchange_free") {
        } else if (name == "box_free") {
        } else if (name == "owned_box") {
        } else if (name == "start") {
        }

        else if (name == "eh_personality") {
        } else if (name == "i128_add") {
        } else if (name == "i128_addo") {
        } else if (name == "u128_add") {
        } else if (name == "u128_addo") {
        } else if (name == "i128_sub") {
        } else if (name == "i128_subo") {
        } else if (name == "u128_sub") {
        } else if (name == "u128_subo") {
        } else if (name == "i128_mul") {
        } else if (name == "i128_mulo") {
        } else if (name == "u128_mul") {
        } else if (name == "u128_mulo") {
        } else if (name == "i128_div") {
        } else if (name == "i128_rem") {
        } else if (name == "u128_div") {
        } else if (name == "u128_rem") {
        } else if (name == "i128_shl") {
        } else if (name == "i128_shlo") {
        } else if (name == "u128_shl") {
        } else if (name == "u128_shlo") {
        } else if (name == "i128_shr") {
        } else if (name == "i128_shro") {
        } else if (name == "u128_shr") {
        } else if (name == "u128_shro") {
        }

        else {
            ERROR(sp, E0000, "Unknown language item '" << name << "'");
        }

        if (type == ITEM_EXTERN_FN) {
            // TODO: This should force a specific link name instead
            return;
        }

        auto rv = crate.langItems.insert(std::make_pair(realName == nullptr ? name : realName, path));
        if (!rv.second) {
            const auto& otherPath = rv.first->second;
            if (path != otherPath) {
                // HACK: Anon modules get visited twice, so can lead to duplicate annotations
                ERROR(sp, E0000, "Duplicate definition of language item '" << name << "' - " << otherPath << " and " << path);
            }
        }
    }
}

LangItemRegistry::LangItemRegistry(ObjPool* pool)
    : handlers(pool)
{
    auto add = [&](const char* name, Handler handler) {
        handlers.insert(name, mv$(handler));
    };

    add("phantom_fn", Handler(ITEM_FN, handleSave));
    add("send", Handler(ITEM_TRAIT, handleSave));
    add("sync", Handler(ITEM_TRAIT, handleSave));
    add("sized", Handler(ITEM_TRAIT, handleSave));
    add("copy", Handler(ITEM_TRAIT, handleSave));
    {
        add("clone", Handler(ITEM_TRAIT, handleSave));
    }
    add("drop", Handler(ITEM_TRAIT, handleSave));
    add("add", Handler(ITEM_TRAIT, handleSave));
    add("sub", Handler(ITEM_TRAIT, handleSave));
    add("mul", Handler(ITEM_TRAIT, handleSave));
    add("div", Handler(ITEM_TRAIT, handleSave));
    add("rem", Handler(ITEM_TRAIT, handleSave));

    add("neg", Handler(ITEM_TRAIT, handleSave));
    add("not", Handler(ITEM_TRAIT, handleSave));

    add("bitand", Handler(ITEM_TRAIT, handleSave));
    add("bitor", Handler(ITEM_TRAIT, handleSave));
    add("bitxor", Handler(ITEM_TRAIT, handleSave));
    add("shl", Handler(ITEM_TRAIT, handleSave));
    add("shr", Handler(ITEM_TRAIT, handleSave));

    add("add_assign", Handler(ITEM_TRAIT, handleSave));
    add("sub_assign", Handler(ITEM_TRAIT, handleSave));
    add("div_assign", Handler(ITEM_TRAIT, handleSave));
    add("rem_assign", Handler(ITEM_TRAIT, handleSave));
    add("mul_assign", Handler(ITEM_TRAIT, handleSave));
    add("bitand_assign", Handler(ITEM_TRAIT, handleSave));
    add("bitor_assign", Handler(ITEM_TRAIT, handleSave));
    add("bitxor_assign", Handler(ITEM_TRAIT, handleSave));
    add("shl_assign", Handler(ITEM_TRAIT, handleSave));
    add("shr_assign", Handler(ITEM_TRAIT, handleSave));

    add("index", Handler(ITEM_TRAIT, handleSave));
    add("deref", Handler(ITEM_TRAIT, handleSave));
    add("index_mut", Handler(ITEM_TRAIT, handleSave));
    add("deref_mut", Handler(ITEM_TRAIT, handleSave));
    add("fn", Handler(ITEM_TRAIT, handleSave));
    add("fn_mut", Handler(ITEM_TRAIT, handleSave));
    add("fn_once", Handler(ITEM_TRAIT, handleSave));

    add("eq", Handler(ITEM_TRAIT, handleSave));
    add("ord", Handler(ITEM_TRAIT, handleSave));
    {
        add("partial_ord", Handler(ITEM_TRAIT, handleSave));
    }

    add("unsize", Handler(ITEM_TRAIT, handleSave));
    add("coerce_unsized", Handler(ITEM_TRAIT, handleSave));
    add("freeze", Handler(ITEM_TRAIT, handleSave)); // TODO: What version?

    add("iterator", Handler(ITEM_TRAIT, handleSave));    /* trustme just desugars? */
    add("debug_trait", Handler(ITEM_TRAIT, handleSave)); /* TODO: Poke derive() with this */

    {
        add("termination", Handler(ITEM_TRAIT, handleSave));
    }

    {
        add("pointee_trait", Handler(ITEM_TRAIT, handleSave));
        add("dyn_metadata", Handler(ITEM_STRUCT, handleSave));
        add("structural_peq", Handler(ITEM_TRAIT, handleSave));
        add("structural_teq", Handler(ITEM_TRAIT, handleSave));
        add("discriminant_kind", Handler(ITEM_TRAIT, handleSave));
    }

    add("non_zero", Handler(ITEM_STRUCT, handleSave));
    add("phantom_data", Handler(ITEM_STRUCT, handleSave));
    add("unsafe_cell", Handler(ITEM_STRUCT, handleSave));

    {
        add("RangeFull", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
            handleSave(sp, crate, "range_full", p);
        }));
        add("Range", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
            handleSave(sp, crate, "range", p);
        }));
        add("RangeFrom", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
            handleSave(sp, crate, "range_from", p);
        }));
        add("RangeTo", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
            handleSave(sp, crate, "range_to", p);
        }));
        add("RangeInclusive", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
            handleSave(sp, crate, "range_inclusive", p);
        }));
        add("RangeToInclusive", Handler(ITEM_STRUCT, [](const auto& sp, auto& crate, const auto&, const auto& p) {
            handleSave(sp, crate, "range_to_inclusive", p);
        }));
    }

    {
        add("unwind_safe", Handler(ITEM_TRAIT, handleSave));
        add("ref_unwind_safe", Handler(ITEM_TRAIT, handleSave));
    }
    {
        add("transmute_trait", Handler(ITEM_TRAIT, handleSave));
        add("destruct", Handler(ITEM_TRAIT, handleSave));
        add("tuple_trait", Handler(ITEM_TRAIT, handleSave));
        add("pointer_like", Handler(ITEM_TRAIT, handleSave));
        add("const_param_ty", Handler(ITEM_TRAIT, handleSave));
        add("fn_ptr_trait", Handler(ITEM_TRAIT, handleSave));

        add("transmute_opts", Handler(ITEM_STRUCT, handleSave));
        add("ptr_unique", Handler(ITEM_STRUCT, handleSave));
        add("CStr", Handler(ITEM_STRUCT, handleSave));
        add("String", Handler(ITEM_STRUCT, handleSave));

        add("from_yeet", Handler(ITEM_FN, handleSave));
        add("panic_nounwind", Handler(ITEM_FN, handleSave));
        add("panic_display", Handler(ITEM_FN, handleSave));
        add("panic_bounds_check", Handler(ITEM_FN, handleSave));
        add("panic_misaligned_pointer_dereference", Handler(ITEM_FN, handleSave));
        add("panic_cannot_unwind", Handler(ITEM_FN, handleSave));
        add("panic_in_cleanup", Handler(ITEM_FN, handleSave));
        add("const_panic_fmt", Handler(ITEM_FN, handleSave));

        add("c_void", Handler(ITEM_ENUM, handleSave));
        add("Option", Handler(ITEM_ENUM, handleSave));

        add("format_arguments", Handler(ITEM_STRUCT, handleSave));
        add("format_placeholder", Handler(ITEM_STRUCT, handleSave));
        add("format_argument", Handler(ITEM_STRUCT, handleSave));
        add("format_unsafe_arg", Handler(ITEM_STRUCT, handleSave));
        add("format_alignment", Handler(ITEM_ENUM, handleSave));
        add("format_count", Handler(ITEM_ENUM, handleSave));

        add("ResumeTy", Handler(ITEM_STRUCT, handleSave));
        add("Poll", Handler(ITEM_ENUM, handleSave));
        add("Context", Handler(ITEM_STRUCT, handleSave));
    }
    {
        add("contract_build_check_ensures", Handler(ITEM_FN, handleSave));
        add("contract_check_requires", Handler(ITEM_FN, handleSave));
        add("contract_check_ensures", Handler(ITEM_FN, handleSave));
        add("use_cloned", Handler(ITEM_TRAIT, handleSave));

        add("Ordering", Handler(ITEM_ENUM, handleSave));

        add("meta_sized", Handler(ITEM_TRAIT, handleSave));
        add("pointee_sized", Handler(ITEM_TRAIT, handleSave));
        add("bikeshed_guaranteed_no_drop", Handler(ITEM_TRAIT, handleSave));
        add("unsafe_unpin", Handler(ITEM_TRAIT, handleSave));
        add("unsized_const_param_ty", Handler(ITEM_TRAIT, handleSave));
        add("coerce_pointee_validated", Handler(ITEM_TRAIT, handleSave));
        add("default_trait1", Handler(ITEM_TRAIT, handleSave));
        add("default_trait2", Handler(ITEM_TRAIT, handleSave));
        add("default_trait3", Handler(ITEM_TRAIT, handleSave));
        add("default_trait4", Handler(ITEM_TRAIT, handleSave));

        add("async_fn", Handler(ITEM_TRAIT, handleSave));
        add("async_fn_mut", Handler(ITEM_TRAIT, handleSave));
        add("async_fn_once", Handler(ITEM_TRAIT, handleSave));

        add("async_fn_kind_helper", Handler(ITEM_TRAIT, handleSave));
        add("coroutine_state", Handler(ITEM_ENUM, handleSave));
        add("coroutine", Handler(ITEM_TRAIT, handleSave));
        add("deref_pure", Handler(ITEM_TRAIT, handleSave));
        add("legacy_receiver", Handler(ITEM_TRAIT, handleSave));

        add("type_id", Handler(ITEM_STRUCT, handleSave));

        add("async_iterator", Handler(ITEM_TRAIT, handleSave));
        add("fused_iterator", Handler(ITEM_TRAIT, handleSave));

        add("panic_const_add_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_sub_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_mul_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_div_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_rem_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_neg_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_shr_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_shl_overflow", Handler(ITEM_FN, handleSave));
        add("panic_const_div_by_zero", Handler(ITEM_FN, handleSave));
        add("panic_const_rem_by_zero", Handler(ITEM_FN, handleSave));
        add("panic_const_coroutine_resumed", Handler(ITEM_FN, handleSave));
        add("panic_const_async_fn_resumed", Handler(ITEM_FN, handleSave));
        add("panic_const_async_gen_fn_resumed", Handler(ITEM_FN, handleSave));
        add("panic_const_gen_fn_none", Handler(ITEM_FN, handleSave));
        add("panic_const_coroutine_resumed_panic", Handler(ITEM_FN, handleSave));
        add("panic_const_async_fn_resumed_panic", Handler(ITEM_FN, handleSave));
        add("panic_const_async_gen_fn_resumed_panic", Handler(ITEM_FN, handleSave));
        add("panic_const_gen_fn_none_panic", Handler(ITEM_FN, handleSave));

        add("panic_const_coroutine_resumed_drop", Handler(ITEM_FN, handleSave));
        add("panic_const_async_fn_resumed_drop", Handler(ITEM_FN, handleSave));
        add("panic_const_async_gen_fn_resumed_drop", Handler(ITEM_FN, handleSave));
        add("panic_const_gen_fn_none_drop", Handler(ITEM_FN, handleSave));

        add("panic_null_pointer_dereference", Handler(ITEM_FN, handleSave));
        add("panic_invalid_enum_construction", Handler(ITEM_FN, handleSave));

        add("unsafe_pinned", Handler(ITEM_STRUCT, handleSave));

        add("RangeCopy", Handler(ITEM_STRUCT, handleSave));
        add("RangeInclusiveCopy", Handler(ITEM_STRUCT, handleSave));
        add("RangeFromCopy", Handler(ITEM_STRUCT, handleSave));

        add("async_drop", Handler(ITEM_TRAIT, handleSave));
        add("async_drop_in_place", Handler(ITEM_FN, handleSave));

        add("into_future", Handler(ITEM_FN, handleSave));

        add("global_alloc_ty", Handler(ITEM_STRUCT, handleSave));
    }
}

void RegisterBuiltinDecorators(ExpandRegistry& registry) {
    auto& derives = *registry.make<DeriveRegistry>();
    auto& langItems = *registry.make<LangItemRegistry>(registry.objectPool());
    registry.addDecorator<CHandlerInline>("inline");
    registry.addDecorator<CHandlerCold>("cold");
    registry.addDecorator<CHandlerRustcAlign>("rustc_align");
    registry.addDecorator<CHandlerRustcLegacyConstGenerics>("rustc_legacy_const_generics");
    registry.addDecorator<CHandlerRepr>("repr");
    registry.addDecorator<CHandlerRustcNonnullOptimizationGuaranteed>("rustc_nonnull_optimization_guaranteed");
    registry.addDecorator<CHandlerRustcLayoutScalarValidRangeStart>("rustc_layout_scalar_valid_range_start");
    registry.addDecorator<CHandlerRustcLayoutScalarValidRangeEnd>("rustc_layout_scalar_valid_range_end");
    registry.addDecorator<CHandlerLinkName>("link_name");
    registry.addDecorator<CHandlerLinkSection>("link_section");
    registry.addDecorator<CHandlerLink>("link");
    registry.addDecorator<CHandlerLinkage>("linkage");
    registry.addDecorator<CHandlerTargetFeature>("target_feature");
    registry.addDecorator<CHandlerRustcIntrinsic>("rustc_intrinsic");
    registry.addDecorator<CHandlerTrackCaller>("track_caller");
    registry.addDecorator<CHandlerUnsafe>("unsafe");
    registry.addDecorator<DecoratorRecursionLimit>("recursion_limit");
    registry.addDecorator<DecoratorFeature>("feature");
    registry.addDecorator<DecoratorCrateType>("crate_type");
    registry.addDecorator<DecoratorCrateName>("crate_name");
    registry.addDecorator<DecoratorAllocator>("allocator");
    registry.addDecorator<DecoratorPanicRuntime>("panic_runtime");
    registry.addDecorator<DecoratorNeedsPanicRuntime>("needs_panic_runtime");
    registry.addDecorator<DecoratorDerive>("derive", derives);
    registry.addDecorator<DecoratorDeriveConst>("derive_const", derives);
    registry.addDecorator<CDocHandler>("doc");
    registry.addDecorator<DecoratorLangItem>("lang", langItems);
    registry.addDecorator<DecoratorMain>("main");
    registry.addDecorator<DecoratorStart>("start");
    registry.addDecorator<DecoratorPanicImplementation>("panic_implementation");
    registry.addDecorator<DecoratorPanicHandler>("panic_handler");
    registry.addDecorator<DecoratorRustcStdInternalSymbol>("rustc_std_internal_symbol");
    registry.addDecorator<DecoratorAllocErrorHandler>("alloc_error_handler");
    registry.addDecorator<DecoratorGlobalAllocator>("global_allocator");
    registry.addDecorator<CHandlerAllow>("allow");
    registry.addDecorator<CHandlerWarn>("warn");
    registry.addDecorator<CHandlerDeny>("deny");
    registry.addDecorator<CHandlerForbid>("forbid");
    registry.addDecorator<CHandlerMustUse>("must_use");
    registry.addDecorator<CHandlerNonExhaustive>("non_exhaustive");
    registry.addDecorator<CHandlerPath>("path");
    registry.addDecorator<CHandlerRustcPromotable>("rustc_promotable");
    registry.addDecorator<CHandlerRustcInheritOverflowChecks>("rustc_inherit_overflow_checks");
    registry.addDecorator<CHandlerRustcOnUnimiplemented>("rustc_on_unimplemented");
    registry.addDecorator<CHandlerRustBox>("rustc_box");
    registry.addDecorator<CHandlerStable>("stable");
    registry.addDecorator<CHandlerUnstable>("unstable");
    registry.addDecorator<CHandlerRustcDeprecated>("rustc_deprecated");
    registry.addDecorator<CHandlerRustcConstUnstable>("rustc_const_unstable");
    registry.addDecorator<CHandlerDeprecated>("deprecated");
    registry.addDecorator<CHandlerAllowInternalUnstable>("allow_internal_unstable");
    registry.addDecorator<DecoratorNoStd>("no_std");
    registry.addDecorator<DecoratorNoCore>("no_core");
    registry.addDecorator<DecoratorNoMain>("no_main");
    registry.addDecorator<DecoratorPreludeImport>("prelude_import");
    registry.addDecorator<DecoratorNoPrelude>("no_prelude");
    registry.addDecorator<CTestHandler>("test");
    registry.addDecorator<CTestHandlerSP>("should_panic");
    registry.addDecorator<CTestHandlerIgnore>("ignore");
}

bool ExpandDecorator::runDuringIter() const {
    return false;
}

bool ExpandDecorator::wantsAllAttrs() const {
    return false;
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const {
    unexpected(sp, mi, "crate");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const {
    unexpected(sp, mi, "item");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const {
    unexpected(sp, mi, "associated item");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const {
    unexpected(sp, mi, "trait item");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const {
    unexpected(sp, mi, "struct item");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const {
    unexpected(sp, mi, "tuple item");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const {
    unexpected(sp, mi, "enum variant");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const {
    unexpected(sp, mi, "expression");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const {
    unexpected(sp, mi, "match arm");
}

void ExpandDecorator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& expr) const {
    unexpected(sp, mi, "struct literal ent");
}

auto CommonFunction::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CommonFunction::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
    } else if (i.is_Function()) {
        this->handle(mi, i.as_Function());
    } else {
        // TODO: Error
    }
}

auto CommonFunction::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
    } else if (i.is_Function()) {
        this->handle(mi, i.as_Function());
    } else {
        // TODO: Error
    }
}

auto CommonFunction::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
    if (i.is_None()) {
    } else if (i.is_Function()) {
        this->handle(mi, i.as_Function());
    } else {
        // TODO: Error
    }
}

auto CHandlerInline::handle(const ASTAttribute& mi, ASTFunction& fcn) const -> void {
    TTStream lex(mi.span(), ParseState(), mi.data());
    if (lex.getTokenIf(TOK_PAREN_OPEN)) {
        auto attr = lex.getTokenCheck(TOK_IDENT).ident().name;
        if (attr == "never") {
            fcn.markings.inlineType = ASTFunction::Markings::Inline::Never;
        } else if (attr == "always") {
            fcn.markings.inlineType = ASTFunction::Markings::Inline::Always;
        } else {
            ERROR(lex.pointSpan(), E0000, "Unknown inline type #[inline(" << attr << ")]");
        }
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);
    } else {
        fcn.markings.inlineType = ASTFunction::Markings::Inline::Normal;
    }
}

auto CHandlerCold::handle(const ASTAttribute& mi, ASTFunction& fcn) const -> void {
    TTStream lex(mi.span(), ParseState(), mi.data());
    lex.getTokenCheck(TOK_EOF);
    fcn.markings.isCold = true;
}

auto CHandlerRustcAlign::handle(const ASTAttribute& mi, ASTFunction& fcn) const -> void {
    TTStream lex(mi.span(), ParseState(), mi.data());
    lex.getTokenCheck(TOK_PAREN_OPEN);
    auto value = lex.getTokenCheck(TOK_INTEGER).intval();
    ASSERT_BUG(lex.pointSpan(), value > U128(0), "#[rustc_align(" << value << ")] - alignment must be non-zero");
    ASSERT_BUG(lex.pointSpan(), (value & (value - 1)) == U128(0), "#[rustc_align(" << value << ")] - alignment must be a power of two");
    ASSERT_BUG(lex.pointSpan(), value < U128(UINT64_MAX), "#[rustc_align(" << value << ")] - alignment is too large");
    fcn.markings.alignment = value.truncateU64();
    lex.getTokenCheck(TOK_PAREN_CLOSE);
    lex.getTokenCheck(TOK_EOF);
}

auto CHandlerRustcLegacyConstGenerics::handle(const ASTAttribute& mi, ASTFunction& fcn) const -> void {
    TTStream lex(mi.span(), ParseState(), mi.data());
    lex.getTokenCheck(TOK_PAREN_OPEN);

    auto& list = fcn.markings.rustcLegacyConstGenerics;
    do {
        auto idxRaw = lex.getTokenCheck(TOK_INTEGER).intval();
        ASSERT_BUG(lex.pointSpan(), idxRaw < U128(UINT_MAX), "#[rustc_legacy_const_generics(" << idxRaw << ")] too large");
        auto idx = static_cast<unsigned>(idxRaw.truncateU64());
        ASSERT_BUG(lex.pointSpan(), std::find(list.begin(), list.end(), idx) == list.end(), "#[rustc_legacy_const_generics(" << idx << ")] duplicate index");
        list.push_back(idx);
    } while (lex.getTokenIf(TOK_COMMA));

    lex.getTokenCheck(TOK_PAREN_CLOSE);
    lex.getTokenCheck(TOK_EOF);
}

auto CHandlerRepr::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerRepr::getReprName(TokenStream& lex) -> RcString {
    Token tok;
    if (lex.getTokenIf(TOK_INTERPOLATED_TYPE, tok)) {
        const auto* ty = tok.fragType();
        if (ty && ty->isPath() && ty->path().nodes().size() == 1) {
            return ty->path().nodes().back().name();
        }
        ERROR(lex.pointSpan(), E0000, "#[repr(...)] with a type that does not name a representation");
    }
    return lex.getTokenCheck(TOK_IDENT).ident().name;
}

auto CHandlerRepr::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
    } else if (auto* s = i.opt_Struct()) {
        TTStream lex(sp, ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_PAREN_OPEN);
        if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
            lex.getTokenCheck(TOK_PAREN_CLOSE);
            return;
        }
        do {
            auto reprType = getReprName(lex);
            if (reprType == "C") {
                switch (s->markings.repr) {
                    case ASTStruct::Markings::Repr::Rust:
                        s->markings.repr = ASTStruct::Markings::Repr::C;
                        break;
                    default:
                        // TODO: Error
                        break;
                }
            } else if (reprType == "packed") {
                switch (s->markings.repr) {
                    case ASTStruct::Markings::Repr::C:
                    case ASTStruct::Markings::Repr::Rust:
                        break;
                    default:
                        // TODO: Error
                        break;
                }
                if (s->markings.maxFieldAlign != 0) {
                    // TODO: Error
                }
                if (lex.getTokenIf(TOK_PAREN_OPEN)) {
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<ASTExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(packed(...))] - alignment must be an integer");
                    auto v = val->value;
                    ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(packed(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(packed(" << v << "))] - alignment must be a power of two");
                    ASSERT_BUG(lex.pointSpan(), s->markings.alignValue == 0, "#[repr(packed(" << v << "))] - conflicts with previous alignment");
                    // TODO: I believe this should change the internal aligment too?
                    s->markings.maxFieldAlign = v.truncateU64();
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else {
                    s->markings.maxFieldAlign = 1;
                }
            } else if (reprType == "simd") {
                s->markings.repr = ASTStruct::Markings::Repr::Simd;
            } else if (reprType == "transparent") {
                s->markings.repr = ASTStruct::Markings::Repr::Transparent;
            } else if (reprType == "align") {
                lex.getTokenCheck(TOK_PAREN_OPEN);
                auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                auto* val = cast<ASTExprNodeInteger>(&*n);
                ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                auto v = val->value;
                ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                s->markings.alignValue = std::max(s->markings.alignValue, v.truncateU64());
                lex.getTokenCheck(TOK_PAREN_CLOSE);
            } else if (reprType == "Rust") {
            } else if (reprType == "no_niche") {
                // TODO: rust-lang/rust#68303 happens with UnsafeCell and niche optionisations

            } else {
                TODO(sp, "Handle struct repr '" << reprType << "'");
            }
        } while (lex.getTokenIf(TOK_COMMA));
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);
    } else if (auto* e = i.opt_Enum()) {
        TTStream lex(sp, ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_PAREN_OPEN);

        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            auto setRepr = [&](ASTEnum::Markings::Repr r) {
                ASSERT_BUG(lex.pointSpan(), e->markings.repr == ASTEnum::Markings::Repr::Rust, "Multiple enum reprs set");
                e->markings.repr = r;
            };
            auto reprStr = getReprName(lex);
            if (reprStr == "C") {
                e->markings.isReprC = true;
            } else if (reprStr == "u8") {
                setRepr(ASTEnum::Markings::Repr::U8);
            } else if (reprStr == "u16") {
                setRepr(ASTEnum::Markings::Repr::U16);
            } else if (reprStr == "u32") {
                setRepr(ASTEnum::Markings::Repr::U32);
            } else if (reprStr == "u64") {
                setRepr(ASTEnum::Markings::Repr::U64);
            } else if (reprStr == "u128") {
                setRepr(ASTEnum::Markings::Repr::U128);
            } else if (reprStr == "usize") {
                setRepr(ASTEnum::Markings::Repr::Usize);
            } else if (reprStr == "i8") {
                setRepr(ASTEnum::Markings::Repr::I8);
            } else if (reprStr == "i16") {
                setRepr(ASTEnum::Markings::Repr::I16);
            } else if (reprStr == "i32") {
                setRepr(ASTEnum::Markings::Repr::I32);
            } else if (reprStr == "i64") {
                setRepr(ASTEnum::Markings::Repr::I64);
            } else if (reprStr == "i128") {
                setRepr(ASTEnum::Markings::Repr::I128);
            } else if (reprStr == "isize") {
                setRepr(ASTEnum::Markings::Repr::Isize);
            } else if (reprStr == "align") {
                lex.getTokenCheck(TOK_PAREN_OPEN);
                auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                auto* val = cast<ASTExprNodeInteger>(&*n);
                ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                auto v = val->value;
                ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                e->markings.alignValue = std::max(e->markings.alignValue, v.truncateU64());
                lex.getTokenCheck(TOK_PAREN_CLOSE);
            } else if (reprStr == "Rust") {
            } else if (reprStr == "transparent") {
                ASSERT_BUG(lex.pointSpan(), e->variants().size() == 1, "#[repr(transparent)] needs exactly one variant");
            } else {
                ERROR(lex.pointSpan(), E0000, "Unknown enum repr '" << reprStr << "'");
            }
            if (!lex.getTokenIf(TOK_COMMA)) {
                break;
            }
        }

        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);
    } else if (auto* e = i.opt_Union()) {
        TTStream lex(sp, ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_PAREN_OPEN);

        do {
            auto reprStr = getReprName(lex);
            if (reprStr == "C") {
                e->markings.repr = ASTUnion::Markings::Repr::C;
            } else if (reprStr == "Rust") {
            } else if (reprStr == "transparent") {
                e->markings.repr = ASTUnion::Markings::Repr::Transparent;
            } else if (reprStr == "packed") {
                //    // TODO: Error

                //    // TODO: Error

                if (lex.getTokenIf(TOK_PAREN_OPEN)) {
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    auto* val = cast<ASTExprNodeInteger>(&*n);
                    ASSERT_BUG(n->span(), val, "#[repr(packed(...))] - alignment must be an integer");
                    auto v = val->value;
                    ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(packed(" << v << "))] - alignment must be non-zero");
                    ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(packed(" << v << "))] - alignment must be a power of two");
                    e->markings.maxFieldAlign = v.truncateU64();
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                } else {
                    e->markings.maxFieldAlign = 1;
                }
            } else if (reprStr == "align") {
                lex.getTokenCheck(TOK_PAREN_OPEN);
                auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                auto* val = cast<ASTExprNodeInteger>(&*n);
                ASSERT_BUG(n->span(), val, "#[repr(align(...))] - alignment must be an integer");
                auto v = val->value;
                ASSERT_BUG(lex.pointSpan(), v > U128(0), "#[repr(align(" << v << "))] - alignment must be non-zero");
                ASSERT_BUG(lex.pointSpan(), (v & (v - 1)) == U128(0), "#[repr(align(" << v << "))] - alignment must be a power of two");
                e->markings.alignValue = std::max(e->markings.alignValue, v.truncateU64());
                lex.getTokenCheck(TOK_PAREN_CLOSE);
            } else {
                ERROR(lex.pointSpan(), E0000, "Unknown union repr '" << reprStr << "'");
            }
        } while (lex.getTokenIf(TOK_COMMA));

        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);
    } else {
        ERROR(mi.span(), E0000, "Unexpected attribute #[repr] on " << i.tagStr());
    }
}

auto CHandlerRustcNonnullOptimizationGuaranteed::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerRustcNonnullOptimizationGuaranteed::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: Types only
    if (i.is_Struct()) {
    } else {
    }
}

auto CHandlerRustcLayoutScalarValidRangeStart::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerRustcLayoutScalarValidRangeStart::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: Types only
    if (auto* s = i.opt_Struct()) {
        TTStream lex(sp, ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_PAREN_OPEN);
        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
        auto* np = cast<ASTExprNodeInteger>(n.get());
        ASSERT_BUG(n->span(), np, "#[rustc_layout_scalar_valid_range_start] requires an integer - got " << FMT_CB(ss, n->print(ss)));
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);

        s->markings.scalarValidStartSet = true;
        s->markings.scalarValidStart = np->value;
    } else {
        TODO(sp, "#[rustc_layout_scalar_valid_range_start] on " << i.tagStr());
    }
}

auto CHandlerRustcLayoutScalarValidRangeEnd::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerRustcLayoutScalarValidRangeEnd::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: Types only
    if (auto* s = i.opt_Struct()) {
        TTStream lex(sp, ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_PAREN_OPEN);
        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
        auto* np = cast<ASTExprNodeInteger>(n.get());
        ASSERT_BUG(n->span(), np, "#[rustc_layout_scalar_valid_range_end] requires an integer - got " << FMT_CB(ss, n->print(ss)));
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);
        s->markings.scalarValidEndSet = true;
        s->markings.scalarValidEnd = np->value;
    } else {
        TODO(sp, "#[rustc_layout_scalar_valid_range_end] on " << i.tagStr());
    }
}

auto CHandlerLinkName::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerLinkName::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    auto linkName = mi.parseEqualsString(wb, crate, mod);
    ASSERT_BUG(sp, linkName != "", "Empty #[link_name] attribute");

    if (i.is_None()) {
    } else if (auto* fcn = i.opt_Function()) {
        ASSERT_BUG(sp, fcn->markings.linkName == "", "Duplicate #[link_name] attributes");
        fcn->markings.linkName = linkName;
    } else if (auto* st = i.opt_Static()) {
        ASSERT_BUG(sp, st->sClass() != ASTStatic::CONST, "#[link_name] on `const`");
        ASSERT_BUG(sp, st->markings.linkName == "", "Duplicate #[link_name] attributes");
        st->markings.linkName = linkName;
    } else {
    }
}

auto CHandlerLinkSection::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerLinkSection::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    auto linkSection = mi.parseEqualsString(wb, crate, mod);
    ASSERT_BUG(sp, linkSection != "", "Empty #[link_section] attribute");

    if (i.is_None()) {
    } else if (auto* fcn = i.opt_Function()) {
        ASSERT_BUG(sp, fcn->markings.linkSection == "", "Duplicate #[link_section] attributes");
        fcn->markings.linkSection = linkSection;
    } else if (auto* st = i.opt_Static()) {
        ASSERT_BUG(sp, st->sClass() != ASTStatic::CONST, "#[link_section] on `const`");
        ASSERT_BUG(sp, st->markings.linkSection == "", "Duplicate #[link_section] attributes");
        st->markings.linkSection = linkSection;
    } else {
    }
}

auto CHandlerLink::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerLink::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
    } else if (auto* b = i.opt_ExternBlock()) {
        TTStream lex(sp, ParseState(), mi.data());
        lex.parseState().wb = &wb;
        lex.getTokenCheck(TOK_PAREN_OPEN);
        std::string libName;
        bool emit = true;
        ASTExternBlock::Link link;

        while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
            auto key = lex.getTokenCheck(TOK_IDENT).ident().name;
            if (key == "name") {
                lex.getTokenCheck(TOK_EQUAL);
                auto v = lex.getTokenCheck(TOK_STRING).str();
                if (v == "") {
                    ERROR(sp, E0000, "Empty name on extern block");
                }
                link.libName = v;
            } else if (key == "kind") {
                lex.getTokenCheck(TOK_EQUAL);
                auto v = lex.getTokenCheck(TOK_STRING).str();
                if (v == "") {
                    ERROR(sp, E0000, "Empty `kind` on extern block #[link]");
                }
                // TODO: save and use the kind
            } else if (key == "cfg") {
                emit &= checkCfgStream(*wb.settings, lex);
            } else if (key == "modifiers") {
                lex.getTokenCheck(TOK_EQUAL);
                auto v = lex.getTokenCheck(TOK_STRING).str();
                if (v == "") {
                    ERROR(sp, E0000, "Empty `modifiers` on extern block #[link]");
                }
                // TODO: save and use the `modifiers` value
            } else {
                TODO(sp, "Unknown attribute `#[link(" << key << ")]`");
            }
            if (!lex.getTokenIf(TOK_COMMA)) {
                break;
            }
        }
        if (link.libName == "") {
            ERROR(sp, E0000, "No name in `#[link]`");
        }
        if (emit && wb.settings->linkDirectives) {
            b->libraries.push_back(std::move(link));
        }
        lex.getTokenCheck(TOK_PAREN_CLOSE);
        lex.getTokenCheck(TOK_EOF);
    } else {
        TODO(sp, "#[link] on " << i.tagStr());
    }
}

auto CHandlerLinkage::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerLinkage::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    TTStream lex(sp, ParseState(), mi.data());
    lex.parseState().wb = &wb;
    lex.getTokenCheck(TOK_EQUAL);
    auto tok = lex.getTokenCheck(TOK_STRING);
    auto linkageStr = tok.str();
    lex.getTokenCheck(TOK_EOF);

    auto linkage = ASTLinkage::Default;
    if (linkageStr == "extern_weak") {
        linkage = ASTLinkage::ExternWeak;
    } else if (linkageStr == "weak") {
        linkage = ASTLinkage::Weak;
    } else if (linkageStr == "linkonce" || linkageStr == "linkonce_odr" || linkageStr == "weak_odr") {
        linkage = ASTLinkage::Weak;
    } else if (linkageStr == "external") {
    } else if (linkageStr == "internal" || linkageStr == "private") {
    } else {
        TODO(sp, "#[linkage=\"" << linkageStr << "\"]");
    }

    if (auto* f = i.opt_Function()) {
        switch (linkage) {
            case ASTLinkage::Default:
            case ASTLinkage::Weak:
                break;
            default:
                TODO(sp, "#[linkage=\"" << linkageStr << "\"] on " << i.tagStr());
        }
        f->markings.linkage = linkage;
    } else if (auto* f = i.opt_Static()) {
        switch (linkage) {
            case ASTLinkage::Default:
            case ASTLinkage::Weak:
            case ASTLinkage::ExternWeak:
                break;
            default:
                TODO(sp, "#[linkage=\"" << linkageStr << "\"] on " << i.tagStr());
        }
        f->markings.linkage = linkage;
    } else {
        TODO(sp, "#[linkage] - " << i.tagStr() << " " << path << ": " << mi);
    }
}

auto CHandlerTargetFeature::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerTargetFeature::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: Only valid on functions?
}

auto CHandlerRustcIntrinsic::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto CHandlerRustcIntrinsic::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (auto* e = i.opt_Function()) {
        if (e->abi() != ABI_RUST) {
            ERROR(sp, E0000, "#[rustc_intrinsic] on function with ABI already set (`" << e->abi() << "`)");
        }
        if (!e->code()) {
            e->setAbi("rust-intrinsic");
        }
    } else {
        ERROR(sp, E0000, "#[rustc_intrinsic] on non-function");
    }
}

auto CHandlerTrackCaller::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerTrackCaller::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (/*auto* e =*/i.opt_Function()) {
    } else if (i.opt_Macro()) {
    } else if (const auto* invocation = i.opt_MacroInv(); invocation && invocation->path().isTrivial() && invocation->path().asTrivial() == "macro_rules") {
    } else {
        ERROR(sp, E0000, "#[track_caller] on non-function");
    }
}

auto CHandlerTrackCaller::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (/*auto* e =*/i.opt_Function()) {
    } else {
        ERROR(sp, E0000, "#[track_caller] on non-function");
    }
}

auto CHandlerTrackCaller::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
    if (/*auto* e =*/i.opt_Function()) {
    } else {
        ERROR(sp, E0000, "#[track_caller] on non-function");
    }
}

auto CHandlerTrackCaller::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const -> void {
    if (auto* n = cast<ASTExprNodeClosure>(expr.get())) {
        n->trackCaller = true;
    } else {
        ERROR(sp, E0000, "#[track_caller] on non-function");
    }
}

auto CHandlerUnsafe::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto CHandlerUnsafe::handleItem(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, const RcString& name, ASTItem& i) const -> void {
    TTStream lex(mi.span(), ParseState(), mi.data());
    lex.parseState().wb = &wb;
    lex.getTokenCheck(TOK_PAREN_OPEN);
    while (lex.lookahead(0) != TOK_PAREN_CLOSE) {
        auto ident = lex.getTokenCheck(TOK_IDENT).ident().name;

        if (ident == "no_mangle") {
            if (auto* e = i.opt_Function()) {
                e->markings.linkName = name.c_str();
            } else if (auto* e = i.opt_Static()) {
                e->markings.linkName = name.c_str();
            } else {
                ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tagStr());
            }
        } else if (ident == "export_name") {
            lex.getTokenCheck(TOK_EQUAL);
            auto s = lex.getTokenCheck(TOK_STRING).str();

            if (auto* e = i.opt_Function()) {
                e->markings.linkName = s;
            } else if (auto* e = i.opt_Static()) {
                e->markings.linkName = s;
            } else {
                ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tagStr());
            }
        } else if (ident == "link_section") {
            lex.getTokenCheck(TOK_EQUAL);
            auto s = lex.getTokenCheck(TOK_STRING).str();

            if (auto* e = i.opt_Function()) {
                e->markings.linkSection = s;
            } else if (auto* e = i.opt_Static()) {
                e->markings.linkSection = s;
            } else {
                ERROR(sp, E0000, "#[unsafe(" << ident << ")] on bad item: " << i.tagStr());
            }
        } else if (ident == "ffi_const") {
            if (/*auto* e =*/i.opt_Function()) {
            } else {
                ERROR(sp, E0000, "#[unsafe(" << ident << ")] on non-function");
            }
        } else if (ident == "naked") {
            if (auto* e = i.opt_Function()) {
                e->markings.isNaked = true;
            } else {
                ERROR(sp, E0000, "#[unsafe(" << ident << ")] on non-function");
            }
        } else {
            ERROR(sp, E0000, "Unknown #[unsafe(" << ident << ")]");
        }

        if (lex.lookahead(0) != TOK_COMMA) {
            break;
        }
        lex.getTokenCheck(TOK_COMMA);
    }
    lex.getTokenCheck(TOK_PAREN_CLOSE);
}

auto CHandlerUnsafe::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& i) const -> void {
    handleItem(sp, mi, wb, path.nodes.back(), i);
}

auto CHandlerUnsafe::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, ASTImpl&, const RcString& name, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& i) const -> void {
    handleItem(sp, mi, wb, name, i);
}

auto CHandlerUnsafe::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate&, const ASTAbsolutePath& path, ASTTrait&, slice<const ASTAttribute>, ASTItem& i) const -> void {
    handleItem(sp, mi, wb, path.nodes.back(), i);
}

auto DecoratorCrateType::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorCrateType::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    auto name = mi.parseEqualsString(wb, crate, crate.rootModule_);
    if (name == "rlib" || name == "lib") {
        crate.crateType = ASTCrate::Type::RustLib;
    } else if (name == "dylib" || name == "rdylib") {
        crate.crateType = ASTCrate::Type::RustDylib;
    } else if (name == "cdylib") {
        crate.crateType = ASTCrate::Type::CDylib;
    } else if (name == "proc-macro") {
        crate.crateType = ASTCrate::Type::ProcMacro;
    } else if (name == "bin") {
        crate.crateType = ASTCrate::Type::Executable;
    } else if (name == "staticlib") {
        crate.crateType = ASTCrate::Type::RustLib;
    } else {
        ERROR(sp, E0000, "Unknown crate type '" << name << "'");
    }
}

auto DecoratorCrateName::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorCrateName::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    auto name = mi.parseEqualsString(wb, crate, crate.rootModule_);
    crate.setCrateName(name);
}

auto DecoratorRecursionLimit::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorRecursionLimit::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    const auto text = mi.parseEqualsString(wb, crate, crate.rootModule_);
    char* end = nullptr;
    const auto value = std::strtoul(text.c_str(), &end, 10);
    if (text.empty() || *end != '\0') {
        ERROR(sp, E0000, "#![recursion_limit] needs a number, got `" << text << "`");
    }
    wb.settings->recursionLimit = static_cast<unsigned int>(value);
}

auto DecoratorFeature::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorFeature::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    mi.parseParenIdentList([&](const Span&, RcString feature) {
        crate.features.insert(feature);
    });
}

auto DecoratorAllocator::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorAllocator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    // TODO: Check for an existing allocator crate
    crate.langItems.insert(std::make_pair("trustme-allocator", ASTAbsolutePath()));
}

auto DecoratorAllocator::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (!i.is_Function()) {
        ERROR(sp, E0000, "#[allocator] can only be put on functions and the crate - found on " << i.tagStr());
    }
    // TODO: Ensure that this is an extern { fn }
    // TODO: Does this need to do anything?
}

auto DecoratorPanicRuntime::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorPanicRuntime::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    // TODO: Check for an existing panic_runtime crate
    crate.langItems.insert(std::make_pair("trustme-panic_runtime", ASTAbsolutePath()));
}

auto DecoratorNeedsPanicRuntime::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorNeedsPanicRuntime::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    crate.langItems.insert(std::make_pair("trustme-needs_panic_runtime", ASTAbsolutePath()));
}

auto Deriver::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const -> ASTImpl {
    ERROR(sp, E0000, "Cannot derive(" << traitName() << ") on union");
}

template <typename F>
auto Deriver::iterateStructFields(const ASTStruct& str, F cb) const -> void {
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            for (const auto& fld : e.ents) {
                cb(fld.name);
            }
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                auto fldName = RcString::newInterned(FMT(idx));
                cb(fldName);
            }
            break;
        }
    }
}

auto Deriver::getParamsWithBounds(ObjPool& pool, const Span& sp, const ASTGenericParams& p, const ASTPath& traitPath, std::vector<ASTType*> additionalBoundedTypes, bool boundTypeParams) const -> ASTGenericParams {
    ASTGenericParams params = p.clone();

    // TODO: Get bounds based on generic (or similar) types used within the type.

    unsigned int i = 0;
    for (const auto& arg : params.params) {
        if (const auto* e = arg.opt_Type()) {
            if (boundTypeParams) {
                params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(pool, sp, e->name(), i), {}, traitPath}));
            }
            i++;
        }
    }

    for (auto& ty : additionalBoundedTypes) {
        params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mv$(ty), {}, traitPath}));
    }

    return params;
}

auto Deriver::getFieldBounds(const ASTStruct& str) const -> std::vector<ASTType*> {
    std::vector<ASTType*> ret;
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            for (const auto& fld : e.ents) {
                addFieldBoundFromTy(str.params(), ret, fld.type);
            }
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            for (const auto& ent : e.ents) {
                addFieldBoundFromTy(str.params(), ret, ent.type);
            }
            break;
        }
    }
    return ret;
}

auto Deriver::getFieldBounds(const ASTEnum& enm) const -> std::vector<ASTType*> {
    std::vector<ASTType*> ret;

    for (const auto& v : enm.variants()) {
        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                for (const auto& ent : e.items) {
                    addFieldBoundFromTy(enm.params(), ret, ent.type);
                }
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                for (const auto& fld : e.fields) {
                    addFieldBoundFromTy(enm.params(), ret, fld.type);
                }
                break;
            }
        }
    }

    return ret;
}

auto Deriver::getFieldBounds(const ASTUnion& unn) const -> std::vector<ASTType*> {
    std::vector<ASTType*> ret;
    for (const auto& fld : unn.variants) {
        addFieldBoundFromTy(unn.params(), ret, fld.type);
    }
    return ret;
}

auto Deriver::addFieldBoundFromTy(const ASTGenericParams& params, std::vector<ASTType*>& outList, ASTType* ty) const -> void {
    struct H {
        static void visitNodes(const Deriver& self, const ASTGenericParams& params, std::vector<ASTType*>& outList, const std::vector<ASTPathNode>& nodes) {
            for (const auto& node : nodes) {
                for (const auto& e : node.args().entries) {
                    switch (e.tag()) {
                        default:
                            break;
                        case ASTPathParamEnt::TAG_Type: {
                            auto& ty = e.as_Type();
                            self.addFieldBoundFromTy(params, outList, ty);
                            break;
                        }
                        case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                            auto& aty = e.as_AssociatedTyEqual();
                            self.addFieldBoundFromTy(params, outList, aty.second);
                            break;
                        }
                    }
                }
            }
        }
    };

    // TODO: Locate type that is directly related to the type param.
    switch (ty->data.tag()) {
        case TypeData::TAG_None: {
            break;
        }
        case TypeData::TAG_Any: {
            break;
        }
        case TypeData::TAG_Unit: {
            break;
        }
        case TypeData::TAG_Bang: {
            break;
        }
        case TypeData::TAG_Macro: {
            break;
        }
        case TypeData::TAG_Primitive: {
            break;
        }
        case TypeData::TAG_Function: {
            // TODO? Well... function types don't tend to depend on the trait?
            break;
        }
        case TypeData::TAG_Tuple: {
            auto& e = ty->data.as_Tuple();
            for (const auto& sty : e.innerTypes) {
                addFieldBoundFromTy(params, outList, sty);
            }
            break;
        }
        case TypeData::TAG_Borrow: {
            auto& e = ty->data.as_Borrow();
            addFieldBoundFromTy(params, outList, e.inner);
            break;
        }
        case TypeData::TAG_Pointer: {
            auto& e = ty->data.as_Pointer();
            addFieldBoundFromTy(params, outList, e.inner);
            break;
        }
        case TypeData::TAG_Array: {
            auto& e = ty->data.as_Array();
            addFieldBoundFromTy(params, outList, e.inner);
            break;
        }
        case TypeData::TAG_Slice: {
            auto& e = ty->data.as_Slice();
            addFieldBoundFromTy(params, outList, e.inner);
            break;
        }
        case TypeData::TAG_Pattern: {
            auto& e = ty->data.as_Pattern();
            addFieldBoundFromTy(params, outList, e.inner);
            break;
        }
        case TypeData::TAG_Generic: {
            break;
        }
        case TypeData::TAG_Path: {
            auto& e = ty->data.as_Path();
            switch (e->cls.tag()) {
                case ASTPathClass::TAG_Invalid: {
                    break;
                }
                case ASTPathClass::TAG_Local: {
                    break;
                }
                case ASTPathClass::TAG_Relative: {
                    auto& pe = e->cls.as_Relative();
                    if (pe.nodes.size() > 1) {
                        for (const auto& param : params.params) {
                            if ((param.is_Type() && (param.as_Type().name() == pe.nodes.front().name()))) {
                                addFieldBound(outList, ty);
                                break;
                            }
                        }
                    }
                    H::visitNodes(*this, params, outList, pe.nodes);
                    break;
                }
                case ASTPathClass::TAG_Self: {
                    break;
                }
                case ASTPathClass::TAG_Super: {
                    break;
                }
                case ASTPathClass::TAG_Absolute: {
                    break;
                }
                case ASTPathClass::TAG_UFCS: {
                    break;
                }
            }
            break;
        }
        case TypeData::TAG_TraitObject: {
            // TODO: Should this be recursed?
            break;
        }
        case TypeData::TAG_ErasedType: {
            // TODO: Should this be recursed?
            break;
        }
    }
}

auto Deriver::addFieldBound(std::vector<ASTType*>& outList, ASTType* type) const -> void {
    for (const auto& ty : outList) {
        if (ty == type) {
            return;
        }
    }

    outList.push_back(type->clone());
}

auto DeriverDebug::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath debugTrait = getPath(coreName, "fmt", "Debug");

    ASTFunction fcn(sp, mkType(*type->pool, sp, getPath(coreName, "fmt", "Result")), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("f")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, getPath(coreName, "fmt", "Formatter"))))));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, debugTrait, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, debugTrait), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString("fmt"), mv$(fcn));
    return mv$(rv);
}

auto DeriverDebug::traitName() const -> const char* {
    return "Debug";
}

auto DeriverDebug::callPath(ASTPath path, const char* method, std::vector<ASTExprNodeP> args) -> ASTExprNodeP {
    return NEWNODE(CallPath, path + RcString::newInterned(method), mv$(args));
}

auto DeriverDebug::builderRef() -> ASTExprNodeP {
    return NEWNODE(UniOp, ASTExprNodeUniOp::REFMUT, NEWNODE(NamedValue, ASTPath(RcString::newInterned("s"))));
}

auto DeriverDebug::builderPattern(const Span& sp) -> ASTPattern {
    return ASTPattern(ASTPattern::TagBind(), sp, RcString::newInterned("s"), ASTPatternBinding::Type::MOVE, /*isMut=*/true);
}

auto DeriverDebug::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    std::string name = type->path().nodes().back().name().c_str();
    const ASTPath pathFormatter = getPath(opts.coreName, "fmt", "Formatter");
    const ASTPath pathDebugStruct = getPath(opts.coreName, "fmt", "DebugStruct");
    const ASTPath pathDebugTuple = getPath(opts.coreName, "fmt", "DebugTuple");

    ASTExprNodeP node;
    if (opts.fmtDebug != Settings::FmtDebug::Full) {
        const char* text = opts.fmtDebug == Settings::FmtDebug::Shallow ? name.c_str() : "";
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), callPath(pathFormatter, "write_str", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, std::string(text)))));
    }
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            node = callPath(pathFormatter, "write_str", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, name)));
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            std::vector<ASTExprNodeBlock::Line> nodes;
            nodes.push_back({true, NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_struct", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, name))))});
            for (const auto& fld : e.ents) {
                nodes.push_back({true, callPath(pathDebugStruct, "field", vec$(builderRef(), NEWNODE(String, fld.name.c_str()), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), fld.name)))))});
            }
            nodes.push_back({false, callPath(pathDebugStruct, "finish", vec$(builderRef()))});
            node = NEWNODE(Block, mv$(nodes));
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            std::vector<ASTExprNodeBlock::Line> nodes;
            nodes.push_back({true, NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_tuple", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, name))))});
            for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                nodes.push_back({true, callPath(pathDebugTuple, "field", vec$(builderRef(), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), RcString::newInterned(FMT(idx)))))))});
            }
            nodes.push_back({false, callPath(pathDebugTuple, "finish", vec$(builderRef()))});
            node = NEWNODE(Block, mv$(nodes));
            break;
        }
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
}

auto DeriverDebug::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    ASTPath basePath = *type->data.as_Path();
    basePath.nodes().back() = basePath.nodes().back().name();
    const ASTPath pathFormatter = getPath(opts.coreName, "fmt", "Formatter");
    const ASTPath pathDebugStruct = getPath(opts.coreName, "fmt", "DebugStruct");
    const ASTPath pathDebugTuple = getPath(opts.coreName, "fmt", "DebugTuple");

    if (opts.fmtDebug == Settings::FmtDebug::None) {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), callPath(pathFormatter, "write_str", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, std::string()))));
    }

    std::vector<ASTExprNodeMatchArm> arms;
    for (const auto& v : enm.variants()) {
        ASTExprNodeP code;
        ASTPattern patA;

        ASTPath variantPath = basePath + v.name;

        if (opts.fmtDebug == Settings::FmtDebug::Shallow) {
            code = callPath(pathFormatter, "write_str", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, v.name.c_str())));
            switch (v.data.tag()) {
                case ASTEnumVariantData::TAG_Unit: {
                    patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                    break;
                }
                case ASTEnumVariantData::TAG_Tuple: {
                    patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, ASTPattern::TuplePat{{}, true, {}});
                    break;
                }
                case ASTEnumVariantData::TAG_Struct: {
                    patA = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, {}, false);
                    break;
                }
            }
            std::vector<ASTPattern> pats;
            pats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));
            arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, mv$(code)));
            continue;
        }

        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                code = callPath(pathFormatter, "write_str", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, v.name.c_str())));
                patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                std::vector<ASTPattern> patsA;
                auto block = newBlock(sp);
                block->pushStmt(NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_tuple", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, v.name.c_str())))));

                makeRefpatA(sp, *block, patsA, e.items, [&](size_t idx, auto a) {
                    return callPath(pathDebugTuple, "field", vec$(builderRef(), mv$(a)));
                });
                block->pushTailExpr(callPath(pathDebugTuple, "finish", vec$(builderRef())));
                code = mkExprnodep(block.release());
                patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                std::vector<ASTStructPatternEntry> patsA;
                auto block = newBlock(sp);
                block->pushStmt(NEWNODE(LetBinding, builderPattern(sp), mkType(*type->pool, sp), callPath(pathFormatter, "debug_struct", vec$(NEWNODE(NamedValue, ASTPath(RcString("f"))), NEWNODE(String, v.name.c_str())))));

                makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                    return callPath(pathDebugStruct, "field", vec$(builderRef(), NEWNODE(String, e.fields[idx].name.c_str()), mv$(a)));
                });
                block->pushTailExpr(callPath(pathDebugStruct, "finish", vec$(builderRef())));

                code = mkExprnodep(block.release());
                patA = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                break;
            }
        }

        std::vector<ASTPattern> pats;
        pats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));

        arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, mv$(code)));
    }
    ASTExprNodeP node = NEWNODE(Match, NEWNODE(NamedValue, ASTPath(RcString("self"))), mv$(arms));

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
}

auto DeriverInnerCompare::compareFieldlessEnum(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    return this->enumMismatch(sp, coreName);
}

auto DeriverInnerCompare::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    auto block = newBlock(sp);

    this->iterateStructFields(str, [&](RcString fldName) {
        auto lhs = NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), fldName);
        auto rhs = NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("v"))), fldName);

        if (str.markings.maxFieldAlign != 0) {
            auto lhsBlock = newBlock(sp);
            lhsBlock->pushTailExpr(mv$(lhs));
            lhs = mkExprnodep(lhsBlock.release());

            auto rhsBlock = newBlock(sp);
            rhsBlock->pushTailExpr(mv$(rhs));
            rhs = mkExprnodep(rhsBlock.release());
        }

        block->pushStmt(this->compareAndRet(sp, opts.coreName, mv$(lhs), mv$(rhs)));
    });
    block->pushTailExpr(this->equalValue(sp, opts.coreName));

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
}

auto DeriverInnerCompare::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    bool fieldless = true;
    for (const auto& variant : enm.variants()) {
        if (variant.data.tag() != ASTEnumVariantData::TAG_Unit) {
            fieldless = false;
            break;
        }
    }
    if (fieldless) {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), this->compareFieldlessEnum(sp, opts.coreName));
    }

    ASTPath basePath = *type->data.as_Path();
    basePath.nodes().back().args() = ASTPathParams();
    std::vector<ASTExprNodeMatchArm> arms;

    for (const auto& v : enm.variants()) {
        ASTExprNodeP code;
        ASTPattern patA;
        ASTPattern patB;
        auto variantPath = basePath + v.name;

        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                code = this->equalValue(sp, opts.coreName);
                patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                patB = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                auto block = newBlock(sp);
                std::vector<ASTPattern> patsA;
                std::vector<ASTPattern> patsB;

                makeRefpatAb(sp, *block, patsA, patsB, e.items, [&](auto idx, auto a, auto b) {
                    return this->compareAndRet(sp, opts.coreName, mv$(a), mv$(b));
                });
                block->pushTailExpr(this->equalValue(sp, opts.coreName));

                patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                patB = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsB));
                code = mkExprnodep(block.release());
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                auto block = newBlock(sp);
                std::vector<ASTStructPatternEntry> patsA;
                std::vector<ASTStructPatternEntry> patsB;

                makeRefpatAb(sp, *block, patsA, patsB, e.fields, [&](const auto& name, auto a, auto b) {
                    return this->compareAndRet(sp, opts.coreName, mv$(a), mv$(b));
                });
                block->pushTailExpr(this->equalValue(sp, opts.coreName));

                patA = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                patB = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsB), true);
                code = mkExprnodep(block.release());
                break;
            }
        }

        std::vector<ASTPattern> pats;
        {
            std::vector<ASTPattern> tuplePats;
            tuplePats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));
            tuplePats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patB)));
            pats.push_back(ASTPattern(ASTPattern::TagTuple(), sp, mv$(tuplePats)));
        }

        arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, mv$(code)));
    }

    {
        arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern()), {}, this->enumMismatch(sp, opts.coreName)));
    }

    std::vector<ASTExprNodeP> vals;
    vals.push_back(NEWNODE(NamedValue, ASTPath(RcString("self"))));
    vals.push_back(NEWNODE(NamedValue, ASTPath(RcString("v"))));
    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(Tuple, mv$(vals)), mv$(arms)));
}

auto DeriverPartialEq::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = getPath(coreName, "cmp", "PartialEq");

    ASTFunction fcn(sp, mkType(*type->pool, sp, CORETYPE_BOOL), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("v")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("eq"), mv$(fcn));
    return mv$(rv);
}

auto DeriverPartialEq::compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const -> ASTExprNodeP {
    std::vector<ASTExprNodeIf::Arm> arms;
    arms.push_back(ASTExprNodeIf::Arm{makeVec1(ASTIfLetCondition{{}, NEWNODE(BinOp, ASTExprNodeBinOp::CMPNEQU, mv$(v1), mv$(v2))}), NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(Bool, false))});
    return NEWNODE(If, std::move(arms), nullptr);
}

auto DeriverPartialEq::equalValue(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(Bool, true);
}

auto DeriverPartialEq::enumMismatch(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(Bool, false);
}

auto DeriverPartialEq::compareFieldlessEnum(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    auto discriminant = [&](const RcString& value) {
        return NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(value))));
    };
    return NEWNODE(BinOp, ASTExprNodeBinOp::CMPEQU, discriminant(RcString("self")), discriminant(RcString("v")));
}

auto DeriverPartialEq::traitName() const -> const char* {
    return "PartialEq";
}

auto DeriverPartialOrd::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = getPath(coreName, "cmp", "PartialOrd");
    const ASTPath pathOrdering = getPath(coreName, "cmp", "Ordering");

    ASTPath pathOptionOrdering = getPath(coreName, "option", "Option");
    pathOptionOrdering.nodes().back().args().entries.push_back(mkType(*type->pool, sp, pathOrdering));

    ASTFunction fcn(sp, mkType(*type->pool, sp, pathOptionOrdering), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("v")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("partial_cmp"), mv$(fcn));
    return mv$(rv);
}

auto DeriverPartialOrd::compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const -> ASTExprNodeP {
    return NEWNODE(Match, NEWNODE(CallPath, getPath(coreName, "cmp", "PartialOrd", "partial_cmp"), ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v2)))), ::makeVec3(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagValue(), sp, getPath(coreName, "option", "Option", "None"))), {}, NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(NamedValue, getPath(coreName, "option", "Option", "None")))), ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagNamedTuple(), sp, getPath(coreName, "option", "Option", "Some"), ::makeVec1(ASTPattern(ASTPattern::TagValue(), sp, getPath(coreName, "cmp", "Ordering", "Equal"))))), {}, NEWNODE(Tuple, std::vector<ASTExprNodeP>())), ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagBind(), sp, RcString("res"))), {}, NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(NamedValue, ASTPath(RcString("res")))))));
}

auto DeriverPartialOrd::equalValue(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(CallPath, getPath(coreName, "option", "Option", "Some"), ::makeVec1(NEWNODE(NamedValue, getPath(coreName, "cmp", "Ordering", "Equal"))));
}

auto DeriverPartialOrd::enumMismatch(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(CallPath, getPath(coreName, "cmp", "PartialOrd", "partial_cmp"), ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(RcString("self")))))), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(RcString("v"))))))));
}

auto DeriverPartialOrd::traitName() const -> const char* {
    return "PartialOrd";
}

auto DeriverEq::getTraitPath(const RcString& coreName) const -> ASTPath {
    return getPath(coreName, "cmp", "Eq");
}

auto DeriverEq::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = this->getTraitPath(coreName);

    ASTFunction fcn(sp, mkType(*type->pool, ASTTypeTags::Unit(), sp), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString("assert_receiver_is_total_eq"), mv$(fcn));
    return mv$(rv);
}

auto DeriverEq::assertIsEq(const ASTPath& methodPath, ASTExprNodeP val) const -> ASTExprNodeP {
    return NEWNODE(CallPath, ASTPath(methodPath), vec$(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val))));
}

auto DeriverEq::field(const std::string& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), RcString::newInterned(name));
}

auto DeriverEq::field(const RcString& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), name);
}

auto DeriverEq::traitName() const -> const char* {
    return "Eq";
}

auto DeriverEq::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    const ASTPath assertMethodPath = this->getTraitPath(opts.coreName) + RcString("assert_receiver_is_total_eq");

    auto block = newBlock(sp);
    this->iterateStructFields(str, [&](RcString name) {
        block->pushStmt(this->assertIsEq(assertMethodPath, this->field(name)));
    });

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
}

auto DeriverEq::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    const ASTPath assertMethodPath = this->getTraitPath(opts.coreName) + RcString("assert_receiver_is_total_eq");

    ASTPath basePath = *type->data.as_Path();
    basePath.nodes().back().args() = ASTPathParams();
    std::vector<ASTExprNodeMatchArm> arms;

    for (const auto& v : enm.variants()) {
        ASTExprNodeP code;
        ASTPattern patA;
        auto variantPath = basePath + v.name;

        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                code = NEWNODE(Block);
                patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(variantPath));
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                auto block = newBlock(sp);
                std::vector<ASTPattern> patsA;
                makeRefpatA(sp, *block, patsA, e.items, [&](size_t idx, auto a) {
                    return this->assertIsEq(assertMethodPath, mv$(a));
                });

                patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, variantPath, mv$(patsA));
                code = mkExprnodep(block.release());
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                auto block = newBlock(sp);
                std::vector<ASTStructPatternEntry> patsA;
                makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                    return this->assertIsEq(assertMethodPath, mv$(a));
                });

                patA = ASTPattern(ASTPattern::TagStruct(), sp, variantPath, mv$(patsA), true);
                code = mkExprnodep(block.release());
                break;
            }
        }

        std::vector<ASTPattern> pats;
        pats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));

        arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, mv$(code)));
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, ASTPath(RcString("self"))), mv$(arms)));
}

auto DeriverEq::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const -> ASTImpl {
    const ASTPath assertMethodPath = this->getTraitPath(opts.coreName) + RcString("assert_receiver_is_total_eq");
    auto block = newBlock(sp);

    for (const auto& fld : unn.variants) {
        block->pushStmt(this->assertIsEq(assertMethodPath, this->field(fld.name)));
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(unn), mkExprnodep(block.release()));
}

auto DeriverOrd::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = getPath(coreName, "cmp", "Ord");
    const ASTPath pathOrdering = getPath(coreName, "cmp", "Ordering");

    ASTFunction fcn(sp, mkType(*type->pool, sp, pathOrdering), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("v")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("cmp"), mv$(fcn));
    return mv$(rv);
}

auto DeriverOrd::compareAndRet(Span sp, const RcString& coreName, ASTExprNodeP v1, ASTExprNodeP v2) const -> ASTExprNodeP {
    return NEWNODE(
        Match,
        NEWNODE(
            CallPath,
            getPath(coreName, "cmp", "Ord", "cmp"),
            // TODO: Optional Ref?
            ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v1)), NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(v2)))
        ),
        ::makeVec2(ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagValue(), sp, getPath(coreName, "cmp", "Ordering", "Equal"))), {}, NEWNODE(Tuple, std::vector<ASTExprNodeP>())), ASTExprNodeMatchArm(::makeVec1(ASTPattern(ASTPattern::TagBind(), sp, RcString("res"))), {}, NEWNODE(Flow, ASTExprNodeFlow::RETURN, "", NEWNODE(NamedValue, ASTPath(RcString("res"))))))
    );
}

auto DeriverOrd::equalValue(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(NamedValue, getPath(coreName, "cmp", "Ordering", "Equal"));
}

auto DeriverOrd::enumMismatch(Span sp, const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(CallPath, getPath(coreName, "cmp", "Ord", "cmp"), ::makeVec2(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(RcString("self")))))), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(CallPath, getPath(coreName, "intrinsics", "discriminant_value"), makeVec1(NEWNODE(NamedValue, ASTPath(RcString("v"))))))));
}

auto DeriverOrd::traitName() const -> const char* {
    return "Ord";
}

auto DeriverClone::getTraitPath(const RcString& coreName) const -> ASTPath {
    return ASTPath(coreName, {ASTPathNode(RcString("clone"), {}), ASTPathNode(RcString("Clone"), {})});
}

auto DeriverClone::getMethodPath(const RcString& coreName) const -> ASTPath {
    return getTraitPath(coreName) + RcString("clone");
}

auto DeriverClone::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = this->getTraitPath(coreName);

    ASTFunction fcn(sp, mktypeSelf(*type->pool, sp), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp)))));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString("clone"), mv$(fcn));
    return mv$(rv);
}

auto DeriverClone::cloneValRef(const RcString& coreName, ASTExprNodeP val) const -> ASTExprNodeP {
    // TODO: Hack for zero-sized arrays? (Not a 1.19 feature)
    return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val))));
}

auto DeriverClone::cloneValDirect(const RcString& coreName, ASTExprNodeP val) const -> ASTExprNodeP {
    return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(mv$(val)));
}

auto DeriverClone::field(const RcString& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), name);
}

auto DeriverClone::field(const std::string& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), RcString::newInterned(name));
}

auto DeriverClone::traitName() const -> const char* {
    return "Clone";
}

auto DeriverClone::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    const ASTPath& tyPath = *type->data.as_Path();

    if (opts.derivesCopy && p.params.empty()) {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, NEWNODE(Deref, NEWNODE(NamedValue, ASTPath(RcString("self"))))));
    }

    ASTExprNodeP node;
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            node = NEWNODE(NamedValue, ASTPath(tyPath));
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            ASTExprNodeStructLiteral::tValues vals;
            for (const auto& fld : e.ents) {
                vals.push_back({{}, fld.name, this->cloneValRef(opts.coreName, this->field(fld.name))});
            }
            node = NEWNODE(StructLiteral, tyPath, nullptr, mv$(vals));
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            std::vector<ASTExprNodeP> vals;
            for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                vals.push_back(this->cloneValRef(opts.coreName, this->field(FMT(idx))));
            }
            node = NEWNODE(CallPath, ASTPath(tyPath), mv$(vals));
            break;
        }
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, mv$(node)));
}

auto DeriverClone::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    if (opts.derivesCopy && p.params.empty()) {
        return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Block, NEWNODE(Deref, NEWNODE(NamedValue, ASTPath(RcString("self"))))));
    }
    ASTPath basePath = *type->data.as_Path();
    basePath.nodes().back().args() = ASTPathParams();
    std::vector<ASTExprNodeMatchArm> arms;

    for (const auto& v : enm.variants()) {
        ASTExprNodeP code;
        ASTPattern patA;

        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                code = NEWNODE(NamedValue, basePath + v.name);
                patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(basePath + v.name));
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                std::vector<ASTPattern> patsA;
                std::vector<ASTExprNodeP> nodes;

                for (size_t idx = 0; idx < e.items.size(); idx++) {
                    auto nameA = RcString::newInterned(FMT("a" << idx));
                    patsA.push_back(ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF));
                    nodes.push_back(this->cloneValDirect(opts.coreName, NEWNODE(NamedValue, ASTPath(nameA))));
                }

                patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, basePath + v.name, mv$(patsA));
                code = NEWNODE(CallPath, basePath + v.name, mv$(nodes));
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                std::vector<ASTStructPatternEntry> patsA;
                ASTExprNodeStructLiteral::tValues vals;

                for (const auto& fld : e.fields) {
                    auto nameA = RcString::newInterned(FMT("a" << fld.name));
                    patsA.push_back(ASTStructPatternEntry{ASTAttributeList(), fld.name, ASTPattern(ASTPattern::TagBind(), sp, nameA, ASTPatternBinding::Type::REF)});
                    vals.push_back({{}, fld.name, this->cloneValDirect(opts.coreName, NEWNODE(NamedValue, ASTPath(nameA)))});
                }

                patA = ASTPattern(ASTPattern::TagStruct(), sp, basePath + v.name, mv$(patsA), true);
                code = NEWNODE(StructLiteral, basePath + v.name, nullptr, mv$(vals));
                break;
            }
        }

        std::vector<ASTPattern> pats;
        pats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));

        arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, mv$(code)));
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, ASTPath(RcString("self"))), mv$(arms)));
}

auto DeriverClone::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const -> ASTImpl {
    return makeCopyClone(sp, opts, p, type, this->getFieldBounds(unn));
}

auto DeriverClone::makeCopyClone(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> fieldBounds) const -> ASTImpl {
    auto ret = this->makeRet(sp, opts.coreName, p, type, std::move(fieldBounds), NEWNODE(Deref, NEWNODE(NamedValue, ASTPath(RcString("self")))));

    // TODO: What if the type is only conditionally copy? (generic over something)

    // TODO: Are these bounds needed?
    for (auto& b : ret.def().params().bounds) {
        auto& be = b.as_IsTrait();
        be.trait = getPath(opts.coreName, "marker", "Copy");
    }

    return ret;
}

auto DeriverCopy::getTraitPath(const RcString& coreName) const -> ASTPath {
    return getPath(coreName, "marker", "Copy");
}

auto DeriverCopy::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = this->getTraitPath(coreName);

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    return mv$(rv);
}

auto DeriverCopy::traitName() const -> const char* {
    return "Copy";
}

auto DeriverCopy::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), nullptr);
}

auto DeriverCopy::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), nullptr);
}

auto DeriverCopy::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTUnion& unn) const -> ASTImpl {
    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(unn), nullptr);
}

auto DeriverDefault::getTraitPath(const RcString& coreName) const -> ASTPath {
    return getPath(coreName, "default", "Default");
}

auto DeriverDefault::getMethodPath(ObjPool& pool, const RcString& coreName) const -> ASTPath {
    return ASTPath::newUfcsTrait(::mkType(pool, Span()), getTraitPath(coreName), {ASTPathNode(RcString::newInterned("default"), {})});
}

auto DeriverDefault::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node, bool boundTypeParams) const -> ASTImpl {
    const ASTPath traitPath = this->getTraitPath(coreName);

    ASTFunction fcn(sp, mktypeSelf(*type->pool, sp), {});
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound), boundTypeParams);

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("default"), mv$(fcn));
    return mv$(rv);
}

auto DeriverDefault::defaultCall(ObjPool& pool, const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(CallPath, this->getMethodPath(pool, coreName), {});
}

auto DeriverDefault::traitName() const -> const char* {
    return "Default";
}

auto DeriverDefault::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    const ASTPath& tyPath = *type->data.as_Path();
    ASTExprNodeP node;

    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            node = NEWNODE(NamedValue, ASTPath(tyPath));
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            ASTExprNodeStructLiteral::tValues vals;
            bool hasDefault = false;
            for (const auto& fld : e.ents) {
                if (fld.defaultValue) {
                    hasDefault = true;
                } else {
                    vals.push_back({{}, fld.name, this->defaultCall(*type->pool, opts.coreName)});
                }
            }
            if (hasDefault) {
                node = NEWNODE(StructLiteralPattern, tyPath, mv$(vals));
            } else {
                node = NEWNODE(StructLiteral, tyPath, nullptr, mv$(vals));
            }
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            std::vector<ASTExprNodeP> vals;
            for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                vals.push_back(this->defaultCall(*type->pool, opts.coreName));
            }
            node = NEWNODE(CallPath, ASTPath(tyPath), mv$(vals));
            break;
        }
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), NEWNODE(Block, mv$(node)));
}

auto DeriverDefault::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    const ASTEnumVariant* defaultVar = nullptr;
    for (const auto& v : enm.variants()) {
        if (v.attrs.has("default")) {
            if (defaultVar) {
                ERROR(sp, E0000, "Multiple #[default] attributes");
            }
            defaultVar = &v;
        }
    }
    if (!defaultVar) {
        ERROR(sp, E0000, "No #[default] attribute on enum with derive(Default)");
    }

    ASTPath varPath = *type->data.as_Path() + ASTPathNode(defaultVar->name);

    std::vector<ASTType*> boundTys;
    ASTExprNodeP node;
    switch (defaultVar->data.tag()) {
        case ASTEnumVariantData::TAG_Unit: {
            node = NEWNODE(NamedValue, std::move(varPath));
            break;
        }
        case ASTEnumVariantData::TAG_Tuple: {
            auto& e = defaultVar->data.as_Tuple();
            std::vector<ASTExprNodeP> vals;
            for (const auto& fld : e.items) {
                addFieldBoundFromTy(enm.params(), boundTys, fld.type);
                vals.push_back(this->defaultCall(*type->pool, opts.coreName));
            }
            node = NEWNODE(CallPath, std::move(varPath), mv$(vals));
            break;
        }
        case ASTEnumVariantData::TAG_Struct: {
            auto& e = defaultVar->data.as_Struct();
            ASTExprNodeStructLiteral::tValues vals;
            for (const auto& fld : e.fields) {
                if (fld.defaultValue) {
                } else {
                    addFieldBoundFromTy(enm.params(), boundTys, fld.type);
                    vals.push_back({{}, fld.name, this->defaultCall(*type->pool, opts.coreName)});
                }
            }
            node = NEWNODE(StructLiteralPattern, std::move(varPath), mv$(vals));
            break;
        }
    }
    return this->makeRet(sp, opts.coreName, p, type, std::move(boundTys), std::move(node), /*boundTypeParams=*/false);
}

auto DeriverHash::getTraitPath(const RcString& coreName) const -> ASTPath {
    return getPath(coreName, "hash", "Hash");
}

auto DeriverHash::getTraitPathHasher(const RcString& coreName) const -> ASTPath {
    return getPath(coreName, "hash", "Hasher");
}

auto DeriverHash::getMethodPath(const RcString& coreName) const -> ASTPath {
    return getTraitPath(coreName) + RcString::newInterned("hash");
}

auto DeriverHash::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = this->getTraitPath(coreName);

    ASTFunction fcn(sp, mkType(*type->pool, ASTTypeTags::Unit(), sp), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("state")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, RcString("#H"), 0x100 | 0)))));
    fcn.params().addTyParam(ASTTypeParam(*type->pool, sp, {}, RcString("#H")));
    fcn.params().addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, RcString("#H"), 0x100 | 0), {}, this->getTraitPathHasher(coreName)}));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, RcString::newInterned("hash"), mv$(fcn));
    return mv$(rv);
}

auto DeriverHash::hashValRef(const RcString& coreName, ASTExprNodeP val) const -> ASTExprNodeP {
    return this->hashValDirect(coreName, NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val)));
}

auto DeriverHash::hashValDirect(const RcString& coreName, ASTExprNodeP val) const -> ASTExprNodeP {
    return NEWNODE(CallPath, this->getMethodPath(coreName), vec$(mv$(val), NEWNODE(NamedValue, ASTPath(RcString("state")))));
}

auto DeriverHash::field(const RcString& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), name);
}

auto DeriverHash::field(const std::string& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), RcString::newInterned(name));
}

auto DeriverHash::traitName() const -> const char* {
    return "Hash";
}

auto DeriverHash::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    auto block = newBlock(sp);

    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            for (const auto& fld : e.ents) {
                block->pushStmt(this->hashValRef(opts.coreName, this->field(fld.name)));
            }
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                block->pushStmt(this->hashValRef(opts.coreName, this->field(FMT(idx))));
            }
            break;
        }
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mkExprnodep(block.release()));
}

auto DeriverHash::discriminantCoreType(const ASTEnum& enm) -> eCoreType {
    switch (enm.markings.repr) {
        case ASTEnum::Markings::Repr::Rust:
            return CORETYPE_INT;
        case ASTEnum::Markings::Repr::U8:
            return CORETYPE_U8;
        case ASTEnum::Markings::Repr::U16:
            return CORETYPE_U16;
        case ASTEnum::Markings::Repr::U32:
            return CORETYPE_U32;
        case ASTEnum::Markings::Repr::U64:
            return CORETYPE_U64;
        case ASTEnum::Markings::Repr::U128:
            return CORETYPE_U128;
        case ASTEnum::Markings::Repr::Usize:
            return CORETYPE_UINT;
        case ASTEnum::Markings::Repr::I8:
            return CORETYPE_I8;
        case ASTEnum::Markings::Repr::I16:
            return CORETYPE_I16;
        case ASTEnum::Markings::Repr::I32:
            return CORETYPE_I32;
        case ASTEnum::Markings::Repr::I64:
            return CORETYPE_I64;
        case ASTEnum::Markings::Repr::I128:
            return CORETYPE_I128;
        case ASTEnum::Markings::Repr::Isize:
            return CORETYPE_INT;
    }
    return CORETYPE_INT;
}

auto DeriverHash::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    ASTPath basePath = *type->data.as_Path();
    basePath.nodes().back().args() = ASTPathParams();
    std::vector<ASTExprNodeMatchArm> arms;

    for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
        const auto& v = enm.variants()[varIdx];
        ASTPattern patA;

        auto varPath = basePath + v.name;
        auto varIdxHash = enm.variants().size() > 1 ? this->hashValRef(opts.coreName, NEWNODE(Integer, U128(varIdx), discriminantCoreType(enm))) : NEWNODE(Tuple, {});

        auto block = newBlock(sp);
        block->pushStmt(mv$(varIdxHash));
        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(varPath));
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                std::vector<ASTPattern> patsA;
                makeRefpatA(sp, *block, patsA, e.items, [&](size_t, auto a) {
                    return this->hashValDirect(opts.coreName, mv$(a));
                });
                patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, varPath, mv$(patsA));
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                std::vector<ASTStructPatternEntry> patsA;
                makeRefpatA(sp, *block, patsA, e.fields, [&](size_t, auto a) {
                    return this->hashValDirect(opts.coreName, mv$(a));
                });
                patA = ASTPattern(ASTPattern::TagStruct(), sp, varPath, mv$(patsA), true);
                break;
            }
        }

        std::vector<ASTPattern> pats;
        pats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));

        arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, mkExprnodep(block.release())));
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), NEWNODE(Match, NEWNODE(NamedValue, ASTPath(RcString("self"))), mv$(arms)));
}

auto DeriverRustcEncodable::getTraitPath() const -> ASTPath {
    return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Encodable"), {})});
}

auto DeriverRustcEncodable::getTraitPathEncoder() const -> ASTPath {
    return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Encoder"), {})});
}

auto DeriverRustcEncodable::getMethodPath() const -> ASTPath {
    return getTraitPath() + "encode";
}

auto DeriverRustcEncodable::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = this->getTraitPath();

    ASTPath resultPath = getPath(coreName, "result", "Result");
    resultPath.nodes()[1].args().entries.push_back(mkType(*type->pool, ASTTypeTags::Unit(), sp));
    resultPath.nodes()[1].args().entries.push_back(mkType(*type->pool, sp, ASTPath::newUfcsTrait(mkType(*type->pool, sp, "S", 0x100 | 0), this->getTraitPathEncoder(), {ASTPathNode("Error", {})})));

    ASTFunction fcn(sp, mkType(*type->pool, sp, mv$(resultPath)), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("self")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), false, mktypeSelf(*type->pool, sp))), ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, RcString("s")), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, RcString::newInterned("S"), 0x100 | 0)))));
    fcn.params().addTyParam(ASTTypeParam(*type->pool, sp, {}, "S"));
    fcn.params().addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, "S", 0x100 | 0), {}, this->getTraitPathEncoder()}));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, "encode", mv$(fcn));
    return mv$(rv);
}

auto DeriverRustcEncodable::encValDirect(ASTExprNodeP val) const -> ASTExprNodeP {
    return NEWNODE(CallPath, this->getMethodPath(), vec$(mv$(val), NEWNODE(NamedValue, ASTPath(RcString("s")))));
}

auto DeriverRustcEncodable::encValRef(ASTExprNodeP val) const -> ASTExprNodeP {
    return this->encValDirect(NEWNODE(UniOp, ASTExprNodeUniOp::REF, mv$(val)));
}

auto DeriverRustcEncodable::field(const RcString& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), name);
}

auto DeriverRustcEncodable::field(std::string name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), RcString::newInterned(name));
}

auto DeriverRustcEncodable::encClosure(ObjPool& pool, Span sp, ASTExprNodeP code) const -> ASTExprNodeP {
    return NEWNODE(Closure, vec$(std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, RcString("s")), ::mkType(pool, sp))), ::mkType(pool, sp), mv$(code), false, false, false);
}

auto DeriverRustcEncodable::getValOk(const RcString& coreName) const -> ASTExprNodeP {
    return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Ok"), vec$(NEWNODE(Tuple, {})));
}

auto DeriverRustcEncodable::traitName() const -> const char* {
    return "RustcEncodable";
}

auto DeriverRustcEncodable::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    std::string structName = type->data.as_Path()->nodes().back().name().c_str();

    auto block = newBlock(sp);
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            unsigned int idx = 0;
            for (const auto& fld : e.ents) {
                block->pushStmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct_field", vec$(NEWNODE(NamedValue, ASTPath(RcString("s"))), NEWNODE(String, fld.name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValRef(this->field(fld.name))))));
                idx++;
            }
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                block->pushStmt(NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct_arg", vec$(NEWNODE(NamedValue, ASTPath(RcString("s"))), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValRef(this->field(FMT(idx)))))));
            }
            break;
        }
    }

    block->pushTailExpr(this->getValOk(opts.coreName));
    auto closure = this->encClosure(*type->pool, sp, mkExprnodep(block.release()));

    ASTExprNodeP node;
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            node = getValOk(opts.coreName);
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_struct", vec$(NEWNODE(NamedValue, ASTPath(RcString("s"))), NEWNODE(String, structName), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_tuple_struct", vec$(NEWNODE(NamedValue, ASTPath(RcString("s"))), NEWNODE(String, structName), NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT), mv$(closure)));
            break;
        }
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
}

auto DeriverRustcEncodable::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    ASTPath basePath = *type->data.as_Path();
    basePath.nodes().back().args() = ASTPathParams();
    std::vector<ASTExprNodeMatchArm> arms;

    auto sEnt = NEWNODE(NamedValue, ASTPath(RcString("s")));

    for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
        const auto& v = enm.variants()[varIdx];
        ASTExprNodeP code;
        ASTPattern patA;

        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(sEnt->clone(), NEWNODE(String, v.name.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(0), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->getValOk(opts.coreName))));
                patA = ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Named(basePath + v.name));
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                auto block = newBlock(sp);
                std::vector<ASTPattern> patsA;
                makeRefpatA(sp, *block, patsA, e.items, [&](size_t idx, auto a) {
                    return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::newInterned("emit_enum_variant_arg"), vec$(sEnt->clone(), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValDirect(mv$(a)))));
                });
                block->pushTailExpr(this->getValOk(opts.coreName));

                code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_variant", vec$(sEnt->clone(), NEWNODE(String, v.name.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(e.items.size()), CORETYPE_UINT), this->encClosure(*type->pool, sp, mkExprnodep(block.release()))));
                patA = ASTPattern(ASTPattern::TagNamedTuple(), sp, basePath + v.name, mv$(patsA));
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                auto block = newBlock(sp);
                std::vector<ASTStructPatternEntry> patsA;
                makeRefpatA(sp, *block, patsA, e.fields, [&](size_t idx, auto a) {
                    return NEWNODE(CallPath, this->getTraitPathEncoder() + RcString::newInterned("emit_enum_struct_variant_field"), vec$(sEnt->clone(), NEWNODE(String, e.fields[idx].name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->encClosure(*type->pool, sp, this->encValDirect(mv$(a)))));
                });
                block->pushTailExpr(this->getValOk(opts.coreName));

                patA = ASTPattern(ASTPattern::TagStruct(), sp, basePath + v.name, mv$(patsA), true);
                code = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum_struct_variant", vec$(sEnt->clone(), NEWNODE(String, v.name.c_str()), NEWNODE(Integer, U128(varIdx), CORETYPE_UINT), NEWNODE(Integer, U128(e.fields.size()), CORETYPE_UINT), this->encClosure(*type->pool, sp, mkExprnodep(block.release()))));
                break;
            }
        }

        std::vector<ASTPattern> pats;
        pats.push_back(ASTPattern(ASTPattern::TagReference(), sp, false, mv$(patA)));

        arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, mv$(code)));
    }

    auto nodeMatch = NEWNODE(Match, NEWNODE(NamedValue, ASTPath(RcString("self"))), mv$(arms));

    std::string enumName = type->data.as_Path()->nodes().back().name().c_str();
    auto node = NEWNODE(CallPath, this->getTraitPathEncoder() + "emit_enum", vec$(mv$(sEnt), NEWNODE(String, enumName), this->encClosure(*type->pool, sp, mv$(nodeMatch))));

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
}

auto DeriverRustcDecodable::getTraitPath() const -> ASTPath {
    return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Decodable"), {})});
}

auto DeriverRustcDecodable::getTraitPathDecoder() const -> ASTPath {
    return ASTPath(RcString::newInterned("=rustc_serialize"), {ASTPathNode(RcString::newInterned("Decoder"), {})});
}

auto DeriverRustcDecodable::getMethodPath() const -> ASTPath {
    return getTraitPath() + "decode";
}

auto DeriverRustcDecodable::makeRet(Span sp, const RcString& coreName, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound, ASTExprNodeP node) const -> ASTImpl {
    const ASTPath traitPath = this->getTraitPath();

    ASTPath resultPath = getPath(coreName, "result", "Result");
    resultPath.nodes()[1].args().entries.push_back(mktypeSelf(*type->pool, sp));
    resultPath.nodes()[1].args().entries.push_back(mkType(*type->pool, sp, ASTPath::newUfcsTrait(mkType(*type->pool, sp, "D", 0x100 | 0), this->getTraitPathDecoder(), {ASTPathNode("Error", {})})));

    ASTFunction fcn(sp, mkType(*type->pool, sp, resultPath), vec$(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, "d"), mkType(*type->pool, ASTTypeTags::Reference(), sp, ASTLifetimeRef(), true, mkType(*type->pool, sp, "D", 0x100 | 0)))));
    fcn.params().addTyParam(ASTTypeParam(*type->pool, sp, {}, "D"));
    fcn.params().addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, "D", 0x100 | 0), {}, this->getTraitPathDecoder()}));
    fcn.setCode(NEWNODE(Block, mv$(node)));

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));

    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    rv.addFunction(sp, {}, ASTVisibility::makeBarePrivate(), false, "decode", mv$(fcn));
    return mv$(rv);
}

auto DeriverRustcDecodable::decVal() const -> ASTExprNodeP {
    return NEWNODE(CallPath, this->getMethodPath(), vec$(NEWNODE(NamedValue, ASTPath("d"))));
}

auto DeriverRustcDecodable::field(const std::string& name) const -> ASTExprNodeP {
    return NEWNODE(Field, NEWNODE(NamedValue, ASTPath(RcString("self"))), RcString::newInterned(name));
}

auto DeriverRustcDecodable::decClosure(ObjPool& pool, Span sp, ASTExprNodeP code) const -> ASTExprNodeP {
    return NEWNODE(Closure, vec$(std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, "d"), ::mkType(pool, sp))), ::mkType(pool, sp), mv$(code), false, false, false);
}

auto DeriverRustcDecodable::getValErrStr(const RcString& coreName, std::string errStr) const -> ASTExprNodeP {
    return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Err"), vec$(NEWNODE(CallMethod, NEWNODE(NamedValue, ASTPath("d")), ASTPathNode("error"), vec$(NEWNODE(String, errStr)))));
}

auto DeriverRustcDecodable::getValOk(const RcString& coreName, ASTExprNodeP inner) const -> ASTExprNodeP {
    return NEWNODE(CallPath, getPath(coreName, "result", "Result", "Ok"), vec$(mv$(inner)));
}

auto DeriverRustcDecodable::getValOkUnit(const RcString& coreName) const -> ASTExprNodeP {
    return getValOk(coreName, NEWNODE(Tuple, {}));
}

auto DeriverRustcDecodable::traitName() const -> const char* {
    return "RustcDecodable";
}

auto DeriverRustcDecodable::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    ASTPath basePath = *type->data.as_Path();
    std::string structName = basePath.nodes().back().name().c_str();

    ASTExprNodeP nodeV;
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            ASTExprNodeStructLiteral::tValues vals;
            unsigned int idx = 0;
            for (const auto& fld : e.ents) {
                vals.push_back({{}, fld.name, NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_struct_field", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, fld.name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal()))))});
                idx++;
            }
            nodeV = NEWNODE(StructLiteral, basePath, nullptr, mv$(vals));
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            std::vector<ASTExprNodeP> vals;
            for (unsigned int idx = 0; idx < e.ents.size(); idx++) {
                vals.push_back(NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_tuple_struct_arg", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal())))));
            }
            nodeV = NEWNODE(CallPath, mv$(basePath), mv$(vals));
            break;
        }
    }

    auto closure = this->decClosure(*type->pool, sp, this->getValOk(opts.coreName, mv$(nodeV)));

    auto args = vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, structName), ASTExprNodeP(), mv$(closure));

    ASTExprNodeP node;
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            node = this->getValOk(opts.coreName, NEWNODE(NamedValue, mv$(basePath)));
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = str.data.as_Struct();
            assert(!args[2]);
            args[2] = NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT);
            node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_struct", mv$(args));
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = str.data.as_Tuple();
            assert(!args[2]);
            args[2] = NEWNODE(Integer, U128(e.ents.size()), CORETYPE_UINT);
            node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_tuple_struct", mv$(args));
            break;
        }
    }

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(str), mv$(node));
}

auto DeriverRustcDecodable::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    ASTPath basePath = *type->data.as_Path();
    basePath.nodes().back().args() = ASTPathParams();
    std::vector<ASTExprNodeMatchArm> arms;

    std::vector<ASTExprNodeP> varNameStrs;

    for (unsigned int varIdx = 0; varIdx < enm.variants().size(); varIdx++) {
        const auto& v = enm.variants()[varIdx];
        ASTExprNodeP code;

        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                code = NEWNODE(NamedValue, basePath + v.name);
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                std::vector<ASTExprNodeP> args;

                for (unsigned int idx = 0; idx < e.items.size(); idx++) {
                    args.push_back(NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant_arg", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal())))));
                }
                code = NEWNODE(CallPath, basePath + v.name, mv$(args));
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                ASTExprNodeStructLiteral::tValues vals;

                unsigned int idx = 0;
                for (const auto& fld : e.fields) {
                    vals.push_back({{}, fld.name, NEWNODE(UniOp, ASTExprNodeUniOp::QMARK, NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_struct_variant_field", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, fld.name.c_str()), NEWNODE(Integer, U128(idx), CORETYPE_UINT), this->decClosure(*type->pool, sp, this->decVal()))))});
                    idx++;
                }

                code = NEWNODE(StructLiteral, basePath + v.name, nullptr, mv$(vals));
                break;
            }
        }

        std::vector<ASTPattern> pats;
        pats.push_back(ASTPattern(ASTPattern::TagValue(), sp, ASTPattern::Value::make_Integer({CORETYPE_UINT, U128(varIdx)})));

        arms.push_back(ASTExprNodeMatchArm(mv$(pats), {}, this->getValOk(opts.coreName, mv$(code))));
        varNameStrs.push_back(NEWNODE(String, v.name.c_str()));
    }

    {
        arms.push_back(ASTExprNodeMatchArm(::makeVec1(ASTPattern()), {}, this->getValErrStr(opts.coreName, "enum value unknown")));
    }

    auto nodeMatch = NEWNODE(Match, NEWNODE(NamedValue, ASTPath("idx")), mv$(arms));
    auto nodeVarClosure = NEWNODE(Closure, vec$(std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, "d"), ::mkType(*type->pool, sp)), std::make_pair(ASTPattern(ASTPattern::TagBind(), sp, "idx"), ::mkType(*type->pool, sp))), ::mkType(*type->pool, sp), mv$(nodeMatch), false, false, false);
    std::string enumName = type->data.as_Path()->nodes().back().name().c_str();

    auto nodeRev = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum_variant", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(Array, mv$(varNameStrs))), mv$(nodeVarClosure)));

    auto node = NEWNODE(CallPath, this->getTraitPathDecoder() + "read_enum", vec$(NEWNODE(NamedValue, ASTPath("d")), NEWNODE(String, enumName), this->decClosure(*type->pool, sp, mv$(nodeRev))));

    return this->makeRet(sp, opts.coreName, p, type, this->getFieldBounds(enm), mv$(node));
}

auto DeriverConstParamTy::handleGeneric(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound) const -> ASTImpl {
    const ASTPath traitPath = getPath(opts.coreName, "marker", "StructuralPartialEq");
    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));
    ASTImpl rv(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
    return mv$(rv);
}

auto DeriverConstParamTy::traitName() const -> const char* {
    return "ConstParamTy";
}

auto DeriverConstParamTy::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    return handleGeneric(sp, opts, p, type, this->getFieldBounds(str));
}

auto DeriverConstParamTy::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    return handleGeneric(sp, opts, p, type, this->getFieldBounds(enm));
}

auto DeriverUnsizedConstParamTy::handleGeneric(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, std::vector<ASTType*> typesToBound) const -> ASTImpl {
    const ASTPath traitPath = getPath(opts.coreName, "marker", "UnsizedConstParamTy");
    const ASTPath eqPath = getPath(opts.coreName, "cmp", "Eq");

    std::vector<ASTType*> typesToBoundByEq;
    for (const auto* boundedType : typesToBound) {
        typesToBoundByEq.push_back(boundedType->clone());
    }

    ASTGenericParams params = getParamsWithBounds(*type->pool, sp, p, traitPath, mv$(typesToBound));
    unsigned int typeIndex = 0;
    for (const auto& param : p.params) {
        if (const auto* typeParam = param.opt_Type()) {
            params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mkType(*type->pool, sp, typeParam->name(), typeIndex), {}, eqPath}));
            typeIndex++;
        }
    }
    for (auto*& boundedType : typesToBoundByEq) {
        params.addBound(ASTGenericBound::make_IsTrait({sp, {}, mv$(boundedType), {}, eqPath}));
    }

    return ASTImpl(ASTImplDef(mv$(params), makeSpanned(sp, traitPath), type->clone()));
}

auto DeriverUnsizedConstParamTy::traitName() const -> const char* {
    return "UnsizedConstParamTy";
}

auto DeriverUnsizedConstParamTy::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTStruct& str) const -> ASTImpl {
    return handleGeneric(sp, opts, p, type, this->getFieldBounds(str));
}

auto DeriverUnsizedConstParamTy::handleItem(Span sp, const DeriveOpts& opts, const ASTGenericParams& p, ASTType* type, const ASTEnum& enm) const -> ASTImpl {
    return handleGeneric(sp, opts, p, type, this->getFieldBounds(enm));
}

auto DeriveRegistry::find(const RcString& traitName) const -> const Deriver* {
#define _(obj)                        \
    if (traitName == obj.traitName()) \
        return &obj;
    _(debug)
    _(partialEq)
    _(partialOrd)
    _(eq)
    _(ord)
    _(clone)
    _(copy)
    _(default_)
    _(hash)
    _(rustcEncodable)
    _(rustcDecodable)
    _(constParamTy)
    _(unsizedConstParamTy)
#undef _
    return nullptr;
}

DecoratorDerive::DecoratorDerive(const DeriveRegistry& registry)
    : registry(registry)
{
}

auto DecoratorDerive::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorDerive::wantsAllAttrs() const -> bool {
    return true;
}

auto DecoratorDerive::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t modIdx, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    switch (i.tag()) {
        case ASTItem::TAG_None: {
            break;
        }
        case ASTItem::TAG_Union: {
            auto& e = i.as_Union();
            deriveItem(registry, sp, wb, crate, mod, attr, path, attrs, vis, e);
            break;
        }
        case ASTItem::TAG_Enum: {
            auto& e = i.as_Enum();
            deriveItem(registry, sp, wb, crate, mod, attr, path, attrs, vis, e);
            break;
        }
        case ASTItem::TAG_Struct: {
            auto& e = i.as_Struct();
            deriveItem(registry, sp, wb, crate, mod, attr, path, attrs, vis, e);
            break;
        }
        default: {
            TODO(sp, "Handle #[derive] for other item types - " << i.tagStr());
            break;
        }
    }
}

auto CDocHandler::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const -> void {
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const -> void {
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const -> void {
}

auto CDocHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const -> void {
}

Handler::Handler(eItemType type, cbT cb)
    : type(type)
    , cb(cb)
{
}

auto LangItemRegistry::find(const char* name) const -> const Handler* {
    return handlers.find(name);
}

DecoratorLangItem::DecoratorLangItem(const LangItemRegistry& registry)
    : registry(registry)
{
}

auto DecoratorLangItem::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorLangItem::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    auto name = attr.parseEqualsString(wb, crate, mod);
    switch (i.tag()) {
        default:
            TODO(sp, "Unknown item type " << i.tagStr() << " with #[" << attr << "] attached at " << path);
            break;
        case ASTItem::TAG_None: {
            break;
        }
        case ASTItem::TAG_Impl: {
            if (name == "i8") {
            } else if (name == "u8") {
            } else if (name == "i16") {
            } else if (name == "u16") {
            } else if (name == "i32") {
            } else if (name == "u32") {
            } else if (name == "i64") {
            } else if (name == "u64") {
            } else if (name == "i128") {
            } else if (name == "u128") {
            } else if (name == "isize") {
            } else if (name == "usize") {
            } else if (name == "const_ptr") {
            } else if (name == "mut_ptr") {
            } else if (name == "const_slice_ptr") {
            } else if (name == "mut_slice_ptr") {
            } else if (name == "array") {
            } else if (name == "bool") {
            } else if (name == "char") {
            } else if (name == "str") {
            } else if (name == "slice") {
            } else if (name == "slice_u8") {
            } else if (name == "slice_alloc") {
            } else if (name == "slice_u8_alloc") {
            } else if (name == "str_alloc") {
            } else if (name == "f32") {
            } else if (name == "f64") {
            } else if (name == "f32_runtime") {
            } else if (name == "f64_runtime") {
            } else {
                ERROR(sp, E0000, "Unknown lang item '" << name << "' on impl");
            }

            // TODO: Somehow annotate these impls to allow them to provide inherents?

            break;
        }
        case ASTItem::TAG_Function: {
            auto& e = i.as_Function();
            if (e.code().isValid()) {
                handleLangItem(registry, sp, crate, path, name, ITEM_FN, i);
            } else {
                handleLangItem(registry, sp, crate, path, name, ITEM_EXTERN_FN, i);
            }
            break;
        }
        case ASTItem::TAG_Type: {
            handleLangItem(registry, sp, crate, path, name, ITEM_TYPE_ALIAS, i);
            break;
        }
        case ASTItem::TAG_Static: {
            handleLangItem(registry, sp, crate, path, name, ITEM_STATIC, i);
            break;
        }
        case ASTItem::TAG_Struct: {
            handleLangItem(registry, sp, crate, path, name, ITEM_STRUCT, i);
            break;
        }
        case ASTItem::TAG_Enum: {
            handleLangItem(registry, sp, crate, path, name, ITEM_ENUM, i);
            break;
        }
        case ASTItem::TAG_Union: {
            handleLangItem(registry, sp, crate, path, name, ITEM_UNION, i);
            break;
        }
        case ASTItem::TAG_Trait: {
            handleLangItem(registry, sp, crate, path, name, ITEM_TRAIT, i);
            break;
        }
    }
}

auto DecoratorLangItem::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
    auto name = mi.parseEqualsString(wb, crate, crate.rootModule_);
    if (name == "into_future") {
        ASSERT_BUG(sp, i.is_Function(), "#[lang = \"into_future\"] on non-function trait item " << path);
        handleLangItem(registry, sp, crate, path, name, ITEM_FN, i);
    }
}

auto DecoratorLangItem::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const -> void {
    // TODO: Enum variants (sub-item of other lang items)
}

auto DecoratorLangItem::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: lang items on associated items (e.g. functions - `RangeFull::new`)
}

auto DecoratorMain::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorMain::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
    } else if (/*const auto* e =*/i.opt_Function()) {
        auto rv = crate.langItems.insert(std::make_pair(std::string("trustme-main"), path));
        if (!rv.second) {
            const auto& otherPath = rv.first->second;
            ERROR(sp, E0000, "Duplicate definition of #[main] - " << otherPath << " and " << path);
        }
    } else {
        ERROR(sp, E0000, "#[main] on non-function " << path);
    }
}

auto DecoratorStart::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorStart::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
    } else if (i.is_Function()) {
        auto rv = crate.langItems.insert(std::make_pair(std::string("trustme-start"), path));
        if (!rv.second) {
            const auto& otherPath = rv.first->second;
            ERROR(sp, E0000, "Duplicate definition of #[start] - " << otherPath << " and " << path);
        }
    } else {
        ERROR(sp, E0000, "#[start] on non-function " << path);
    }
}

auto DecoratorPanicImplementation::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorPanicImplementation::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_Function()) {
        auto rv = crate.langItems.insert(std::make_pair(std::string("trustme-panic_implementation"), path));
        if (!rv.second) {
            const auto& otherPath = rv.first->second;
            ERROR(sp, E0000, "Duplicate definition of #[panic_implementation] - " << otherPath << " and " << path);
        }
    } else {
        ERROR(sp, E0000, "#[panic_implementation] on non-function " << path);
    }
}

auto DecoratorPanicHandler::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorPanicHandler::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_Function()) {
        auto rv = crate.langItems.insert(std::make_pair(std::string("trustme-panic_implementation"), path));
        if (!rv.second) {
            const auto& otherPath = rv.first->second;
            ERROR(sp, E0000, "Duplicate definition of #[panic_handler] - " << otherPath << " and " << path);
        }
    } else {
        ERROR(sp, E0000, "#[panic_handler] on non-function " << path);
    }
}

auto DecoratorRustcStdInternalSymbol::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorRustcStdInternalSymbol::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto DecoratorAllocErrorHandler::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorAllocErrorHandler::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_Function()) {
        auto rv = crate.langItems.insert(std::make_pair(std::string("trustme-alloc_error_handler"), path));
        if (!rv.second) {
            const auto& otherPath = rv.first->second;
            ERROR(sp, E0000, "Duplicate definition of #[alloc_error_handler] - " << otherPath << " and " << path);
        }
    }
}

auto DecoratorGlobalAllocator::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorGlobalAllocator::handle(const Span& sp, const ASTAttribute&, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute>, const ASTVisibility&, ASTItem& item) const -> void {
    if (!item.is_Static()) {
        ERROR(sp, E0000, "#[global_allocator] on non-static " << path);
    }
    auto rv = crate.langItems.insert(std::make_pair(std::string("trustme-global_allocator"), path));
    if (!rv.second && rv.first->second != path) {
        ERROR(sp, E0000, "Duplicate definition of #[global_allocator] - " << rv.first->second << " and " << path);
    }
}

auto CMultiHandlerLint::stage() const -> AttrStage {
    return AttrStage::Pre;
}

template <typename F>
auto CMultiHandlerLint::collectLintNames(const ASTAttribute& mi, const F& cb) -> void {
    TTStream lex(mi.span(), ParseState(), mi.data());
    if (!lex.getTokenIf(TOK_PAREN_OPEN)) {
        return;
    }
    unsigned depth = 1;
    bool atName = true;
    while (depth > 0) {
        auto tok = lex.getToken();
        if (tok == TOK_EOF) {
            break;
        }
        if (tok == TOK_PAREN_OPEN) {
            depth += 1;
            atName = false;
            continue;
        }
        if (tok == TOK_PAREN_CLOSE) {
            depth -= 1;
            atName = true;
            continue;
        }
        if (tok == TOK_COMMA) {
            atName = (depth == 1);
            continue;
        }
        if (depth == 1 && atName && tok == TOK_IDENT) {
            const auto next = lex.lookahead(0);
            if (next == TOK_COMMA || next == TOK_PAREN_CLOSE) {
                cb(tok.ident().name);
            }
        }
        atName = false;
    }
}

auto CMultiHandlerLint::recordItemLevel(const ASTAttribute& mi, ASTItem& item) const -> void {
    LintLevelOverrides* overrides = nullptr;
    if (auto* function = item.opt_Function()) {
        overrides = &function->markings.lintLevels;
    } else if (auto* module = item.opt_Module()) {
        overrides = &module->lintLevels;
    }
    if (!overrides) {
        return;
    }
    collectLintNames(mi, [&](const RcString& name) {
        const bool isGroup = name == "warnings" || name == "unused";
        overrides->set(name, isGroup, this->level());
    });
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    collectLintNames(mi, [&](const RcString& name) {
        CfgSetLintLevel(*wb.settings, name.c_str(), this->level());
    });
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    recordItemLevel(mi, i);
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    recordItemLevel(mi, i);
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
    recordItemLevel(mi, i);
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const -> void {
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const -> void {
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const -> void {
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const -> void {
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeMatchArm& expr) const -> void {
}

auto CMultiHandlerLint::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeStructLiteral::Ent& expr) const -> void {
}

auto CHandlerAllow::level() const -> CfgLintLevel {
    return CfgLintLevel::Allow;
}

auto CHandlerWarn::level() const -> CfgLintLevel {
    return CfgLintLevel::Warn;
}

auto CHandlerDeny::level() const -> CfgLintLevel {
    return CfgLintLevel::Deny;
}

auto CHandlerForbid::level() const -> CfgLintLevel {
    return CfgLintLevel::Forbid;
}

auto CHandlerMustUse::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerMustUse::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: only allowed on types
}

auto CHandlerMustUse::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: only allowed on associated types
}

auto CHandlerMustUse::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
    // TODO: only allowed on associated types
}

auto CHandlerNonExhaustive::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerNonExhaustive::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: only allowed on types
}

auto CHandlerPath::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerPath::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: only allowed on modules
}

auto CHandlerRustcPromotable::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerRustcPromotable::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CHandlerRustcPromotable::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    // TODO: only allowed on functions?
}

auto CHandlerRustcPromotable::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
    // TODO: only allowed on functions?
}

auto CHandlerRustcInheritOverflowChecks::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerRustcInheritOverflowChecks::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CHandlerRustcInheritOverflowChecks::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CHandlerRustcInheritOverflowChecks::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
}

auto CHandlerRustcInheritOverflowChecks::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const -> void {
}

auto CHandlerRustcOnUnimiplemented::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerRustcOnUnimiplemented::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CHandlerRustBox::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto CHandlerRustBox::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTExprNodeP& expr) const -> void {
    auto* n = cast<ASTExprNodeCallPath>(expr.get());
    ASSERT_BUG(expr->span(), n, "");
    ASSERT_BUG(expr->span(), n->args.size() == 1, "");
    auto val = std::move(n->args[0]);
    auto span = n->span();
    expr.reset(new ASTExprNodeUniOp(ASTExprNodeUniOp::BOX, std::move(val)));
    expr->setSpan(span);
}

auto CMultiHandlerStability::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CMultiHandlerStability::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
}

auto CMultiHandlerStability::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CMultiHandlerStability::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTImpl& impl, const RcString& name, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto CMultiHandlerStability::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTTrait& trait, slice<const ASTAttribute> attrs, ASTItem& i) const -> void {
}

auto CMultiHandlerStability::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTStructItem& si) const -> void {
}

auto CMultiHandlerStability::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTTupleItem& si) const -> void {
}

auto CMultiHandlerStability::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, ASTEnumVariant& ev) const -> void {
}

auto CHandlerAllowInternalUnstable::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CHandlerAllowInternalUnstable::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
}

auto DecoratorNoStd::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorNoStd::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    if (crate.loadStd != ASTCrate::LOAD_STD && crate.loadStd != ASTCrate::LOAD_CORE) {
        WARNING(sp, W0000, "Use of #![no_std] with itself or #![no_core]");
        return;
    }
    crate.loadStd = ASTCrate::LOAD_CORE;
}

auto DecoratorNoCore::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorNoCore::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate) const -> void {
    if (crate.loadStd != ASTCrate::LOAD_STD && crate.loadStd != ASTCrate::LOAD_NONE) {
        WARNING(sp, W0000, "Use of #![no_core] with itself or #![no_std]");
    }
    crate.loadStd = ASTCrate::LOAD_NONE;
}

auto DecoratorNoMain::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorNoMain::handle(const Span&, const ASTAttribute&, const WireBoard&, ASTCrate& crate) const -> void {
    crate.noMain = true;
}

auto DecoratorNoPrelude::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorNoPrelude::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_Module()) {
        i.as_Module().insertPrelude = false;
    } else {
        ERROR(sp, E0000, "Invalid use of #[no_prelude] on non-module");
    }
}

auto DecoratorPreludeImport::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto DecoratorPreludeImport::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (const auto* e = i.opt_Use()) {
        if (e->entries.size() != 1) {
            ERROR(sp, E0000, "#[prelude_import] should be on a single-entry use");
        }
        ASSERT_BUG(sp, path.nodes.size() > 0, path);
        ASSERT_BUG(sp, path.nodes.back() == "", path);
        if (e->entries.front().name != "") {
            ERROR(sp, E0000, "#[prelude_import] should be on a glob");
        }
        const auto& p = e->entries.front().path;
        // TODO: Ensure that this statement is a glob (has a name of "")
        if (p.isRelative()) {
            crate.preludePath = ASTPath(path);
            crate.preludePath.nodes().pop_back();
            crate.preludePath += p;
        } else {
            crate.preludePath = ASTPath(p);
        }
    } else {
        ERROR(sp, E0000, "Invalid use of #[prelude_import] on non-use");
    }
}

auto CTestHandler::stage() const -> AttrStage {
    return AttrStage::Pre;
}

auto CTestHandler::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (!i.is_Function()) {
        ERROR(sp, E0000, "#[test] can only be put on functions - found on " << i.tagStr());
    }

    if (crate.testHarness) {
        ASTTestDesc td;
        td.span = sp;
        for (const auto& node : path.nodes) {
            if (!td.name.empty()) {
                td.name += "::";
            }
            td.name += node.c_str();
        }
        td.path = path;

        crate.tests.push_back(mv$(td));
    } else {
        i = ASTItem::make_None({});
    }
}

auto CTestHandlerSP::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto CTestHandlerSP::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule& mod, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (!i.is_Function()) {
        ERROR(sp, E0000, "#[should_panic] can only be put on functions - found on " << i.tagStr());
    }

    if (crate.testHarness) {
        // TODO: If this test doesn't yet exist, create it (but as disabled)?
        for (auto& td : crate.tests) {
            if (td.path != path) {
                continue;
            }

            if (mi.data().size() != 0) {
                td.panicType = ASTTestDesc::ShouldPanic::YesWithMessage;

                TTStream lex(sp, ParseState(), mi.data());
                lex.parseState().wb = &wb;
                auto parseMessage = [&]() {
                    auto n = ExpandParseAndExpandExprVal(crate, mod, lex);
                    if (auto* v = cast<ASTExprNodeString>(&*n)) {
                        td.expectedPanicMessage = v->value;
                    } else {
                        parseErrorUnexpected(lex, Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())), TOK_STRING);
                    }
                };
                if (lex.getTokenIf(TOK_EQUAL)) {
                    parseMessage();
                } else {
                    bool gotMessage = false;
                    if (lex.getTokenIf(TOK_PAREN_OPEN) && lex.lookahead(0) == TOK_IDENT && lex.lookahead(1) == TOK_EQUAL) {
                        auto n = lex.getTokenCheck(TOK_IDENT).ident().name;
                        lex.getTokenCheck(TOK_EQUAL);
                        if (n == "expected" && lex.lookahead(0) == TOK_STRING) {
                            parseMessage();
                            gotMessage = lex.lookahead(0) == TOK_PAREN_CLOSE;
                        }
                    }
                    if (!gotMessage) {
                        td.panicType = ASTTestDesc::ShouldPanic::Yes;
                        td.expectedPanicMessage = "";
                    }
                }
            } else {
                td.panicType = ASTTestDesc::ShouldPanic::Yes;
            }
            return;
        }
    }
}

auto CTestHandlerIgnore::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto CTestHandlerIgnore::handle(const Span& sp, const ASTAttribute& mi, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (!i.is_Function()) {
        ERROR(sp, E0000, "#[ignore] can only be put on functions - found on " << i.tagStr());
    }

    if (crate.testHarness) {
        for (auto& td : crate.tests) {
            if (td.path != path) {
                continue;
            }

            td.ignore = true;
            return;
        }
    }
}
