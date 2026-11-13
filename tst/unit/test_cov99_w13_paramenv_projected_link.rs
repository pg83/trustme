//@ crate-type: lib
//@ compile-fail: Type mismatch

trait Link<T> {}
impl Link<u8> for () {}
impl Link<u16> for () {}
trait Source {
    type Value;
}
trait Output {
    type Value;
}
struct Stage<F>(F);
impl<F, B> Output for Stage<F>
where
    F: Source<Value = Option<B>>,
    (): Link<B>,
{
    type Value = (u8, B);
}

fn inspect<F: Source, C>(call: C)
where
    Stage<F>: Output,
    C: FnOnce() -> <Stage<F> as Output>::Value,
{
    let _value: (u8, _) = call();
}
