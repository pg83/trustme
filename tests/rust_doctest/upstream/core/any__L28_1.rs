// Extracted from library/core/src/any.rs:28
#![allow(unused)]
fn main() {
    use std::any::{Any, TypeId};
    
    let boxed: Box<dyn Any> = Box::new(3_i32);
    
    // You're more likely to want this:
    let actual_id = (&*boxed).type_id();
    // ... than this:
    let boxed_id = boxed.type_id();
    
    assert_eq!(actual_id, TypeId::of::<i32>());
    assert_eq!(boxed_id, TypeId::of::<Box<dyn Any>>());
}
