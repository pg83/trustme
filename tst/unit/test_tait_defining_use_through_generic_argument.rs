//@ edition: 2021
#![feature(type_alias_impl_trait)]

// In its defining scope an opaque type is the inference variable of its
// hidden type: upstream replaces every opaque in the signature by a fresh
// variable before the body is checked (`replace_opaque_types_with_inference_vars`),
// so `Thunk::new(closure)` against the expected `Thunk<Tait>` unifies the
// method's `F` with that variable, the closure becomes the hidden type, and
// `F: FnOnce(u32) -> u32` is checked on the closure.  Binding `F` to the
// opaque type itself instead asks `impl ContFn: FnOnce(u32) -> u32`, which
// nothing answers.

struct Thunk<F>(F);

impl<F> Thunk<F> {
    fn new(f: F) -> Self
    where
        F: FnOnce(u32) -> u32,
    {
        Thunk(f)
    }
}

trait ContFn {}

impl<F: FnOnce(u32) -> u32> ContFn for F {}

type Tait = impl ContFn;

#[define_opaque(Tait)]
fn reify_as_tait() -> Thunk<Tait> {
    Thunk::new(|c| c + 1)
}

fn main() {
    let _ = reify_as_tait();
}
