#[repr(i128)]
enum Balance {
    Debt(u8) = i128::MIN,
    Empty = 0,
    Credit(u16) = i128::MAX,
}
#[inline(never)]
fn amount(value: &Balance) -> i32 {
    match value {
        Balance::Debt(value) => -(*value as i32),
        Balance::Empty => 0,
        Balance::Credit(value) => *value as i32,
    }
}
fn main() {
    assert_eq!(amount(&std::hint::black_box(Balance::Debt(13))), -13);
    assert_eq!(amount(&std::hint::black_box(Balance::Empty)), 0);
    assert_eq!(amount(&std::hint::black_box(Balance::Credit(513))), 513);
}
