//@ compile-fail: Unexpected token TOK_GT
macro_rules! keep { ($value:expr) => { $value }; }
fn main() { let _ = keep!(<u8 as >::VALUE); }
