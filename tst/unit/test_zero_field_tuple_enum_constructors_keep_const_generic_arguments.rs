//@ compile-variant: -O

enum Choice<const N: usize> { Empty(), Bytes([u8; N]) }
const fn constructor<const N: usize>() -> fn() -> Choice<N> { Choice::<N>::Empty }
const BUILD: fn() -> Choice<5> = constructor::<{ 2 + 3 }>();

fn main() {
    assert!(matches!(BUILD(), Choice::Empty()));
    assert!(matches!(Choice::<3>::Bytes([3, 5, 7]), Choice::Bytes([3, 5, 7])));
}
