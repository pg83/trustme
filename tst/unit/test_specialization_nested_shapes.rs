#![feature(specialization)]
#![allow(incomplete_features)]

trait Kind { const ID: u8; }
struct Wrap<T>(T);

impl<T> Kind for Wrap<T> { default const ID: u8 = 1; }
impl Kind for Wrap<u16> { const ID: u8 = 2; }
impl<T> Kind for (T, u8) { default const ID: u8 = 3; }
impl Kind for (u16, u8) { const ID: u8 = 4; }
impl<T> Kind for [T; 2] { default const ID: u8 = 5; }
impl Kind for [u16; 2] { const ID: u8 = 6; }
impl<T> Kind for *const T { default const ID: u8 = 7; }
impl Kind for *const u16 { const ID: u8 = 8; }
impl<T> Kind for *mut T { default const ID: u8 = 9; }
impl Kind for *mut u16 { const ID: u8 = 10; }
impl<T> Kind for &T { default const ID: u8 = 11; }
impl Kind for &u16 { const ID: u8 = 12; }
impl<T> Kind for &mut T { default const ID: u8 = 13; }
impl Kind for &mut u16 { const ID: u8 = 14; }
impl<T> Kind for [T] { default const ID: u8 = 15; }
impl Kind for [u16] { const ID: u8 = 16; }

fn main() {
    assert_eq!([Wrap::<u8>::ID, Wrap::<u16>::ID], [1, 2]);
    assert_eq!([<(u8, u8)>::ID, <(u16, u8)>::ID], [3, 4]);
    assert_eq!([<[u8; 2]>::ID, <[u16; 2]>::ID], [5, 6]);
    assert_eq!([<*const u8>::ID, <*const u16>::ID], [7, 8]);
    assert_eq!([<*mut u8>::ID, <*mut u16>::ID], [9, 10]);
    assert_eq!([<&u8>::ID, <&u16>::ID], [11, 12]);
    assert_eq!([<&mut u8>::ID, <&mut u16>::ID], [13, 14]);
    assert_eq!([<[u8]>::ID, <[u16]>::ID], [15, 16]);
}
