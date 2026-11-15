//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Mapper<F, T>(F, std::marker::PhantomData<T>);
trait Inner<const N: usize> {
    type Item;
}
impl<F, T, U> Inner<3> for Mapper<F, T>
where
    F: FnOnce() -> Option<U>,
{
    type Item = U;
}

struct Relay<F, const N: usize>(F);
trait Output {
    type Out;
}
impl<F, const N: usize> Output for Relay<F, N>
where
    Mapper<F, ()>: Inner<N>,
    Mapper<F, <Mapper<F, ()> as Inner<N>>::Item>: Inner<N>,
{
    type Out = <Mapper<F, <Mapper<F, ()> as Inner<N>>::Item> as Inner<N>>::Item;
}

trait Accepted {}
impl<S: Output<Out = u8>> Accepted for S {}
fn consume<S: Accepted>(_: S) {}

fn main() {
    consume(Relay::<_, _>(|| Default::default()));
}
