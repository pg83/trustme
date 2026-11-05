//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Token<T: Copy> = impl Copy;
#[define_opaque(Token)]
fn token<T: Copy>(value: T) -> Token<T> { value }

struct Tagged<A, B>(A, B);
impl<T: Copy> Tagged<Token<T>, T> {
    fn width(&self) -> usize { std::mem::size_of::<T>() }
}

fn width<T: Copy>(value: Token<T>, tag: T) -> usize {
    Tagged(value, tag).width()
}

fn main() {
    assert_eq!(width(token(139u8), 149u8), 1);
    assert_eq!(width(token(151u16), 157u16), 2);
}
