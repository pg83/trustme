//@ compile-fail: error
macro_rules! define { ($ty:ty) => { fn main() {} }; }
define!(fn u32);
