// term-transcript (a winnow dev-dependency) writes
// `class.unwrap_or(Cow::Borrowed(b""))` with `class: Option<Cow<'_, [u8]>>`.
// rustc checks the receiver, looks the method up, and only then checks the
// arguments with the expected input `Cow<[u8]>`, so `Cow::Borrowed`'s `B` is
// `[u8]` and `b""` is unsized into it. Our receiver `W(class)` got its type in
// the same argument-bindings step that bound `B = [u8; 0]` from `b""` inside
// the method's argument; the probe then rejected the method ("No applicable
// methods").
use std::borrow::Cow;

struct W<T>(Option<T>);

impl<T> W<T> {
    fn pick(self, default: T) -> T {
        match self.0 {
            Some(v) => v,
            None => default,
        }
    }
}

fn picked(class: Option<Cow<'_, [u8]>>) -> Cow<'_, [u8]> {
    W(class).pick(Cow::Borrowed(b""))
}

fn class_of(class: Option<Cow<'_, [u8]>>) -> Cow<'_, [u8]> {
    class.unwrap_or(Cow::Borrowed(b""))
}

fn main() {
    assert_eq!(&*picked(None), b"");
    assert_eq!(&*picked(Some(Cow::Borrowed(b"x"))), b"x");
    assert_eq!(&*class_of(None), b"");
    assert_eq!(&*class_of(Some(Cow::Owned(vec![1, 2]))), &[1, 2]);
}
