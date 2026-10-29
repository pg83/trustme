//@ compile-fail: different source
fn pair() -> (impl Copy, impl Copy) { (1u8, 2u8) }
fn same<T: Copy>(_: T, _: T) {}
fn main() { let (first, second) = pair(); same(first, second); }
