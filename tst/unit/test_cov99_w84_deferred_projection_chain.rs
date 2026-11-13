use std::ops::Deref;

trait Select { type Output; }
impl Select for u8 { type Output = u16; }
impl Select for u16 { type Output = u8; }

struct Chain<T, U>(Option<T>, U);
impl<T, U> Deref for Chain<T, U> {
    type Target = U;
    fn deref(&self) -> &U { &self.1 }
}

fn chain<T: Select>() -> Chain<T, Chain<(T::Output, T), ()>> {
    Chain(None, Chain(None, ()))
}

fn main() {
    let outer = chain();
    let selected = if std::hint::black_box(true) { &outer } else { &outer.1 };
    let output: Option<(_, u8)> = selected.0;
    assert!(output.is_none());
}
