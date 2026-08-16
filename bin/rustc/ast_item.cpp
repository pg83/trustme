#include "ast_item.h"

ASTVisibility::ASTVisibility()
    : ty_(Ty::Pub)
{
}

ASTVisibility ASTVisibility::makeBarePrivate() {
    ASTVisibility rv;
    rv.ty_ = Ty::Private;
    return rv;
}

const ASTPath& ASTVisibility::inPath() const {
    assert(inPath_);
    return *inPath_;
}

const ASTAbsolutePath& ASTVisibility::visPath() const {
    assert(visPath_);
    return *visPath_;
}
