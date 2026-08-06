// Extracted from library/std/src/thread/mod.rs:784
#![allow(unused)]
fn main() {
    use std::thread;
    
    struct SomeStruct;
    
    impl Drop for SomeStruct {
        fn drop(&mut self) {
            if thread::panicking() {
                println!("dropped while unwinding");
            } else {
                println!("dropped while not unwinding");
            }
        }
    }
    
    {
        print!("a: ");
        let a = SomeStruct;
    }
    
    {
        print!("b: ");
        let b = SomeStruct;
        panic!()
    }
}
