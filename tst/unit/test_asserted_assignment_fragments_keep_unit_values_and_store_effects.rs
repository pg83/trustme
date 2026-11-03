macro_rules! unit_value {
    ($value:expr) => { assert!($value == ()); };
}
fn main() {
    let mut value = 0u16;
    unit_value!(value = 347);
    assert_eq!(value, 347);
    unit_value!(value += 2);
    assert_eq!(value, 349);
    unit_value!(value ^= 1);
    assert_eq!(value, 348);
}
