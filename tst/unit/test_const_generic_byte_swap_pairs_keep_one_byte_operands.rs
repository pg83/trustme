#![feature(core_intrinsics)]

const fn swap_pair<T: Copy>(value: T) -> (T, T) {
    (core::intrinsics::bswap(value), core::intrinsics::bswap(value))
}

const BYTE: (u8, u8) = swap_pair(0xd7u8);
const SIGNED: (i8, i8) = swap_pair(-57i8);
const WORD: (u16, u16) = swap_pair(0x1337u16);

fn main() {
    assert_eq!(BYTE, (0xd7, 0xd7));
    assert_eq!(SIGNED, (-57, -57));
    assert_eq!(WORD, (0x3713, 0x3713));
}
