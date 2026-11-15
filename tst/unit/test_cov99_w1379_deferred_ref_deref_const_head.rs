//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::ops::Deref;

trait Tag<const N: usize>: Sized {}
struct Item<const N: usize>;
impl<const N: usize> Tag<N> for Item<N> {}

fn item<const N: usize>() -> impl Tag<N> {
    Item::<N>
}

struct Thunk<F>(F);
impl<F, T> Deref for Thunk<F>
where
    F: Fn() -> T,
{
    type Target = T;
    fn deref(&self) -> &T {
        panic!("not called")
    }
}
impl<F> Tag<7> for Thunk<F> {}

fn needs_seven<T: Tag<7>>(_: &T) {}
fn needs_nine<T: Tag<9>>(_: T) {}

fn choose<'a, T: Tag<7>>() -> &'a T {
    panic!("not called")
}

pub fn exercise() {
    let thunk = Thunk(|| item());
    let mut borrowed = choose();
    borrowed = &thunk;
    needs_seven(borrowed);
    needs_nine((thunk.0)());
}
