#![feature(trait_alias)]

trait Values = Iterator<Item: Copy + Eq>;
trait MoreValues = Values + Iterator<Item: Copy>;
trait ExactValues = MoreValues + Values + ExactSizeIterator;

fn first_pair<I: ExactValues>(mut values: I) -> (I::Item, I::Item, usize) {
    let first = values.next().unwrap();
    assert!(first == first);
    (first, first, values.len())
}

fn main() {
    assert_eq!(first_pair(5u16..9), (5, 5, 3));
    assert_eq!(first_pair([true, false].into_iter()), (true, true, 1));
}
