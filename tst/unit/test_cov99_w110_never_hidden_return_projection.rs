#![feature(unboxed_closures)]

trait Select<F> { type Output; }
impl<F, R> Select<F> for i32 where F: FnOnce() -> R {
    type Output = R;
}
impl<F, R> Select<F> for u16 where F: FnOnce() -> R {
    type Output = u16;
}

trait Pick<Tag> {
    type Output;
    fn pick(self, tag: Tag) -> Self::Output;
}
impl<F, R> Pick<i32> for F where F: FnOnce() -> R {
    type Output = <F as FnOnce<()>>::Output;
    fn pick(self, _: i32) -> R { self() }
}
impl<F, R> Pick<u16> for F where F: FnOnce() -> R {
    type Output = <F as FnOnce<()>>::Output;
    fn pick(self, _: u16) -> R { self() }
}

fn bridge<Tag, Mark, F, R>(tag: Tag, _: Mark, f: F) -> R
where
    F: FnOnce() -> R + Pick<Tag, Output = <Mark as Select<F>>::Output>,
    Mark: Select<F, Output = R>,
{
    f.pick(tag)
}

fn main() {
    if std::hint::black_box(false) {
        bridge(0, 0, || loop {});
    }
}
