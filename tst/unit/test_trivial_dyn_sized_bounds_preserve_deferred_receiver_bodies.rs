//@ crate-type: lib
#![feature(trivial_bounds)]
pub trait Value { fn value(self) -> u32; }
pub fn deferred<T>(value: &(dyn Value + 'static)) -> u32 where dyn Value: Sized {
    unsafe { std::ptr::read(value) }.value()
}
