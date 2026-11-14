#![feature(unboxed_closures)]

trait Gate<H> { type Output; }
impl<H, B> Gate<H> for u8 where H: FnMut(u8) -> Option<B> {
    type Output = B;
}
trait Delay<H> { type Output; }
impl<G, M, H, B> Delay<H> for G
where
    G: FnOnce() -> M,
    M: Gate<H, Output = B>,
{
    type Output = B;
}
trait Select<F> { type Output; }
impl<F> Select<F> for i32 { type Output = (); }
impl<F, R> Select<F> for u16 where F: FnOnce() -> R {
    type Output = R;
}

struct Pair<G, F>(G, F);
trait Pick<Tag> { type Output; }
impl<G, F, M, R> Pick<i32> for Pair<G, F>
where G: FnOnce() -> M, F: FnOnce() -> R {
    type Output = (<G as FnOnce<()>>::Output, <F as FnOnce<()>>::Output);
}
impl<G, F, M, R> Pick<u16> for Pair<G, F>
where G: FnOnce() -> M, F: FnOnce() -> R {
    type Output = (<G as FnOnce<()>>::Output, <F as FnOnce<()>>::Output);
}

fn bridge<Tag, Mark, G, F, H, M, R>(_: Tag, _: Mark, _: G, _: F, _: H) -> (M, R)
where
    G: FnOnce() -> M,
    F: FnOnce() -> R,
    M: Gate<H>,
    Pair<G, F>: Pick<Tag, Output = (u8, (<G as Delay<H>>::Output, <Mark as Select<F>>::Output))>,
    Mark: Select<F>,
{
    loop {}
}

fn main() {
    if std::hint::black_box(false) {
        let result = bridge(0, 0, || loop {}, || loop {}, |_: u8| loop {});
        let _: u8 = result.1.0;
    }
}
