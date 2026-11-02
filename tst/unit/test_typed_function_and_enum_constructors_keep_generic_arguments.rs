//@ compile-variant: -O
enum Choice<T> { Item(T), Empty }
fn identity<T>(value: T) -> T { value }
fn main() {
    let value = Choice::<u16>::Item(identity::<u16>(47));
    assert_eq!(match value { Choice::Item(n) => n, Choice::Empty => 0 }, 47);
    assert!(matches!(Choice::<u8>::Empty, Choice::Empty));
}
