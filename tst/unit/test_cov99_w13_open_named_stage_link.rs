//@ compile-fail: type annotations needed

trait Link<T> {}
impl Link<u8> for () {}
impl Link<u16> for () {}
trait Output {
    type Value;
}
struct Stage<F>(F);
impl<F, B> Output for Stage<F>
where
    F: FnOnce() -> Option<B>,
    (): Link<B>,
{
    type Value = (u8, B);
}

fn projected<O: Output>(_: O) -> O::Value {
    loop {}
}
fn require<I, T, F: FnOnce(I) -> (u8, T)>(_: F, _: I) {}

fn main() {
    require(projected::<_>, Stage(|| Default::default()));
}
