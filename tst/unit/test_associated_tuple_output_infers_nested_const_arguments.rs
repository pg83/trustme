#![feature(generic_arg_infer)]
trait Build { type Output; fn build(self) -> Self::Output; }
struct Seed;
impl Build for Seed { type Output = ([u16; 2], [u16; 3]); fn build(self) -> Self::Output { ([2, 3], [5, 7, 11]) } }
fn pair<const N: usize, const M: usize, B: Build<Output = ([u16; N], [u16; M])>>(seed: B) -> ([u16; N], [u16; M]) { seed.build() }
fn main() { let (left, right) = pair::<_, _, _>(Seed); assert_eq!(left.len(), 2); assert_eq!(right.len(), 3); assert_eq!(left[1] + right[2], 14); }
