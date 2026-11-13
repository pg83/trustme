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

fn unknown<T>() -> T {
    loop {}
}

fn main() {
    let call: Box<dyn FnOnce() -> <Stage<fn() -> _> as Output>::Value> = unknown();
    let _value: (u8, _) = call();
}
