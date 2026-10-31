#[inline(never)]
fn classify(value: &[()]) -> u8 {
    match value { [(), (), .., ()] => 7, [(), ..] => 11, [] => 13 }
}
fn main() {
    assert_eq!(classify(&[]), 13);
    assert_eq!(classify(&[()]), 11);
    assert_eq!(classify(&[(), ()]), 11);
    assert_eq!(classify(&[(), (), ()]), 7);
    assert_eq!(classify(&[(), (), (), ()]), 7);
}
