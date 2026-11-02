//@ compile-fail: self parameter
macro_rules! function_type { ($ty:ty) => { fn invalid(_: $ty) {} }; }
function_type!(fn(self));
fn main() {}
