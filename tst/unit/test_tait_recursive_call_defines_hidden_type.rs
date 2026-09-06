//@ edition: 2021
#![feature(type_alias_impl_trait)]

// A recursive call inside the defining scope returns the opaque type, and
// there the opaque type is the hidden type's inference variable: `let x:
// Vec<i32> = foo(false)` settles it as `Vec<i32>`, and the final
// `collect()` with the opaque type expected collects into `Vec<i32>`.
// Binding the collect's target variable to the opaque type itself instead
// asks `impl Debug: FromIterator<_>`, which cannot be inferred.

type Foo = impl std::fmt::Debug;

#[define_opaque(Foo)]
fn foo(b: bool) -> Foo {
    if b {
        return vec![];
    }
    let x: Vec<i32> = foo(false);
    let _ = x;
    std::iter::empty().collect()
}

fn main() {
    let _ = foo(true);
}
