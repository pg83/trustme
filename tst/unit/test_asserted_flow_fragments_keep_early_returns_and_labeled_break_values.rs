//@ compile-fail: Failed to find an impl
macro_rules! condition {
    ($value:expr) => { assert!($value == true); };
}
fn returning() -> u16 {
    condition!(return 353);
    0
}
fn main() {
    assert_eq!(returning(), 353);
    let value = 'done: loop {
        condition!(break 'done 359u16);
    };
    assert_eq!(value, 359);
}
