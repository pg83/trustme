#![feature(impl_trait_in_assoc_type)]

trait Stream {
    type Feed: Iterator;
    fn open() -> Self::Feed;
}

struct Source;

impl Stream for Source {
    type Feed = impl Iterator<Item: Copy + Eq> + Clone;

    fn open() -> Self::Feed {
        [1031u16, 1033].into_iter()
    }
}

fn bridge() -> <Source as Stream>::Feed {
    Source::open()
}

fn main() {
    let mut original = bridge();
    let first = original.next().unwrap();
    let mut cloned = original.clone();
    assert!(first != original.next().unwrap());
    assert!(cloned.next().is_some());
    assert!(original.next().is_none());
    assert!(cloned.next().is_none());
}
