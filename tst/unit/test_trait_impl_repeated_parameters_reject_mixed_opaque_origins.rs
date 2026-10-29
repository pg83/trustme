//@ compile-fail: No applicable methods
#![feature(type_alias_impl_trait)]
struct Pair<A, B>(A, B);
trait Same { fn width(&self) -> usize; }
impl<T: Copy> Same for Pair<T, T> { fn width(&self) -> usize { std::mem::size_of::<T>() } }
mod alias {
    pub type Token = impl Copy;
    #[define_opaque(Token)] pub fn make() -> Token { 3u8 }
}
fn function() -> impl Copy { 5u8 }
fn main() { let pair = Pair(alias::make(), function()); let _ = pair.width(); }
