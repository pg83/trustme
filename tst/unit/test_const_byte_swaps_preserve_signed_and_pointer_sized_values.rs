#![feature(core_intrinsics)]
const U8_VALUE: u8 = std::intrinsics::bswap(0x93u8);
const I8_VALUE: i8 = std::intrinsics::bswap(-19i8);
const U16_VALUE: u16 = std::intrinsics::bswap(0x173bu16);
const I32_VALUE: i32 = std::intrinsics::bswap(-37i32);
const U64_VALUE: u64 = std::intrinsics::bswap(0x0123_4567_89ab_cdefu64);
const I128_VALUE: i128 = std::intrinsics::bswap(-41i128);
const USIZE_VALUE: usize = std::intrinsics::bswap(0x173b_2547usize);
const ISIZE_VALUE: isize = std::intrinsics::bswap(-43isize);
fn main() {
    assert_eq!((U8_VALUE, I8_VALUE, U16_VALUE), (0x93, -19, 0x3b17));
    assert_eq!(I32_VALUE, (-37i32).swap_bytes());
    assert_eq!(U64_VALUE, 0xefcd_ab89_6745_2301);
    assert_eq!(I128_VALUE, (-41i128).swap_bytes());
    assert_eq!(USIZE_VALUE, 0x173b_2547usize.swap_bytes());
    assert_eq!(ISIZE_VALUE, (-43isize).swap_bytes());
}
