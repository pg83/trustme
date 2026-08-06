// Extracted from library/core/src/future/into_future.rs:34
#![allow(unused)]
fn main() {
    use std::future::{ready, Ready, IntoFuture};
    
    /// Eventually multiply two numbers
    pub struct Multiply {
        num: u16,
        factor: u16,
    }
    
    impl Multiply {
        /// Constructs a new instance of `Multiply`.
        pub fn new(num: u16, factor: u16) -> Self {
            Self { num, factor }
        }
    
        /// Set the number to multiply by the factor.
        pub fn number(mut self, num: u16) -> Self {
            self.num = num;
            self
        }
    
        /// Set the factor to multiply the number with.
        pub fn factor(mut self, factor: u16) -> Self {
            self.factor = factor;
            self
        }
    }
    
    impl IntoFuture for Multiply {
        type Output = u16;
        type IntoFuture = Ready<Self::Output>;
    
        fn into_future(self) -> Self::IntoFuture {
            ready(self.num * self.factor)
        }
    }
    
    // NOTE: Rust does not yet have an `async fn main` function, that functionality
    // currently only exists in the ecosystem.
    async fn run() {
        let num = Multiply::new(0, 0)  // initialize the builder to number: 0, factor: 0
            .number(2)                 // change the number to 2
            .factor(2)                 // change the factor to 2
            .await;                    // convert to future and .await
    
        assert_eq!(num, 4);
    }
}
