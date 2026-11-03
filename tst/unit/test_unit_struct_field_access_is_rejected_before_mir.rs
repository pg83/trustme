//@ compile-fail: Couldn't find the field
struct Signal;
fn main() {
    let value = Signal;
    let _ = value.missing;
}
