//@ compile-variant: -O
#![feature(f16, f128)]

macro_rules! check_precision {
    ($module:ident, $ty:ty) => {
        mod $module {
            #[inline(never)]
            pub fn classify(value: $ty) -> u8 {
                match value { 4.25 => 7, -2.5 => 11, 0.0 => 13, _ => 17 }
            }
            pub fn check() {
                for (value, expected) in [(4.25, 7), (-2.5, 11), (0.0, 13), (-0.0, 13), (-3.0, 17), (5.0, 17)] {
                    assert_eq!(classify(std::hint::black_box(value)), expected);
                }
                assert_eq!(classify(<$ty>::NAN), 17);
                assert_eq!(classify(<$ty>::INFINITY), 17);
                assert_eq!(classify(<$ty>::NEG_INFINITY), 17);
            }
        }
    };
}
check_precision!(half, f16);
check_precision!(single, f32);
check_precision!(double, f64);
check_precision!(quad, f128);

fn main() { half::check(); single::check(); double::check(); quad::check(); }
