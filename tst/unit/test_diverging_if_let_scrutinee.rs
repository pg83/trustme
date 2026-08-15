#![allow(irrefutable_let_patterns, unreachable_code)]

fn through_if_let() -> bool {
    if let _ = return true && false {};
}

fn main() {
    assert!(!through_if_let());
}
