// Extracted from library/core/src/any.rs:385
#![allow(unused)]
fn main() {
    use std::any::Any;
    
    fn modify_if_u32(s: &mut (dyn Any + Send)) {
        if let Some(num) = s.downcast_mut::<u32>() {
            *num = 42;
        }
    }
    
    let mut x = 10u32;
    let mut s = "starlord".to_string();
    
    modify_if_u32(&mut x);
    modify_if_u32(&mut s);
    
    assert_eq!(x, 42);
    assert_eq!(&s, "starlord");
}
