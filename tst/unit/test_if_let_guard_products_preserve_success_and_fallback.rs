#![feature(if_let_guard)]
fn select(value: (u8, Option<u8>)) -> u8 {
    match value {
        (head, Some(1 | 2)) if let Some(extra) = head.checked_add(9) => extra,
        (head, Some(tail)) if head > tail => head - tail,
        _ => 0,
    }
}
fn main() {
    assert_eq!(select((17, Some(2))), 26);
    assert_eq!(select((250, Some(1))), 249);
    assert_eq!(select((23, Some(5))), 18);
    assert_eq!(select((3, Some(8))), 0);
    assert_eq!(select((17, None)), 0);
}
