// `ExactSizeIterator::len` is visible through its blanket `Box<I>` impl while
// the inner type is still `_`; its `I: ExactSizeIterator` bound must not hide
// the inherent method that becomes visible after inference and autoderef.
struct Map<K, V>(K, V);

fn make<K, V>(key: K, value: V) -> Map<K, V> {
    Map(key, value)
}

impl<K, V> Map<K, V> {
    fn len(&mut self) -> usize {
        0
    }
}

fn main() {
    let mut map: Box<_> = Box::new(make::<(), ()>((), ()));
    assert_eq!(map.len(), 0);
}
