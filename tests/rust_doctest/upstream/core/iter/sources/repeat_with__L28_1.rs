// Extracted from library/core/src/iter/sources/repeat_with.rs:28
#![allow(unused)]
fn main() {
    use std::iter;
    
    // let's assume we have some value of a type that is not `Clone`
    // or which we don't want to have in memory just yet because it is expensive:
    #[derive(PartialEq, Debug)]
    struct Expensive;
    
    // a particular value forever:
    let mut things = iter::repeat_with(|| Expensive);
    
    assert_eq!(Some(Expensive), things.next());
    assert_eq!(Some(Expensive), things.next());
    assert_eq!(Some(Expensive), things.next());
    assert_eq!(Some(Expensive), things.next());
    assert_eq!(Some(Expensive), things.next());
}
