// Extracted from library/alloc/src/boxed/convert.rs:468
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::any::Any;

    fn print_if_string(value: Box<dyn Any + Send + Sync>) {
        if let Ok(string) = value.downcast::<String>() {
            println!("String ({}): {}", string.len(), string);
        }
    }

    let my_string = "Hello World".to_string();
    print_if_string(Box::new(my_string));
    print_if_string(Box::new(0i8));
}
