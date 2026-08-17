//@ edition: 2024
// A `gen fn` body is a coroutine and the function hands back the iterator over
// it, so the declared return type is the item type.
#![feature(gen_blocks)]

gen fn counted() -> i32 {
    yield 1;
    yield 2;
    yield 3;
}

gen fn borrowed<'a, 'b>(_x: &'a i32, y: &'b i32, z: &'b i32) -> &'b i32 {
    yield y;
    yield z;
}

fn main() {
    let collected: Vec<i32> = counted().collect();
    assert_eq!(collected, [1, 2, 3]);

    let three = 3;
    let mut iter = borrowed(&1, &2, &three);
    assert_eq!(iter.next(), Some(&2));
    assert_eq!(iter.next(), Some(&3));
    assert_eq!(iter.next(), None);
}
