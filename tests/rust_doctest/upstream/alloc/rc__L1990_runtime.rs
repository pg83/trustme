// Extracted from library/alloc/src/rc.rs:1990
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::any::Any;
    use std::rc::Rc;
    
    fn print_if_string(value: Rc<dyn Any>) {
        if let Ok(string) = value.downcast::<String>() {
            println!("String ({}): {}", string.len(), string);
        }
    }
    
    let my_string = "Hello World".to_string();
    print_if_string(Rc::new(my_string));
    print_if_string(Rc::new(0i8));
}
