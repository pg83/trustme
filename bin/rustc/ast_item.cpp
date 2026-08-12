#include "ast_item.h"

namespace AST {

Visibility::Visibility()
    : m_ty(Ty::Pub) {
}
Visibility Visibility::make_bare_private() {
    Visibility rv;
    rv.m_ty = Ty::Private;
    return rv;
}
const AST::Path& Visibility::in_path() const {
    assert(m_in_path);
    return *m_in_path;
}
const AST::AbsolutePath& Visibility::vis_path() const {
    assert(m_vis_path);
    return *m_vis_path;
}
}
