/* arbitrary's `int_in_range_impl`: `end.wrapping_sub(start)` on `T::Unsigned`, a method
   of the `Int` bound the associated type declares (`type Unsigned: Int`). */
pub trait Int: Copy + PartialOrd + std::fmt::Debug {
    type Unsigned: Int;
    const ZERO: Self;
    fn wrapping_sub(self, rhs: Self) -> Self;
    fn to_unsigned(self) -> Self::Unsigned;
    fn from_unsigned(u: Self::Unsigned) -> Self;
}

impl Int for u8 {
    type Unsigned = u8;
    const ZERO: Self = 0;
    fn wrapping_sub(self, rhs: Self) -> Self {
        u8::wrapping_sub(self, rhs)
    }
    fn to_unsigned(self) -> u8 {
        self
    }
    fn from_unsigned(u: u8) -> Self {
        u
    }
}

impl Int for i8 {
    type Unsigned = u8;
    const ZERO: Self = 0;
    fn wrapping_sub(self, rhs: Self) -> Self {
        i8::wrapping_sub(self, rhs)
    }
    fn to_unsigned(self) -> u8 {
        self as u8
    }
    fn from_unsigned(u: u8) -> Self {
        u as i8
    }
}

fn delta<T: Int>(start: T, end: T) -> T {
    if start == end {
        return T::ZERO;
    }
    let start = start.to_unsigned();
    let end = end.to_unsigned();
    let delta = end.wrapping_sub(start);
    debug_assert_ne!(delta, T::Unsigned::ZERO);
    T::from_unsigned(delta)
}

fn main() {
    assert_eq!(delta(3u8, 10u8), 7);
    assert_eq!(delta(-1i8, 2i8), 3);
    assert_eq!(delta(5u8, 5u8), 0);
}
