#![feature(core_intrinsics)]
#![allow(internal_features)]
#[inline(never)]
fn unsigned(value: u8) -> u8 { core::intrinsics::bswap(value) }
#[inline(never)]
fn signed(value: i8) -> i8 { core::intrinsics::bswap(value) }
#[inline(never)]
fn literal() -> u8 { core::intrinsics::bswap(29u8) }
fn main() {
    assert_eq!(unsigned(std::hint::black_box(0xA5)), 0xA5);
    assert_eq!(signed(std::hint::black_box(-37)), -37);
    assert_eq!(literal(), 29);
}
