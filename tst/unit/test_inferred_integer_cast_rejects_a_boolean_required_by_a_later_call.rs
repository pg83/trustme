//@ compile-fail: cast
fn require_boolean(value: bool) {
    assert!(value);
}

fn main() {
    let converted = 1u8 as _;
    require_boolean(converted);
}
