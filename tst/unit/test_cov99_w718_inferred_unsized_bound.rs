//@ compile-fail: Select
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Reveal<T: ?Sized> {}
impl Reveal<str> for () {}

trait Select<T: ?Sized> {}
impl<T> Select<T> for () where (): Reveal<T> {}

fn select<T: ?Sized>() where (): Select<T> {}

fn main() {
    select::<_>();
}
