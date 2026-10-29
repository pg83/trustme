#![feature(f16, f128)]
macro_rules! classifier {
    ($name:ident, $ty:ty) => {
        fn $name(value: $ty, guard: bool) -> u8 {
            match (value, guard) {
                (-0.0, true) if guard => 1,
                (-1.25..=1.25, _) if guard => 2,
                (2.0..3.0, _) if guard => 3,
                _ => 4,
            }
        }
    };
}
classifier!(half, f16);
classifier!(single, f32);
classifier!(double, f64);
classifier!(quad, f128);
macro_rules! verify {
    ($f:ident, $ty:ty) => {
        for (value, expected) in [(0.0, 1), (-0.0, 1), (-1.25, 2), (1.25, 2), (2.0, 3), (3.0, 4)] {
            assert_eq!($f(std::hint::black_box(value as $ty), true), expected);
            assert_eq!($f(std::hint::black_box(value as $ty), false), 4);
        }
        assert_eq!($f(<$ty>::NAN, true), 4);
        assert_eq!($f(<$ty>::INFINITY, true), 4);
        assert_eq!($f(<$ty>::NEG_INFINITY, true), 4);
    };
}
fn main() { verify!(half, f16); verify!(single, f32); verify!(double, f64); verify!(quad, f128); }
