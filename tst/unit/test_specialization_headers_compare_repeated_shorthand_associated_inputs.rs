#![feature(specialization)]
type AChosen = <(u8, u8, Token) as Pick>::Output;
trait Family { type Item; }
struct Token;
impl Family for Token { type Item = u8; }
trait Pick { type Output; }
impl<A, B> Pick for (A, A, B) { default type Output = u16; }
impl<T: Family> Pick for (T::Item, T::Item, T) { type Output = u32; }
fn main() {
    let value: AChosen = 13u32;
    assert_eq!(value, 13);
    assert_eq!(std::mem::size_of::<AChosen>(), 4);
}
