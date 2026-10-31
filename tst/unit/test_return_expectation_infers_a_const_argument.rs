#![feature(generic_arg_infer)]
struct Packet<const N: usize>([u8; N]);
fn make<const N: usize>() -> Packet<N> { Packet([7; N]) }
fn main() { let packet: Packet<3> = make::<_>(); assert_eq!(packet.0, [7, 7, 7]); }
