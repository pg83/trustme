//@ aux-build: delegated_semantic_signatures.rs
//@ compile-variant: -O
use delegated_semantic_signatures::{Signatures, Source};
struct Wrapped(Source);
impl Signatures for Wrapped {
    fn values(&self, first: u8) -> impl Iterator<Item = u8> + Clone { self.0.values(first) }
}
fn main() {
    let values = Wrapped(Source).values(19);
    assert_eq!(values.clone().sum::<u8>(), 39);
    assert_eq!(values.collect::<Vec<_>>(), [19, 20]);
}
