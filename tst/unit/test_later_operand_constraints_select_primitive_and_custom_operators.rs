use std::ops::Add;

fn default_value<T: Default>() -> T {
    T::default()
}

struct Total(u16);

impl Add<u8> for Total {
    type Output = u16;

    fn add(self, rhs: u8) -> u16 {
        self.0 + rhs as u16 + 1
    }
}

impl Add<u16> for Total {
    type Output = u32;

    fn add(self, rhs: u16) -> u32 {
        self.0 as u32 + rhs as u32 + 9
    }
}

fn main() {
    let right = default_value();
    let sum = 607u16 + right;
    let _: u16 = right;
    assert_eq!(sum, 607);

    let shift = default_value();
    let shifted = 617u32 << shift;
    let _: u8 = shift;
    assert_eq!(shifted, 617);

    let addend = default_value();
    let total = Total(631) + addend;
    let _: u16 = addend;
    assert_eq!(total, 640u32);
}
