//@ edition: 2021

use std::ops::{BitOrAssign, ShlAssign};

struct Bits(u32);

impl ShlAssign<u8> for Bits {
    fn shl_assign(&mut self, rhs: u8) {
        self.0 <<= rhs;
    }
}

impl BitOrAssign<u32> for Bits {
    fn bitor_assign(&mut self, rhs: u32) {
        self.0 |= rhs;
    }
}

fn main() {
    let mut value = Bits(3);
    value <<= 2u8;
    value |= 1u32;
    assert_eq!(value.0, 13);
}
