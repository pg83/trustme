// Extracted from library/core/src/option.rs:1374
#![allow(unused)]
fn main() {
    let x: Option<String> = Some("hey".to_owned());
    assert_eq!(x.as_deref(), Some("hey"));
    
    let x: Option<String> = None;
    assert_eq!(x.as_deref(), None);
}
