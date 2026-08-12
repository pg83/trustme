#include "ast_item.h"

namespace AST {

Visibility::Visibility()
    : mTy(Ty::Pub) {
}
Visibility Visibility::make_bare_private() {
    Visibility rv;
    rv.mTy = Ty::Private;
    return rv;
}
const AST::Path& Visibility::in_path() const {
    assert(inPath);
    return *inPath;
}
const AST::AbsolutePath& Visibility::vis_path() const {
    assert(visPath);
    return *visPath;
}
}
