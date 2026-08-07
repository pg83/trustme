// Extracted from library/core/src/option.rs:2288
#![allow(unused)]
fn main() {
    let mut s = Some(String::from("Hello"));
    let o: Option<&mut String> = Option::from(&mut s);

    match o {
        Some(t) => *t = String::from("Hello, Rustaceans!"),
        None => (),
    }

    assert_eq!(s, Some(String::from("Hello, Rustaceans!")));
}
