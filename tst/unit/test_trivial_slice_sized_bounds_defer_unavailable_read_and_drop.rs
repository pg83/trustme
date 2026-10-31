//@ crate-type: lib
#![feature(trivial_bounds)]
pub fn deferred<T>(value: &[u8]) where [u8]: Sized {
    std::mem::drop(unsafe { std::ptr::read(value) });
}
