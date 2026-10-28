const LOW: f64 = f64::NEG_INFINITY;
const HIGH: f64 = f64::INFINITY;

#[inline(never)]
fn classify(value: f64, guard: bool) -> u8 {
    match value {
        0.0 if guard => 1,
        -2.0..=2.0 if guard => 2,
        LOW..0.0 => 3,
        0.0..=HIGH => 4,
        _ => 5,
    }
}

#[inline(never)]
fn narrow(value: f32, guard: bool) -> u8 {
    match value {
        1.5 if guard => 6,
        -1.0..1.0 if guard => 7,
        _ => 8,
    }
}

#[inline(never)]
fn unguarded(value: f64) -> u8 {
    match value {
        LOW..0.0 => 1,
        0.0..HIGH => 2,
        HIGH => 3,
        _ => 4,
    }
}

#[inline(never)]
fn all_ordered(value: f64, guard: bool) -> bool {
    match value { LOW..=HIGH if guard => true, _ => false }
}

fn main() {
    for (value, guard, expected) in [
        (0.0, true, 1), (-0.0, true, 1), (2.0, true, 2),
        (-3.0, true, 3), (LOW, false, 3), (HIGH, false, 4),
        (0.0, false, 4), (f64::NAN, true, 5),
    ] {
        assert_eq!(classify(std::hint::black_box(value), guard), expected);
    }
    assert_eq!(narrow(std::hint::black_box(1.5), true), 6);
    assert_eq!(narrow(std::hint::black_box(0.5), true), 7);
    assert_eq!(narrow(std::hint::black_box(1.0), true), 8);
    assert_eq!(narrow(std::hint::black_box(f32::NAN), true), 8);
    for (value, expected) in [(LOW, 1), (-1.0, 1), (0.0, 2), (1.0, 2), (HIGH, 3), (f64::NAN, 4)] {
        let value = std::hint::black_box(value);
        assert_eq!(unguarded(value), expected);
        assert_eq!(all_ordered(value, true), !value.is_nan());
        assert!(!all_ordered(value, false));
    }
}
