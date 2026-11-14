//@ compile-flags: -Cpanic=abort
#![feature(guard_patterns)]

fn select(value: u32) -> u32 {
    match value {
        saved @ (_ if value == 9) => saved,
        _ => 0,
    }
}

fn main() {
    assert_eq!(select(9), 9);
    assert_eq!(select(4), 0);
}
