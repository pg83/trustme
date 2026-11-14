//@ compile-fail: Outer

trait Base {
    type Output;
}

struct First;
struct Second;

impl Base for First {
    type Output = u8;
}

impl Base for Second {
    type Output = u16;
}

trait Inner<A, B, const N: usize> {}

impl Inner<u8, u16, 7> for () {}

trait Outer<T> {
    fn pick();
}

impl<T, U> Outer<T> for ()
where
    T: Base<Output = U>,
    (): Inner<U, U, 7>,
{
    fn pick() {}
}

fn main() {
    <() as Outer<_>>::pick();
}
