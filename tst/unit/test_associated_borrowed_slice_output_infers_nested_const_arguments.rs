#![feature(generic_arg_infer)]
trait Build { type Output; fn build(self) -> Self::Output; }
struct Seed;
impl Build for Seed { type Output = &'static [[u16; 2]]; fn build(self) -> Self::Output { &[[2, 3], [5, 7], [11, 13]] } }
fn rows<const N: usize, B: Build<Output = &'static [[u16; N]]>>(seed: B) -> &'static [[u16; N]] { seed.build() }
fn main() { let values = rows::<_, _>(Seed); assert_eq!(values[0].len(), 2); assert_eq!(values[2][1], 13); }
