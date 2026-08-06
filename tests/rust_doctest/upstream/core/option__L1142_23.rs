// Extracted from library/core/src/option.rs:1142
#![allow(unused)]
fn main() {
    let maybe_some_string = Some(String::from("Hello, World!"));
    // `Option::map` takes self *by value*, consuming `maybe_some_string`
    let maybe_some_len = maybe_some_string.map(|s| s.len());
    assert_eq!(maybe_some_len, Some(13));
    
    let x: Option<&str> = None;
    assert_eq!(x.map(|s| s.len()), None);
}
