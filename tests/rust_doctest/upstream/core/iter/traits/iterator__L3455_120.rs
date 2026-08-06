// Extracted from library/core/src/iter/traits/iterator.rs:3455
#![allow(unused)]
fn main() {
    let a = [vec![0_u8, 1, 2], vec![3, 4], vec![23]];
    // don't do this:
    let slower: Vec<_> = a.iter().cloned().filter(|s| s.len() == 1).collect();
    assert_eq!(&[vec![23]], &slower[..]);
    // instead call `cloned` late
    let faster: Vec<_> = a.iter().filter(|s| s.len() == 1).cloned().collect();
    assert_eq!(&[vec![23]], &faster[..]);
}
