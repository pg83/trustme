fn tail(values: &[u16; 32770]) -> (u16, u16) {
    match values { [.., before, last] => (*before, *last) }
}
fn classify(values: &[u16; 32770]) -> u8 {
    match values { [.., 37, 41] => 7, [.., 40..=42] => 11, _ => 13 }
}
fn main() {
    let mut values = [0_u16; 32770];
    values[32768] = 37;
    values[32769] = 41;
    assert_eq!(tail(&values), (37, 41));
    assert_eq!(classify(&values), 7);
    values[32768] = 19;
    assert_eq!(classify(&values), 11);
    values[32769] = 43;
    assert_eq!(classify(&values), 13);
}
