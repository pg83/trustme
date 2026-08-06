// Extracted from library/core/src/iter/traits/iterator.rs:161
#![allow(unused)]
fn main() {
    // The even numbers in the range of zero to nine.
    let iter = (0..10).filter(|x| x % 2 == 0);
    
    // We might iterate from zero to ten times. Knowing that it's five
    // exactly wouldn't be possible without executing filter().
    assert_eq!((0, Some(10)), iter.size_hint());
    
    // Let's add five more numbers with chain()
    let iter = (0..10).filter(|x| x % 2 == 0).chain(15..20);
    
    // now both bounds are increased by five
    assert_eq!((5, Some(15)), iter.size_hint());
}
