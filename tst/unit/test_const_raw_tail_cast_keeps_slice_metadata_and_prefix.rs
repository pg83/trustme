//@ compile-variant: -O
#![feature(ptr_metadata)]
#[repr(C)]
struct Tail<T: ?Sized> { prefix: u16, values: T }
static DATA: Tail<[u16; 3]> = Tail { prefix: 17, values: [23, 29, 31] };
const POINTER: *const Tail<[u16]> = &DATA as *const Tail<[u16; 3]> as *const Tail<[u16]>;
const LENGTH: usize = std::ptr::metadata(POINTER);
fn main() {
    assert_eq!(LENGTH, 3);
    unsafe {
        assert_eq!((*POINTER).prefix, 17);
        assert_eq!(&(*POINTER).values, &[23, 29, 31]);
    }
}
