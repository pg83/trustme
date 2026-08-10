// Extracted from library/alloc/src/fmt.rs:333
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(format!("Hello {{}}"), "Hello {}");
    assert_eq!(format!("{{ Hello"), "{ Hello");
}
