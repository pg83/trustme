#![feature(never_patterns)]
enum Empty {}
fn absent(value: Option<Empty>) -> u32 {
    match value { Some(!), None => 521 }
}
fn guarded(value: u32) -> u32 {
    match value {
        #[allow(unreachable_patterns)]
        | 0 | 1 if value == 1 => 523,
        0 => 541,
        other => { other + 547 }
    }
}
fn main() {
    assert_eq!(absent(None), 521);
    assert_eq!(guarded(0), 541);
    assert_eq!(guarded(1), 523);
    assert_eq!(guarded(2), 549);
}
