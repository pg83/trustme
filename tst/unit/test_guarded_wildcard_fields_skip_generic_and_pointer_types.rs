fn classify<T>(value: T, pointer: *const T, function: fn() -> u8, flag: bool) -> u8 {
    match (value, pointer, function, flag) { (_, _, _, false) if function() == 37 => 7, (_, _, _, true) => 11, _ => 13 }
}
fn value() -> u8 { 37 }
fn main() {
    let number = 41;
    assert_eq!(classify(number, &number, value, false), 7);
    assert_eq!(classify(number, &number, value, true), 11);
    assert_eq!(classify(number, &number, || 19, false), 13);
}
