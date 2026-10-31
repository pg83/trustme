#![feature(generic_arg_infer)]
struct Packet<const N: usize>([u16; N]);
trait Build { type Output; fn build(self) -> Self::Output; }
struct Seed;
impl Build for Seed { type Output = Packet<3>; fn build(self) -> Self::Output { Packet([2, 3, 5]) } }
fn packet<const N: usize, B: Build<Output = Packet<N>>>(seed: B) -> Packet<N> { seed.build() }
fn main() { let values = packet::<_, _>(Seed); assert_eq!(values.0.len(), 3); assert_eq!(values.0[2], 5); }
