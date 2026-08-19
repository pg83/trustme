//@ run-pass
// `repr(Rust)` may lay a tuple struct's fields out in a different order than
// they were written, so the constructor names each member it fills rather than
// relying on the position. And a tuple whose last element is unsized keeps it
// last: the metadata a pointer to the tuple carries is that element's.

#[derive(Copy, Clone, PartialEq, Debug)]
struct Pair<'a>(i32, &'a i32);

#[derive(Copy, Clone, PartialEq, Debug)]
enum Either<'a> {
    Left(i32, &'a i32),
    #[allow(dead_code)]
    Right,
}

fn build<'a, F: Fn(i32, &'a i32) -> Pair<'a>>(v: &'a i32, f: F) -> Pair<'a> {
    f(42, v)
}

fn main() {
    let x = 5;

    let make = Pair;
    assert_eq!(Pair(42, &x), make(42, &x));
    assert_eq!(Pair(42, &x), build(&x, Pair));
    assert_eq!(Pair(42, &x).0, 42);
    assert_eq!(*Pair(42, &x).1, 5);

    let variant = Either::Left;
    assert_eq!(Either::Left(42, &x), variant(42, &x));

    // A tuple naming an unsized type only as a phantom still has to lay out,
    // and the unsized element has to stay last for that to be possible.
    let _ = Holder::<[()], Never>::new();
}

use core::marker::PhantomData;

pub struct Holder<T: ?Sized, A>(PhantomData<(A, T)>);

enum Never {}

impl<T: ?Sized> Holder<T, Never> {
    fn new() -> Holder<T, Never> {
        Holder(PhantomData)
    }
}
