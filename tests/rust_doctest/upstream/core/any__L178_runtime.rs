// Extracted from library/core/src/any.rs:178
#![allow(unused)]
fn main() {
    use std::any::Any;
    
    fn is_string(s: &dyn Any) {
        if s.is::<String>() {
            println!("It's a string!");
        } else {
            println!("Not a string...");
        }
    }
    
    is_string(&0);
    is_string(&"cookie monster".to_string());
}
