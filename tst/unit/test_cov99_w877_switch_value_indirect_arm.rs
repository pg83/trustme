//@ edition: 2021
//@ compile-flags: -O -Cpanic=abort -Z dump-mir

fn dispatch(tag: u8, operation: fn(u32) -> u32, value: u32) -> u32 {
    match tag {
        2 => operation(value),
        7 => value ^ 3,
        _ => 41,
    }
}

fn increment(value: u32) -> u32 {
    value + 1
}

fn main() {
    assert_eq!(dispatch(2, increment, 10), 11);
    assert_eq!(dispatch(7, increment, 10), 9);
    assert_eq!(dispatch(0, increment, 10), 41);
}
