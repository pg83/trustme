//@ compile-fail: only be applied to functions
#[proc_macro_derive(Mark)]
enum Choice { Value }
fn main() {}
