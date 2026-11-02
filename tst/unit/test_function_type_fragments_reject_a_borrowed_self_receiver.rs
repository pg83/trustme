//@ compile-fail: self is not a type
macro_rules! function_type { ($ty:ty) => { type Callback = $ty; }; }
function_type!(for<'a> fn(&'a mut self));
fn main() {}
