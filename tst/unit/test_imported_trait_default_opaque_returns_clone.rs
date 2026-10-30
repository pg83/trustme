//@ aux-build: opaque_default_sequence.rs
use opaque_default_sequence::{Factory, Source};
fn join<T: Iterator<Item = u8>>(first: T, second: T) -> Vec<u8> { first.chain(second).collect() }
fn main() {
    let first = <Source as Factory<u8, 2>>::sequence(11);
    assert_eq!(first.clone().sum::<u8>(), 22);
    assert_eq!(join(first, Source::sequence(19)), [11, 11, 19, 19]);
}
