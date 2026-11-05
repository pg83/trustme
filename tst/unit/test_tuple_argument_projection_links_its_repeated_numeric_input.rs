//@ compile-variant: -O

struct Wrap<T>(T);
trait Reify<P> { type Out; }
impl<T> Reify<u8> for Wrap<T> { type Out = T; }

fn retain<P, T>(pair: (P, <Wrap<T> as Reify<P>>::Out), _: Wrap<T>)
    -> (P, <Wrap<T> as Reify<P>>::Out)
where Wrap<T>: Reify<P>
{
    pair
}

fn main() {
    let repeated = 17;
    let marker = 23;
    let pair = retain((repeated, repeated), Wrap(marker));
    assert_eq!(pair, (17u8, 17u8));
}
