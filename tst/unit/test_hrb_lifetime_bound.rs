// `for<'a: 'b>` bounds the lifetime the binder introduces. The bound list was
// not parsed at all, so the `:` after the lifetime was an unexpected token.
//
// rustc parses the form and rejects it afterwards ("bounds cannot be used in
// this context"), so the only way to exercise the parser on it is behind a
// `cfg` that removes the item before anything checks it.
//
// Same shape as the upstream test
// traits/const-traits/conditionally-const-trait-bound-syntax.rs.
#![allow(dead_code)]

trait Tr<'a> {}

#[cfg(false)]
struct WithBound<T>(T)
where
    for<'a: 'static> T: Tr<'a>;

#[cfg(false)]
fn takesFn<F>(_: F)
where
    F: for<'a: 'static> Fn(&'a u32) -> &'a u32,
{
}

// Several bounds on the one lifetime.
#[cfg(false)]
fn several<T>()
where
    for<'a: 'b + 'c> T: Tr<'a>,
{
}

// And the plain binder is unchanged.
fn plain<F: for<'a> Fn(&'a u32) -> &'a u32>(f: F) -> u32 {
    *f(&7)
}

fn main() {
    assert_eq!(plain(|x| x), 7);
}
