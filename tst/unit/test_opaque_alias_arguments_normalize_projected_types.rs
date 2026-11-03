#![feature(type_alias_impl_trait)]
type Hidden<T> = impl Fn(T) -> T;
#[define_opaque(Hidden)]
fn build<T>() -> Hidden<T> { |value| value }
trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
fn exercise<F: Family>(value: F::Word) -> F::Word {
    let function: Hidden<F::Word> = build();
    function(value)
}
fn main() { assert_eq!(exercise::<Whole>(181), 181); }
