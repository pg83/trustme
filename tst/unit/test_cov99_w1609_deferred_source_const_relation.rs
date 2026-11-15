//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Base {
    type Out;
}
struct Complete;
impl Base for Complete {
    type Out = u8;
}
trait Source {
    type Out;
}
impl<T, U> Source for &Option<T>
where
    T: Base<Out = U>,
{
    type Out = U;
}

struct Mapper<T>(T);
trait Inner<const N: usize> {
    type Item;
}
impl<T, U> Inner<3> for Mapper<T>
where
    T: Source<Out = U>,
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
struct Relay<T, const N: usize>(T);
trait Output {
    type Out;
}
impl<T, const N: usize> Output for Relay<T, N>
where
    Mapper<T>: Inner<N>,
{
    type Out = <Shell<<Mapper<T> as Inner<N>>::Item> as Extract>::Out;
}
fn consume<S: Output<Out = u8>>(_: S) {}

fn main() {
    let seed = None;
    consume(Relay::<_, _>(&seed));
    let _: Option<Complete> = seed;
}
