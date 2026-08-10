// Extracted from library/core/src/iter/adapters/chain.rs:13
#![allow(unused)]
fn main() {
    use std::iter::Chain;
    use std::slice::Iter;

    let a1 = [1, 2, 3];
    let a2 = [4, 5, 6];
    let iter: Chain<Iter<'_, _>, Iter<'_, _>> = a1.iter().chain(a2.iter());
}
