//@ compile-variant: -O
#![feature(core_intrinsics)]
#[inline(never)]
fn swapped<T: Copy>(value: T) -> T { std::intrinsics::bswap(value) }
fn main() {
    for value in [0u8, 1, 127, 128, 255] {
        assert_eq!(swapped(std::hint::black_box(value)), value);
    }
    for value in [-128i8, -91, -1, 0, 127] {
        assert_eq!(swapped(std::hint::black_box(value)), value);
    }
    assert_eq!(std::intrinsics::bswap(19u8), 19);
    assert_eq!(std::intrinsics::bswap(-37i8), -37);
    assert_eq!(swapped(0x1234u16), 0x3412);
}
