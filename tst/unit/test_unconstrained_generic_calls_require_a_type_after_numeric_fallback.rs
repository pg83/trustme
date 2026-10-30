//@ compile-fail: Failed to infer type
fn unspecified<T>() -> T { loop {} }
fn main() {
    let _integer = 17;
    let _floating = 0.5;
    let _character = 65 as char;
    let empty = [];
    let _pointer = &empty as *const u16;
    let _value = unspecified();
}
