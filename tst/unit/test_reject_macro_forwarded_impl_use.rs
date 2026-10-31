//@ compile-fail: not allowed
macro_rules! member { ($item:item) => { $item }; }
struct Host; impl Host { member!(use core::fmt::Debug;); }
fn main() {}
