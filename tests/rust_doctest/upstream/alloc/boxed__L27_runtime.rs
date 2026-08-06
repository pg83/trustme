// Extracted from library/alloc/src/boxed.rs:27
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        #[allow(dead_code)]
        #[derive(Debug)]
        enum List<T> {
            Cons(T, Box<List<T>>),
            Nil,
        }
        
        let list: List<i32> = List::Cons(1, Box::new(List::Cons(2, Box::new(List::Nil))));
        println!("{list:?}");
        Ok(())
    }
    doctest().unwrap();
}
