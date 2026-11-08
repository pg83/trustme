//@ compile-fail: only valid in expressions on the left-hand side of an assignment
fn main() {
    let value: u8 = _;
    let _ = value;
}
