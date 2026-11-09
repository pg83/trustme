enum Void {}

fn eliminate(value: Void) -> u8 {
    match value {
        #[cfg(any())]
        _ => 1,
    }
}

#[inline(never)]
fn classify(values: &[u8]) -> u8 {
    match values {
        [1 | 2, .., 3 | 4] => 7,
        [_, ..] => 9,
        [] => 0,
    }
}

#[inline(never)]
fn duplicate(value: u8) -> u8 {
    match value {
        5 | 5 => 11,
        _ => 13,
    }
}

fn main() {
    let _ = std::hint::black_box(eliminate as fn(Void) -> u8);
    assert_eq!(classify(&[1, 3]), 7);
    assert_eq!(classify(&[1, 0, 4]), 7);
    assert_eq!(classify(&[2, 3]), 7);
    assert_eq!(classify(&[2, 0, 4]), 7);
    assert_eq!(classify(&[8]), 9);
    assert_eq!(classify(&[]), 0);
    assert_eq!(duplicate(5), 11);
    assert_eq!(duplicate(6), 13);
}
