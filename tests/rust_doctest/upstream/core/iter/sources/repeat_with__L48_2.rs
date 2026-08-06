// Extracted from library/core/src/iter/sources/repeat_with.rs:48
#![allow(unused)]
fn main() {
    use std::iter;
    
    // From the zeroth to the third power of two:
    let mut curr = 1;
    let mut pow2 = iter::repeat_with(|| { let tmp = curr; curr *= 2; tmp })
                        .take(4);
    
    assert_eq!(Some(1), pow2.next());
    assert_eq!(Some(2), pow2.next());
    assert_eq!(Some(4), pow2.next());
    assert_eq!(Some(8), pow2.next());
    
    // ... and now we're done
    assert_eq!(None, pow2.next());
}
