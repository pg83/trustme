// Extracted from library/core/src/option.rs:61
#![allow(unused)]
fn main() {
    let optional = None;
    check_optional(optional);
    
    let optional = Some(Box::new(9000));
    check_optional(optional);
    
    fn check_optional(optional: Option<Box<i32>>) {
        match optional {
            Some(p) => println!("has value {p}"),
            None => println!("has no value"),
        }
    }
}
