#include "ast_item.h"

#include "output.h"
#include "ast_path.h"

using namespace stl;

ASTVisibility::ASTVisibility()
    : ty_(Ty::Pub)
{
}

ASTVisibility ASTVisibility::makeBarePrivate() {
    ASTVisibility rv;
    rv.ty_ = Ty::Private;
    return rv;
}

ASTVisibility ASTVisibility::makeGlobal() {
    return ASTVisibility();
}

ASTVisibility ASTVisibility::makeRestricted(Ty type, ASTAbsolutePath path) {
    ASTVisibility result;
    result.ty_ = type;
    result.visPath_ = std::make_shared<ASTAbsolutePath>(std::move(path));
    return result;
}

ASTVisibility ASTVisibility::makeRestricted(ASTAbsolutePath path, ASTPath inPath) {
    ASTVisibility result;
    result.ty_ = Ty::PubIn;
    result.visPath_ = std::make_shared<ASTAbsolutePath>(std::move(path));
    result.inPath_ = std::make_shared<ASTPath>(std::move(inPath));
    return result;
}

void ASTVisibility::fmt(ZeroCopyOutput& out) const {
    switch (ty_) {
        case Ty::Private:
            break;
        case Ty::Pub:
            out << StringView("pub ");
            break;
        case Ty::Crate:
            out << StringView("crate ");
            break;
        case Ty::PubCrate:
            out << StringView("pub(crate) ");
            break;
        case Ty::PubSuper:
            out << StringView("pub(super) ");
            break;
        case Ty::PubSelf:
            out << StringView("pub(self) ");
            break;
        case Ty::PubIn:
            out << StringView("pub(in ");
            if (inPath_) {
                out << *inPath_;
            } else {
                out << StringView("???");
            }
            out << StringView(")");
            break;
    }
}

const ASTPath& ASTVisibility::inPath() const {
    BUG_ASSERT(inPath_);
    return *inPath_;
}

const ASTAbsolutePath& ASTVisibility::visPath() const {
    BUG_ASSERT(visPath_);
    return *visPath_;
}

bool ASTVisibility::isVisible(const ASTAbsolutePath& fromMod) const {
    if (!visPath_) {
        return true;
    }
    if (visPath_->crate != fromMod.crate || visPath_->nodes.length() > fromMod.nodes.length()) {
        return false;
    }
    for (size_t i = 0; i < visPath_->nodes.length(); i++) {
        if (visPath_->nodes[i] != fromMod.nodes[i]) {
            return false;
        }
    }
    return true;
}

bool ASTVisibility::contains(const ASTVisibility& value) const {
    return visPath_ ? value.isVisible(*visPath_) : true;
}

void ASTVisibility::inplaceUnion(const ASTVisibility& value) {
    if (contains(value)) {
        return;
    }
    if (value.contains(*this)) {
        visPath_ = value.visPath_;
        return;
    }
    TODO(Span(), StringView("Union with incompatible visbility"));
}

template <>
void stl::output<ZeroCopyOutput, ASTVisibility>(ZeroCopyOutput& out, const ASTVisibility& value) {
    value.fmt(out);
}
