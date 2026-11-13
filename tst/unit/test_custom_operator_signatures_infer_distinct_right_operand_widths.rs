use std::ops::{Add, Shl};

struct Amount(u16);

impl Add<u16> for Amount {
    type Output = Self;

    fn add(self, rhs: u16) -> Self {
        Amount(self.0 + rhs)
    }
}

impl Shl<u8> for Amount {
    type Output = u16;

    fn shl(self, rhs: u8) -> u16 {
        self.0 << rhs
    }
}

fn main() {
    let value = Amount(593) + 7;
    assert_eq!(value.0, 600);
    assert_eq!(value << 2, 2400);
}
