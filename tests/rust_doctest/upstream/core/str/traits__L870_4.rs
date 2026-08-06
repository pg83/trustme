// Extracted from library/core/src/str/traits.rs:870
#![allow(unused)]
fn main() {
    use std::str::FromStr;
    
    assert_eq!(FromStr::from_str("true"), Ok(true));
    assert_eq!(FromStr::from_str("false"), Ok(false));
    assert!(<bool as FromStr>::from_str("not even a boolean").is_err());
}
