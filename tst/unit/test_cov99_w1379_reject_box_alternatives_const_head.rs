//@ crate-type: lib
//@ compile-only
//@ compile-fail: error:
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::ops::Deref;

trait Tag<const N: usize>: Sized {}
struct Item<const N: usize>;
impl<const N: usize> Tag<N> for Item<N> {}
fn item<const N: usize>() -> impl Tag<N> { Item::<N> }

struct Thunk<F, const ID: u8>(F);
impl<F, T, const ID: u8> Deref for Thunk<F, ID>
where
    F: Fn() -> T,
{
    type Target = T;
    fn deref(&self) -> &T { panic!("not called") }
}
fn needs_seven<T: Tag<7>>(_: &Box<T>) {}

pub fn exercise(flag: bool) {
    let closure = || item();
    let left = Box::new(Thunk::<_, 0>(closure));
    let right = Box::new(Thunk::<_, 1>(closure));
    let selected: Box<_> = if flag { left } else { right };
    needs_seven(&selected);
}
