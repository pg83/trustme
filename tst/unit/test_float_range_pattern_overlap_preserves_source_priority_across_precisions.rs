//@ compile-variant: -O
#![feature(f16, f128)]

macro_rules! check_precision {
    ($module:ident, $ty:ty) => {
        mod $module {
            #[inline(never)]
            fn inclusive(value: $ty) -> u8 {
                match value { 1.0..=3.0 => 1, -5.0..=-1.0 => 2, -2.0..=2.0 => 3, _ => 4 }
            }
            #[inline(never)]
            fn exclusive(value: $ty) -> u8 {
                match value { 2.0..4.0 => 11, -4.0..-2.0 => 13, -3.0..3.0 => 17, _ => 19 }
            }
            pub fn check() {
                for (value, expected) in [(1.0, 1), (1.5, 1), (3.0, 1), (-5.0, 2), (-1.5, 2), (-1.0, 2), (-0.5, 3), (0.0, 3), (-0.0, 3), (-6.0, 4), (3.5, 4)] {
                    assert_eq!(inclusive(std::hint::black_box(value)), expected);
                }
                for (value, expected) in [(2.0, 11), (3.0, 11), (4.0, 19), (-4.0, 13), (-3.0, 13), (-2.0, 17), (0.0, 17), (-0.0, 17), (-5.0, 19)] {
                    assert_eq!(exclusive(std::hint::black_box(value)), expected);
                }
                assert_eq!(inclusive(<$ty>::NAN), 4);
                assert_eq!(exclusive(<$ty>::NAN), 19);
            }
        }
    };
}
check_precision!(half, f16);
check_precision!(single, f32);
check_precision!(double, f64);
check_precision!(quad, f128);

#[inline(never)]
fn integer(value: i64) -> u8 {
    match value { 10..=20 => 21, -20..=-10 => 23, -12..=12 => 25, _ => 27 }
}

fn main() {
    half::check(); single::check(); double::check(); quad::check();
    for (value, expected) in [(10, 21), (20, 21), (-20, 23), (-10, 23), (0, 25), (-30, 27), (30, 27)] {
        assert_eq!(integer(std::hint::black_box(value)), expected);
    }
}
