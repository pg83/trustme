//@ compile-fail: different source
fn red() -> impl Copy { 1u8 }
fn blue() -> impl Copy { 2u8 }
fn same<T: Copy>(_: T, _: T) {}
fn main() { same(red(), blue()); }
