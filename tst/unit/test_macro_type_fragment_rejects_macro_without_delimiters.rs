//@ compile-fail: error
macro_rules! define { ($ty:ty) => { fn main() {} }; }
define!(missing_type! u32);
