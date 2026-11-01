//@ compile-fail: Duplicate definition of #[start]
#[start]
fn first(_: isize, _: *const *const u8) -> isize { 0 }
#[start]
fn second(_: isize, _: *const *const u8) -> isize { 0 }
fn main() {}
