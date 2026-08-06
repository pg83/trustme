// Impl selection on a const-generic value treated Unevaluated literals (carried
// through a type alias, as in harfrust's SelectAtomic<8/16/32>) as a fuzzy
// match and picked the first candidate.
trait Width { const BITS: usize; }
struct W8; struct W16; struct W32;
impl Width for W8  { const BITS: usize = 8;  }
impl Width for W16 { const BITS: usize = 16; }
impl Width for W32 { const BITS: usize = 32; }
trait Sel<const N: usize> { type Ty: Width; }
impl Sel<8>  for () { type Ty = W8;  }
impl Sel<16> for () { type Ty = W16; }
impl Sel<32> for () { type Ty = W32; }
// The type alias carries N into the projection unevaluated (the trigger).
type Pick<const N: usize> = <() as Sel<N>>::Ty;
fn main() {
    assert_eq!(<Pick<8> as Width>::BITS, 8);
    assert_eq!(<Pick<16> as Width>::BITS, 16);
    assert_eq!(<Pick<32> as Width>::BITS, 32);
}
