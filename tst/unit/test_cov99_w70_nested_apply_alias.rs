type Tuple<T> = (T,);

trait Apply<A> {
    type Output;
    fn apply(self, arg: A) -> Self::Output;
}

impl<F, A, R> Apply<A> for F
where
    F: FnOnce(A) -> R,
{
    type Output = R;
    fn apply(self, arg: A) -> R { self(arg) }
}

fn compose<F, G, A, B, C>(first: F, second: G, arg: A) -> C
where
    F: Apply<A, Output = B>,
    G: Apply<B, Output = C>,
{
    second.apply(first.apply(arg))
}

fn main() {
    let result: u8 = compose(|pair: Tuple<_>| pair.0, |value| value, (7u8,));
    assert_eq!(result, 7u8);
}
