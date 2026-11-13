struct Pair<A, B>(A, B);
trait Joined {}
impl Joined for u8 {}
impl<A: Joined, B: Joined> Joined for Pair<A, B> {}

fn identity<T>(value: T) -> T { value }
fn hold<T: Joined>(value: T) -> T { value }

macro_rules! tree {
    (@leaf) => { identity(7u8) };
    (@one $($tail:tt)*) => { Pair(tree!($($tail)*), tree!($($tail)*)) };
}

fn main() {
    let value = hold(tree!(@one @one @one @one @leaf));
    assert_eq!(value.0.0.0.0, 7);
    assert_eq!(value.1.1.1.1, 7);
}
