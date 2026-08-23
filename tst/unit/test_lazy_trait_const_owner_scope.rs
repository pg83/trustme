trait Numbers {
    const BITS: usize = 1;
    const SIZE: usize = Self::BITS;
}

impl Numbers for u8 {}

struct Use([u8; <u8 as Numbers>::SIZE]);

fn main() {
    let value = Use([0]);
    assert_eq!(value.0[0], 0);
}
