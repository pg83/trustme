macro_rules! condition {
    ($value:expr) => { assert!($value && true); };
}
fn returning() -> u16 {
    condition!(return 367);
    0
}
fn returning_unit() { condition!(return); }
fn main() {
    assert_eq!(returning(), 367);
    returning_unit();
    let value = 'done: loop { condition!(break 'done 373u16); };
    assert_eq!(value, 373);
    let mut visits = 0u16;
    'once: loop {
        visits += 1;
        condition!(break 'once);
    }
    assert_eq!(visits, 1);
}
