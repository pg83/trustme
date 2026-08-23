use std::mem::size_of;

enum Digits {
    Inline(Option<u64>),
    Heap(Vec<u64>),
}

fn main() {
    assert!(size_of::<Option<Digits>>() > size_of::<Digits>());

    let mut value: Option<Digits> = None;
    value = Some(Digits::Inline(None));
    assert!(matches!(value, Some(Digits::Inline(None))));

    let heap = Some(Digits::Heap(vec![1, 2]));
    assert!(matches!(heap, Some(Digits::Heap(values)) if values == [1, 2]));
}
