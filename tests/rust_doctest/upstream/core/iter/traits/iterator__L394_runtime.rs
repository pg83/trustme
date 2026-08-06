// Extracted from library/core/src/iter/traits/iterator.rs:394
#![allow(unused)]
fn main() {
    fn advance_n_and_return_first<I>(iter: &mut I, n: usize) -> Option<I::Item>
    where
        I: Iterator,
    {
        let next = iter.next();
        if n > 1 {
            iter.nth(n - 2);
        }
        next
    }
}
