#![crate_type = "dylib"]
#![crate_type = "cdylib"]
#![crate_type = "proc-macro"]
#![crate_type = "staticlib"]
fn main() {
    assert_eq!(23u8 + 19, 42);
}
