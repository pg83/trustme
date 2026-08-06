// Extracted from library/core/src/str/mod.rs:2027
#![allow(unused)]
fn main() {
    let v: Vec<&str> = "abcXXXabcYYYabc".rmatches("abc").collect();
    assert_eq!(v, ["abc", "abc", "abc"]);
    
    let v: Vec<&str> = "1abc2abc3".rmatches(char::is_numeric).collect();
    assert_eq!(v, ["3", "2", "1"]);
}
