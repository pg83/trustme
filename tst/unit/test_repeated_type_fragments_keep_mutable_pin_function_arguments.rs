#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

macro_rules! aliases {
    ($first:ident, $second:ident, $ty:ty) => {
        type $first = $ty;
        type $second = $ty;
    };
}

aliases!(First, Second, fn(&pin mut u16) -> u16);

fn increment(mut value: &pin mut u16) -> u16 {
    *value.as_mut().get_mut() += 7;
    *value
}

fn main() {
    let mut value = 10u16;
    let first: First = increment;
    let second: Second = increment;
    assert_eq!(first(&pin mut value), 17);
    assert_eq!(second(&pin mut value), 24);
    assert_eq!(value, 24);
}
