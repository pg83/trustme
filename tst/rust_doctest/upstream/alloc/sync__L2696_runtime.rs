// Extracted from library/alloc/src/sync.rs:2696
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::any::Any;
    use std::sync::Arc;

    fn print_if_string(value: Arc<dyn Any + Send + Sync>) {
        if let Ok(string) = value.downcast::<String>() {
            println!("String ({}): {}", string.len(), string);
        }
    }

    let my_string = "Hello World".to_string();
    print_if_string(Arc::new(my_string));
    print_if_string(Arc::new(0i8));
}
