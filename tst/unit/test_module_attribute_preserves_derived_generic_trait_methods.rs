//@ proc-macro-aux-build: late_module_tokens.rs
#[late_module_tokens::keep_tree]
mod forest {
    #[derive(Clone, PartialEq, Eq, Debug, Hash)]
    pub struct Branch<T>(pub T, pub ());
    pub fn duplicate<T: Clone>(value: &Branch<T>) -> Branch<T> { value.clone() }
}
fn main() {
    use std::hash::{Hash, Hasher};
    let value = forest::Branch(113u32, ());
    let copy = forest::duplicate(&value);
    assert_eq!(value, copy);
    let mut hash = std::collections::hash_map::DefaultHasher::new();
    value.hash(&mut hash);
    assert_ne!(hash.finish(), 0);
}
