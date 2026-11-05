//@ compile-fail: No applicable methods
#![feature(type_alias_impl_trait)]

type Token<T: Copy> = impl Copy;
type Seal<T: Copy> = impl Copy;

#[define_opaque(Token)]
fn token<T: Copy>(value: T) -> Token<T> { value }
#[define_opaque(Seal)]
fn seal<T: Copy>(value: T) -> Seal<T> { value }

struct Tagged<A, B>(A, B);
impl<T: Copy> Tagged<Token<T>, T> {
    fn width(&self) -> usize { std::mem::size_of::<T>() }
}

fn main() {
    let _ = token(163u8);
    let _ = Tagged(seal(167u8), 173u8).width();
}
