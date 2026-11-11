fn classify(value: i64) -> u8 {
    match value {
        i64::MIN..=-129 => 3,
        -128..=-1 => 5,
        0 => 7,
        1..=255 => 11,
        _ => 13,
    }
}

fn main() {
    for (value, expected) in [
        (i64::MIN, 3),
        (-129, 3),
        (-128, 5),
        (-1, 5),
        (0, 7),
        (255, 11),
        (256, 13),
        (i64::MAX, 13),
    ] {
        assert_eq!(classify(core::hint::black_box(value)), expected);
    }
    let small = 2.25f32;
    let large = 3.5f64;
    let zero = 0.0f64;
    assert_eq!((-small * 4.0) as i8, -9);
    assert_eq!((-large * 2.0) as i16, -7);
    assert_eq!((-zero).to_bits(), 1u64 << 63);
    assert!(255u8 > 128u8);
    assert!(u64::MAX > i64::MAX as u64);
}
