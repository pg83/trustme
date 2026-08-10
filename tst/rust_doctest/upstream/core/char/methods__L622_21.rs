// Extracted from library/core/src/char/methods.rs:622
#![allow(unused)]
fn main() {
    // as chars
    let eastern = '東';
    let capital = '京';

    // both can be represented as three bytes
    assert_eq!(3, eastern.len_utf8());
    assert_eq!(3, capital.len_utf8());

    // as a &str, these two are encoded in UTF-8
    let tokyo = "東京";

    let len = eastern.len_utf8() + capital.len_utf8();

    // we can see that they take six bytes total...
    assert_eq!(6, tokyo.len());

    // ... just like the &str
    assert_eq!(len, tokyo.len());
}
