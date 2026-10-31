fn classify(value: bool, allow: bool, visits: &mut u32) -> u8 {
    match value {
        false if { *visits += 1; allow } => 7,
        true if { *visits += 10; allow } => 11,
        false => 13,
        true => 17,
    }
}
fn main() {
    let mut visits = 0;
    assert_eq!(classify(false, false, &mut visits), 13);
    assert_eq!(classify(false, true, &mut visits), 7);
    assert_eq!(classify(true, false, &mut visits), 17);
    assert_eq!(classify(true, true, &mut visits), 11);
    assert_eq!(visits, 22);
}
