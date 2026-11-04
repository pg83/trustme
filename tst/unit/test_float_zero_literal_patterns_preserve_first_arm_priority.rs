//@ compile-variant: -O
#![feature(f16, f128)]

macro_rules! check_precision {
    ($module:ident, $ty:ty) => {
        mod $module {
            #[inline(never)]
            fn negative_first(value: $ty) -> u8 {
                match value { -0.0 => 17, 0.0 => 19, _ => 23 }
            }
            #[inline(never)]
            fn positive_first(value: $ty) -> u8 {
                match value { 0.0 => 29, -0.0 => 31, _ => 37 }
            }
            pub fn check() {
                for value in [0.0, -0.0] {
                    assert_eq!(negative_first(std::hint::black_box(value)), 17);
                    assert_eq!(positive_first(std::hint::black_box(value)), 29);
                }
                assert_eq!(negative_first(1.0), 23);
                assert_eq!(positive_first(-1.0), 37);
                assert_eq!(negative_first(<$ty>::NAN), 23);
                assert_eq!(positive_first(<$ty>::NAN), 37);
            }
        }
    };
}
check_precision!(half, f16);
check_precision!(single, f32);
check_precision!(double, f64);
check_precision!(quad, f128);

fn main() { half::check(); single::check(); double::check(); quad::check(); }
