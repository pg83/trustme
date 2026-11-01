//@ compile-fail: only be applied to functions
#[proc_macro]
struct Record;
fn main() {}
