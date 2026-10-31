fn main() {
    let mut sum = 0u8;
    macro_rules! value { () => { 17u8 }; }
    [value!(), 23].iter().for_each(|number| sum += *number);
    assert_eq!(sum, 40);
    macro_rules! pair { () => { [31u8, 37] }; }
    let first = pair!{}[0];
    let second = pair!()[1];
    assert_eq!((first, second), (31, 37));
}
