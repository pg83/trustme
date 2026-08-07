// Extracted from library/core/src/borrow.rs:63
#![allow(unused)]
fn main() {
    use std::borrow::Borrow;
    use std::hash::Hash;

    pub struct HashMap<K, V> {
        marker: ::std::marker::PhantomData<(K, V)>,
        // fields omitted
    }

    impl<K, V> HashMap<K, V> {
        pub fn insert(&self, key: K, value: V) -> Option<V>
        where K: Hash + Eq
        {
            unimplemented!()
            // ...
        }

        pub fn get<Q>(&self, k: &Q) -> Option<&V>
        where
            K: Borrow<Q>,
            Q: Hash + Eq + ?Sized
        {
            unimplemented!()
            // ...
        }
    }
}
