#![feature(unboxed_closures)]

trait Peel { type Output; }
impl<G, B> Peel for G where G: FnMut(u8) -> Option<B> {
    type Output = B;
}

trait Select<F> { type Output; }
impl<F> Select<F> for i32 { type Output = (); }
impl<F, R> Select<F> for u16 where F: FnOnce() -> R {
    type Output = R;
}

trait Pick<Tag> { type Output; }
impl<F, R> Pick<i32> for F where F: FnOnce() -> R {
    type Output = <F as FnOnce<()>>::Output;
}
impl<F, R> Pick<u16> for F where F: FnOnce() -> R {
    type Output = <F as FnOnce<()>>::Output;
}

fn bridge<Tag, Mark, G, F, R>(_: Tag, _: Mark, _: G, _: F) -> R
where
    F: FnOnce() -> R + Pick<Tag, Output = (<G as Peel>::Output, <Mark as Select<F>>::Output)>,
    Mark: Select<F>,
    G: Peel,
{
    loop {}
}

fn main() {
    if std::hint::black_box(false) {
        let result = bridge(0, 0, |_: u8| loop {}, || loop {});
        let _: u8 = result.0;
    }
}
