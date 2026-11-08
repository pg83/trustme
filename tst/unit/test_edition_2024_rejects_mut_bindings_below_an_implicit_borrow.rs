//@ edition: 2024
//@ compile-fail: cannot bind
fn main() {
    let source = &[17u8];
    let [mut value] = source;
    value = 23;
    let _ = value;
}
