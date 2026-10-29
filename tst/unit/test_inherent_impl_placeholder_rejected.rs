//@ compile-fail: the placeholder `_` is not allowed in impl signatures
struct Wrap<T>(T);
impl Wrap<_> {}
fn main() {}
