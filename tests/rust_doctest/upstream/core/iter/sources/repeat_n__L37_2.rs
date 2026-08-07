// Extracted from library/core/src/iter/sources/repeat_n.rs:37
#![allow(unused)]
fn main() {
    use std::iter;

    let v: Vec<i32> = Vec::with_capacity(123);
    let mut it = iter::repeat_n(v, 5);

    for i in 0..4 {
        // It starts by cloning things
        let cloned = it.next().unwrap();
        assert_eq!(cloned.len(), 0);
        assert_eq!(cloned.capacity(), 0);
    }

    // ... but the last item is the original one
    let last = it.next().unwrap();
    assert_eq!(last.len(), 0);
    assert_eq!(last.capacity(), 123);

    // ... and now we're done
    assert_eq!(None, it.next());
}
