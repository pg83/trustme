// Extracted from src/types/closure.md:444
#![allow(unused)]
fn main() {
    struct T(String, String);
    
    let mut t = T(String::from("foo"), String::from("bar"));
    let t_mut_ref = &mut t;
    let mut c = move || {
        t_mut_ref.0.push_str("123"); // captures `t_mut_ref` ByValue
    };
    c();
}
