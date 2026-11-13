struct Pair<A, B>(A, B);
fn identity<T>(value: T) -> T { value }

fn check<T>(value: T) {
    assert_eq!(std::mem::size_of_val(&value), 2048);
}

macro_rules! tree {
    (@leaf) => { identity(7u8) };
    (@one $($tail:tt)*) => { Pair(tree!($($tail)*), tree!($($tail)*)) };
}

fn main() {
    check(tree!(@one @one @one @one @one @one @one @one @one @one @one @leaf));
}
