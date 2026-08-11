//@ check-pass
//@ compile-flags: -Znext-solver

#![feature(ptr_metadata)]

fn null_for_thin<T: ?Sized + std::ptr::Thin>() -> *const T {
    std::ptr::null()
}

fn main() {
    assert!(null_for_thin::<u8>().is_null());
}
