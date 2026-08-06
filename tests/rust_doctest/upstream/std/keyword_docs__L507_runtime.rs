// Extracted from library/std/src/keyword_docs.rs:507
#![allow(unused)]
fn main() {
    fn generic_function<T: Clone>(x: T) -> (T, T, T) {
        (x.clone(), x.clone(), x.clone())
    }
    
    fn generic_where<T>(x: T) -> T
        where T: std::ops::Add<Output = T> + Copy
    {
        x + x + x
    }
}
