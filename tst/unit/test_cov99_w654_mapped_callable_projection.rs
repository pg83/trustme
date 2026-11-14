//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Read {
    type Item;
    fn read(self) -> Self::Item;
}

struct One<T>(T);

impl<T> Read for One<T> {
    type Item = T;
    fn read(self) -> T {
        self.0
    }
}

struct Mapped<I, F>(I, F);

impl<I: Read, F: FnOnce(I::Item) -> B, B> Read for Mapped<I, F> {
    type Item = B;
    fn read(self) -> B {
        (self.1)(self.0.read())
    }
}

fn take<I: Read>(input: I) -> I::Item {
    input.read()
}

fn main() {
    let result: Option<u16> = take(Mapped(One(9u16), |value| Some(value)));
    assert_eq!(result, Some(9u16));
}
