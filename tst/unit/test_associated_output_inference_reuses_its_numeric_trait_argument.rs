//@ compile-variant: -O

struct Wrap<T>(T);
trait Reify<P> {
    type Out;
    fn reify(self) -> Self::Out;
}
impl<T> Reify<u8> for Wrap<T> {
    type Out = T;
    fn reify(self) -> T { self.0 }
}

fn project<P, T>(_: P, input: Wrap<T>) -> <Wrap<T> as Reify<P>>::Out
where Wrap<T>: Reify<P>
{
    input.reify()
}

fn agree<T>(left: T, right: T) -> (T, T) { (left, right) }

fn main() {
    let first = 7;
    let second = 11;
    let pair = agree(first, project(first, Wrap(second)));
    assert_eq!(pair, (7u8, 11u8));
}
