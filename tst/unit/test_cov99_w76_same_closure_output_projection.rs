#![feature(unboxed_closures)]

trait Pick<Tag> {
    type Output;
    fn pick(self, tag: Tag) -> Self::Output;
}

impl<F, R> Pick<i32> for F
where
    F: FnOnce() -> R,
{
    type Output = <F as FnOnce<()>>::Output;
    fn pick(self, _: i32) -> Self::Output { self() }
}

impl<F, R> Pick<u16> for F
where
    F: FnOnce() -> R,
{
    type Output = <F as FnOnce<()>>::Output;
    fn pick(self, _: u16) -> Self::Output { self() }
}

fn choose<Tag, F, R>(tag: Tag, make: F) -> R
where
    F: FnOnce() -> R,
    F: Pick<Tag, Output = <F as FnOnce<()>>::Output>,
{
    make.pick(tag)
}

fn main() {
    let mut values = choose(0, || Vec::new());
    values.push(17u32);
    assert_eq!(values, [17]);
}
