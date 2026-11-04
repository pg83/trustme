//@ compile-variant: -O
macro_rules! select {
    ($input:expr, $pattern:expr) => {
        match $input { $pattern => 17, _ => 29 }
    };
}

fn main() {
    assert_eq!(select!(std::hint::black_box(-7i16), -7i16), 17);
    assert_eq!(select!(std::hint::black_box(-8i16), -7i16), 29);
    assert_eq!(select!(std::hint::black_box(-2.75f32), -2.75f32), 17);
    assert_eq!(select!(std::hint::black_box(-1.25f32), -2.75f32), 29);
    assert_eq!(select!(std::hint::black_box(f32::NAN), -2.75f32), 29);
    assert_eq!(select!(std::hint::black_box(true), true), 17);
    assert_eq!(select!(std::hint::black_box(false), true), 29);
}
