//@ compile-flags: -Z dump-mir -C panic=abort
//@ output-contains: .cpp case INT64_MIN:

#[inline(never)]
fn classify(value: i64) -> u32 {
    match value {
        i64::MIN => 11,
        -7 => 23,
        19 => 37,
        _ => 41,
    }
}

fn main() {
    assert_eq!(classify(i64::MIN), 11);
    assert_eq!(classify(-7), 23);
    assert_eq!(classify(19), 37);
    assert_eq!(classify(0), 41);
    assert_eq!(classify(i64::MAX), 41);
}
