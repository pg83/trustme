fn replace_tail(values: &mut [u8; 65536]) -> u8 {
    let [first, .., last] = values;
    *first = 3;
    let old = *last;
    *last = 37;
    old
}
fn main() {
    let mut values = [0_u8; 65536];
    values[65535] = 13;
    assert_eq!(replace_tail(&mut values), 13);
    assert_eq!((values[0], values[65534], values[65535]), (3, 0, 37));
}
