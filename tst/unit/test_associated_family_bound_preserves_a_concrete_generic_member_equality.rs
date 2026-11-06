trait Family {
    type Member<T>;
    fn make() -> Self::Member<u8>;
}
trait Carrier {
    type Family: Family<Member<u8> = u16>;
}
struct Numbers;
impl Family for Numbers {
    type Member<T> = u16;
    fn make() -> u16 { 43 }
}
struct Container;
impl Carrier for Container { type Family = Numbers; }
fn require<F: Family<Member<u8> = u16>>() -> u16 { F::make() }
fn through<C: Carrier>() -> u16 { require::<C::Family>() }
fn main() { assert_eq!(through::<Container>(), 43); }
