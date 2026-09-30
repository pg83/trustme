// crypto-bigint declares `trait AddMod<Rhs = Self, Mod = NonZero<Self>>` and
// uses it as a supertrait. Looking an associated item up through the
// supertraits fills in the omitted parameters of each: rustc instantiates a
// default with `Self` and the arguments before it. We only knew a bare `Self`
// default and stopped on "TODO: Monomorphise default arg NonZero<Self>".
pub struct NonZero<T>(pub T);
pub trait AddMod<Rhs = Self, Mod = NonZero<Self>> {
    type Output;
    fn add_mod(&self, rhs: &Rhs, p: &Mod) -> Self::Output;
}
pub trait Integer {
    type Monty;
    fn monty(&self) -> Self::Monty;
}
pub trait Unsigned: AddMod<Output = Self> + Integer + Sized {}
fn f<T: Unsigned>(t: &T) -> T::Monty {
    t.monty()
}
impl AddMod for u32 {
    type Output = u32;
    fn add_mod(&self, rhs: &u32, p: &NonZero<u32>) -> u32 { (self + rhs) % p.0 }
}
impl Integer for u32 {
    type Monty = u64;
    fn monty(&self) -> u64 { *self as u64 * 2 }
}
impl Unsigned for u32 {}
fn main() {
    assert_eq!(f(&21u32), 42);
    assert_eq!(5u32.add_mod(&4, &NonZero(7)), 2);
}
