#![allow(dead_code)]

enum Void {}

enum Choice {
    Gone(Void),
    Value(bool),
    Empty,
}

#[inline(never)]
fn is_absent(value: &Choice) -> bool {
    match value {
        Choice::Gone(_) => true,
        _ => false,
    }
}

fn main() {
    assert!(!is_absent(&Choice::Value(false)));
    assert!(!is_absent(&Choice::Value(true)));
    assert!(!is_absent(&Choice::Empty));
}
