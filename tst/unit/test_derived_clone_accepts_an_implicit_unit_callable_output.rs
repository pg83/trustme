//@ edition: 2015
#![allow(bare_trait_objects)]
#[derive(Clone)]
struct Callback(std::marker::PhantomData<Fn()>);
fn main() {
    let original = Callback(std::marker::PhantomData);
    let cloned = original.clone();
    assert_eq!(std::mem::size_of_val(&cloned), 0);
}
