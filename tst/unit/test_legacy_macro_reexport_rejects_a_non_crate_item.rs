//@ compile-fail: Use of #[macro_reexport] on non-crate
#[macro_reexport(missing)]
fn helper() {}
fn main() {}
