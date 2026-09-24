// Upstream's constituent types of `PhantomData<T>` for an auto trait are `T`
// (`constituent_types_for_auto_trait`); ours was a struct without fields and
// so `Send` and `Sync` for any `T`.
//@ compile-fail: Failed to find an impl
use std::marker::PhantomData;

fn need_sync<T: Sync>() {}

fn main() {
    need_sync::<PhantomData<*const ()>>();
}
