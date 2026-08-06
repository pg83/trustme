// Extracted from library/core/src/pin.rs:1389
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    
    struct Type {}
    impl Type {
        fn method(self: Pin<&mut Self>) {
            // do something
        }
    
        fn call_method_twice(mut self: Pin<&mut Self>) {
            // `method` consumes `self`, so reborrow the `Pin<&mut Self>` via `as_mut`.
            self.as_mut().method();
            self.as_mut().method();
        }
    }
}
