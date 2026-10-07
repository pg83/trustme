//@ aux-build: shared_generic_instance.rs
// At opt-level 0 rustc shares generic instances between crates (`-Z
// share-generics`): a crate that instantiates `identity::<u32>` exports it,
// and a crate depending on it links to that instance instead of making its
// own. A function pointer to the instance therefore has one address whichever
// crate takes it. Every crate used to emit a private copy of each instance
// it named, so the two pointers differed.
extern crate shared_generic_instance;

fn main() {
    let downstream: fn(u32) -> u32 = shared_generic_instance::identity::<u32>;
    assert_eq!(downstream(3), 3);
    assert!(downstream as usize == shared_generic_instance::upstream_pointer() as usize);
}
