//@ compile-fail: macro
macro_rules! select { ($pattern:pat => $value:expr) => { $value }; }
fn main() { let _ = select!(-true => 7); }
