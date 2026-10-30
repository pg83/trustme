//@ compile-fail: No arm matched
macro_rules! union { ($name:ident) => { fn $name() {} }; }
macro_rules! item { ($item:item) => { $item }; }
item! { union!(missing) }
fn main() {}
