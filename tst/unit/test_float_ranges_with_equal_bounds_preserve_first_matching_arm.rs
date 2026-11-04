//@ compile-variant: -O
#![feature(f16, f128)]

macro_rules! check_precision {
    ($module:ident, $ty:ty) => {
        mod $module {
            #[inline(never)]
            fn classify(value: $ty) -> u8 {
                match value {
                    1.0..3.0 => 11,
                    1.0..=3.0 => 13,
                    1.0..=4.0 => 17,
                    _ => 19,
                }
            }

            pub fn check() {
                for (value, expected) in [
                    (0.0, 19), (-0.0, 19), (1.0, 11), (2.0, 11),
                    (3.0, 13), (3.5, 17), (4.0, 17), (4.25, 19),
                ] {
                    assert_eq!(classify(std::hint::black_box(value)), expected);
                }
                assert_eq!(classify(<$ty>::NAN), 19);
            }
        }
    };
}

check_precision!(half, f16);
check_precision!(single, f32);
check_precision!(double, f64);
check_precision!(quad, f128);

fn main() {
    half::check();
    single::check();
    double::check();
    quad::check();
}
