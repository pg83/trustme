//@ edition: 2024
//@ compile-fail: cannot explicitly dereference
fn main() {
    let values = &[&7u8];
    let [&value] = values;
    let _ = value;
}
