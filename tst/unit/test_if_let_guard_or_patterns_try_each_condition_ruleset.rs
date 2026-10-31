#![feature(if_let_guard)]
fn select(value: Option<u8>, condition: Option<u8>) -> u8 {
    match value {
        Some(value) if let Some(3 | 5) = condition => value + 1,
        Some(value) if value > 9 && condition.is_none() => value + 2,
        _ => 0,
    }
}
fn main() {
    assert_eq!(select(Some(17), Some(3)), 18);
    assert_eq!(select(Some(23), Some(5)), 24);
    assert_eq!(select(Some(37), None), 39);
    assert_eq!(select(Some(41), Some(7)), 0);
    assert_eq!(select(None, Some(5)), 0);
}
