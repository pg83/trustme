#![feature(generic_arg_infer)]
trait Build { type Output; fn build(self) -> Self::Output; }
struct Seed;
impl Build for Seed { type Output = [u16; 3]; fn build(self) -> Self::Output { [2, 3, 5] } }
fn array<const N: usize, B: Build<Output = [u16; N]>>(seed: B) -> [u16; N] { seed.build() }
fn main() { let values = array::<_, _>(Seed); assert_eq!(values.len(), 3); assert_eq!(values[2], 5); }
