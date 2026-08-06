// Extracted from library/core/src/clone.rs:334
#![allow(unused)]
#![feature(clone_to_uninit)]
fn main() {
    use std::rc::Rc;
    
    trait Foo: std::fmt::Debug + std::clone::CloneToUninit {
        fn modify(&mut self);
        fn value(&self) -> i32;
    }
    
    impl Foo for i32 {
        fn modify(&mut self) {
            *self *= 10;
        }
        fn value(&self) -> i32 {
            *self
        }
    }
    
    let first: Rc<dyn Foo> = Rc::new(1234);
    
    let mut second = first.clone();
    Rc::make_mut(&mut second).modify(); // make_mut() will call clone_to_uninit()
    
    assert_eq!(first.value(), 1234);
    assert_eq!(second.value(), 12340);
}
