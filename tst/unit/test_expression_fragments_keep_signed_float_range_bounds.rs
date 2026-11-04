//@ compile-variant: -O
macro_rules! band {
    ($name:ident, $ty:ty, $lower:expr, $upper:expr) => {
        fn $name(value: $ty, enabled: bool) -> u8 {
            match value { $lower..=$upper if enabled => 9, _ => 31 }
        }
    };
}

band!(narrow_band, f32, -3.25f32, 1.5f32);
band!(wide_band, f64, -6.5f64, 2.75f64);

fn main() {
    for enabled in [false, true] {
        for value in [-3.25f32, -0.0, 0.0, 1.5] {
            assert_eq!(narrow_band(value, enabled), if enabled { 9 } else { 31 });
        }
        for value in [-6.5f64, -0.0, 0.0, 2.75] {
            assert_eq!(wide_band(value, enabled), if enabled { 9 } else { 31 });
        }
    }
    for value in [f32::NEG_INFINITY, -4.0, 2.0, f32::INFINITY, f32::NAN] {
        assert_eq!(narrow_band(value, true), 31);
    }
    for value in [f64::NEG_INFINITY, -7.0, 3.0, f64::INFINITY, f64::NAN] {
        assert_eq!(wide_band(value, true), 31);
    }
}
