// Extracted from library/core/src/any.rs:495
#![allow(unused)]
fn main() {
    use std::any::Any;

    fn print_if_string(s: &(dyn Any + Send + Sync)) {
        if let Some(string) = s.downcast_ref::<String>() {
            println!("It's a string({}): '{}'", string.len(), string);
        } else {
            println!("Not a string...");
        }
    }

    print_if_string(&0);
    print_if_string(&"cookie monster".to_string());
}
