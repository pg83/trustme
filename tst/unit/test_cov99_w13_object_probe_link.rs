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

fn main() {
    let call: Box<dyn FnOnce() -> <Stage<fn() -> _> as Output>::Value> =
        Box::new(|| (9u8, 5u8));
    let value: (u8, _) = call();
    assert_eq!(value, (9, 5));
}
