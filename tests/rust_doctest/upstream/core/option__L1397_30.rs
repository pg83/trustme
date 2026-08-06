// Extracted from library/core/src/option.rs:1397
#![allow(unused)]
fn main() {
    let mut x: Option<String> = Some("hey".to_owned());
    assert_eq!(x.as_deref_mut().map(|x| {
        x.make_ascii_uppercase();
        x
    }), Some("HEY".to_owned().as_mut_str()));
}
