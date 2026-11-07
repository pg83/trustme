use std::ops::{Add, Deref};

struct Wrapped(u16);

impl Deref for Wrapped {
    type Target = u16;

    fn deref(&self) -> &u16 {
        &self.0
    }
}

impl Add for Wrapped {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Wrapped(self.0 + other.0)
    }
}

fn main() {
    let value = Wrapped(127);
    let borrowed = &value;
    let pointer = &**borrowed as *const u16;
    assert_eq!(**borrowed, 127);
    assert_eq!(unsafe { *pointer }, 127);

    let sum: u16 = **borrowed + unsafe { *pointer };
    assert_eq!(sum, 254);

    let combined = value + Wrapped(131);
    assert_eq!(*combined, 258);
}
