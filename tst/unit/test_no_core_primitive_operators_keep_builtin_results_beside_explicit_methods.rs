//@ compile-flags: -Coverflow-checks=off -Clink-arg=-lc
//@ compile-variant: -O -Coverflow-checks=off
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"] trait PointeeSized {}
#[lang = "meta_sized"] trait MetaSized: PointeeSized {}
#[lang = "sized"] trait Sized: MetaSized {}
#[lang = "copy"] trait Copy {}
impl Copy for i32 {}
impl Copy for u8 {}
impl Copy for bool {}
#[lang = "add"] trait Add<Rhs = Self> { type Output; fn add(self, rhs: Rhs) -> Self::Output; }
#[lang = "neg"] trait Neg { type Output; fn neg(self) -> Self::Output; }
#[lang = "not"] trait Not { type Output; fn not(self) -> Self::Output; }
#[lang = "shl"] trait Shl<Rhs> { type Output; fn shl(self, rhs: Rhs) -> Self::Output; }
#[lang = "drop_in_place"] unsafe fn drop_in_place<T: PointeeSized>(_: *mut T) {}
static mut CALLED: i32 = 0;
impl Add for i32 {
    type Output = i32;
    fn add(self, _: i32) -> i32 { unsafe { CALLED = 7; } 41 }
}
impl Neg for i32 {
    type Output = i32;
    fn neg(self) -> i32 { unsafe { CALLED = 11; } 43 }
}
impl Not for bool {
    type Output = bool;
    fn not(self) -> bool { unsafe { CALLED = 13; } false }
}
impl Shl<u8> for i32 {
    type Output = i32;
    fn shl(self, _: u8) -> i32 { unsafe { CALLED = 17; } 47 }
}
#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    match (17i32 + 2i32, -5i32, !false, 3i32 << 2u8, unsafe { CALLED }) {
        (19, -5, true, 12, 0) => {}
        _ => return 1,
    }
    match (<i32 as Add>::add(1, 2), <i32 as Neg>::neg(5), <bool as Not>::not(false), <i32 as Shl<u8>>::shl(3, 2)) {
        (41, 43, false, 47) => {}
        _ => return 2,
    }
    match unsafe { CALLED } { 17 => 0, _ => 3 }
}
