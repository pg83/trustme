#include "ast_item.h"


ASTVisibility::ASTVisibility()
    : mTy(Ty::Pub) {
}
ASTVisibility ASTVisibility::makeBarePrivate() {
    ASTVisibility rv;
    rv.mTy = Ty::Private;
    return rv;
}
const ASTPath& ASTVisibility::inPath() const {
    assert(mInPath);
    return *mInPath;
}
const ASTAbsolutePath& ASTVisibility::visPath() const {
    assert(mVisPath);
    return *mVisPath;
}
