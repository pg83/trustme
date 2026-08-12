#include "ast_item.h"

namespace AST {

Visibility::Visibility()
    : mTy(Ty::Pub) {
}
Visibility Visibility::makeBarePrivate() {
    Visibility rv;
    rv.mTy = Ty::Private;
    return rv;
}
const AST::Path& Visibility::inPath() const {
    assert(mInPath);
    return *mInPath;
}
const AST::AbsolutePath& Visibility::visPath() const {
    assert(mVisPath);
    return *mVisPath;
}
}
