// Extracted from library/core/src/option.rs:2182
#![allow(unused)]
fn main() {
    let opt: Option<u32> = Option::default();
    assert!(opt.is_none());
}
