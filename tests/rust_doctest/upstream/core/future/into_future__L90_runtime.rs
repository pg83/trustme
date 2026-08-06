// Extracted from library/core/src/future/into_future.rs:90
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::future::IntoFuture;
        
        /// Converts the output of a future to a string.
        async fn fut_to_string<Fut>(fut: Fut) -> String
        where
            Fut: IntoFuture,
            Fut::Output: std::fmt::Debug,
        {
            format!("{:?}", fut.await)
        }
        Ok(())
    }
    doctest().unwrap();
}
