//@ compile-fail: No macro arm matched
macro_rules! keep { ($value:expr) => { $value }; }
fn main() { let _ = keep!(::); }
