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
    BUG_ASSERT(inPath_);
    return *inPath_;
}

const ASTAbsolutePath& ASTVisibility::visPath() const {
    BUG_ASSERT(visPath_);
    return *visPath_;
}
