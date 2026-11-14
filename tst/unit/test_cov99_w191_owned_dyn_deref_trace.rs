//@ crate-type: lib
//@ compile-flags: -Z dump-hir -Z stop-after=typeck
//@ env: RUSTC_BOOTSTRAP=1
#![feature(auto_traits, negative_impls)]

use std::ops::Deref;

pub auto trait ValueOnly {}
impl<T: ?Sized> !ValueOnly for &T {}
impl<T: ?Sized> !ValueOnly for &mut T {}

trait Measure {
    fn measure(self, bias: u32) -> u32;
}
impl<T: ValueOnly> Measure for T {
    fn measure(self, bias: u32) -> u32 { bias }
}

pub fn inspect(source: &(dyn Deref<Target = u32> + ValueOnly)) -> u32 {
    source.measure(41)
}
