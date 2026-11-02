//@ compile-fail: self parameter
macro_rules! function_type { ($ty:ty) => { type Callback = $ty; }; }
function_type!(fn(self: u16));
fn main() {}
