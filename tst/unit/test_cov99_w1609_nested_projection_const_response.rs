//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Mapper<F>(F);
trait Inner<const N: usize> {
    type Item;
}
impl<F, U> Inner<3> for Mapper<F>
where
    F: FnOnce() -> Option<U>,
{
    type Item = U;
}

struct Shell<T>(T);
trait Extract {
    type Out;
}
impl<T> Extract for Shell<T> {
    type Out = T;
}

struct Relay<F, const N: usize>(F);
trait Output {
    type Out;
}
impl<F, const N: usize> Output for Relay<F, N>
where
    Mapper<F>: Inner<N>,
{
    type Out = <Shell<<Mapper<F> as Inner<N>>::Item> as Extract>::Out;
}

fn consume<S: Output<Out = u8>>(_: S) {}

fn main() {
    consume(Relay::<_, _>(|| Default::default()));
}
