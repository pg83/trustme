//@ compile-fail: #[rustc_intrinsic] on function with ABI already set
#![feature(rustc_attrs)]
#[rustc_intrinsic]
extern "C" fn intrinsic() {}
fn main() {}
