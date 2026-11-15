//@ crate-type: lib
//@ compile-only
//@ compile-fail: Tag
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

fn needs_seven<T: Tag<7>>(_: &T) {}

fn choose<T: Tag<7>>() -> Box<T> {
    panic!("not called")
}

pub fn exercise() {
    let closure = || item();
    let allocated = Box::new(Thunk(closure));
    let mut selected = choose();
    selected = allocated;
    needs_seven(&*selected);
}
