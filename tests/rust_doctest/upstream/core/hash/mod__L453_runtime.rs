// Extracted from library/core/src/hash/mod.rs:453
#![allow(unused)]
#![feature(hasher_prefixfree_extras)]
#![allow(non_local_definitions)]
fn main() {
    // Stubs to make the `impl` below pass the compiler
    struct MyCollection<T>(Option<T>);
    impl<T> MyCollection<T> {
        fn len(&self) -> usize { todo!() }
    }
    impl<'a, T> IntoIterator for &'a MyCollection<T> {
        type Item = T;
        type IntoIter = std::iter::Empty<T>;
        fn into_iter(self) -> Self::IntoIter { todo!() }
    }

    use std::hash::{Hash, Hasher};
    impl<T: Hash> Hash for MyCollection<T> {
        fn hash<H: Hasher>(&self, state: &mut H) {
            state.write_length_prefix(self.len());
            for elt in self {
                elt.hash(state);
            }
        }
    }
}
