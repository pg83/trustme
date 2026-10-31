#![feature(specialization)]
#![allow(incomplete_features)]
trait Rank { const VALUE: u8; }
impl<T> Rank for [T; 3] { default const VALUE: u8 = 17; }
impl Rank for [u8; 3] { const VALUE: u8 = 23; }
impl<T> Rank for (T, u16) { default const VALUE: u8 = 17; }
impl Rank for (u8, u16) { const VALUE: u8 = 23; }
impl<T> Rank for [T] { default const VALUE: u8 = 17; }
impl Rank for [u8] { const VALUE: u8 = 23; }
impl<T: ?Sized> Rank for *const T { default const VALUE: u8 = 17; }
impl Rank for *const u8 { const VALUE: u8 = 23; }
impl<T: ?Sized> Rank for &T { default const VALUE: u8 = 17; }
impl Rank for &u8 { const VALUE: u8 = 23; }
fn main() {
    assert_eq!((<[u8; 3] as Rank>::VALUE, <[u16; 3] as Rank>::VALUE), (23, 17));
    assert_eq!((<(u8, u16) as Rank>::VALUE, <(u32, u16) as Rank>::VALUE), (23, 17));
    assert_eq!((<[u8] as Rank>::VALUE, <[u16] as Rank>::VALUE), (23, 17));
    assert_eq!((<*const u8 as Rank>::VALUE, <*const u16 as Rank>::VALUE), (23, 17));
    assert_eq!((<&u8 as Rank>::VALUE, <&u16 as Rank>::VALUE), (23, 17));
}
