// Extracted from library/core/src/str/mod.rs:2108
#![allow(unused)]
fn main() {
    let v: Vec<_> = "abcXXXabcYYYabc".rmatch_indices("abc").collect();
    assert_eq!(v, [(12, "abc"), (6, "abc"), (0, "abc")]);
    
    let v: Vec<_> = "1abcabc2".rmatch_indices("abc").collect();
    assert_eq!(v, [(4, "abc"), (1, "abc")]);
    
    let v: Vec<_> = "ababa".rmatch_indices("aba").collect();
    assert_eq!(v, [(2, "aba")]); // only the last `aba`
}
