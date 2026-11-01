//@ compile-fail: only be applied to functions
#[proc_macro_attribute]
const VALUE: u8 = 7;
fn main() {}
