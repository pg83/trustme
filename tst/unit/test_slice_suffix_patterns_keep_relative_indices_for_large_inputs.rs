fn classify(values: &[u16]) -> u16 {
    match values {
        [first, .., last] if first != last => *last,
        [single] => *single + 1,
        _ => 0,
    }
}
fn main() {
    let mut values = [1_u16; 40000];
    values[39999] = 37;
    assert_eq!(classify(&values), 37);
    assert_eq!(classify(&values[100..]), 37);
    assert_eq!(classify(&[13]), 14);
    assert_eq!(classify(&[5, 5]), 0);
    assert_eq!(classify(&[]), 0);
}
