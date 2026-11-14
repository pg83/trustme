#![feature(ptr_metadata)]

fn retain<T: core::ptr::Pointee>(value: T) -> T {
    value
}

fn main() {
    assert_eq!(retain(37), 37);
}
