const fn constant(a:u16,b:u16) -> u16 { a*10+b }
#[inline(never)]
fn runtime(a:u16,b:u16) -> u16 { a*10+b }
const fn select(a:u16,b:u16) -> u16 { std::intrinsics::const_eval_select((a,b),constant,runtime) }
const VALUE:u16=select(3,7);
const fn constant_empty() -> u8 { 13 }
#[inline(never)]
fn runtime_empty() -> u8 { 13 }
const fn empty() -> u8 { std::intrinsics::const_eval_select((),constant_empty,runtime_empty) }
const EMPTY:u8=empty();
const fn constant_array<const N:usize>(value:u16) -> [u16;N] { [value;N] }
#[inline(never)]
fn runtime_array<const N:usize>(value:u16) -> [u16;N] { [value;N] }
const fn array<const N:usize>(value:u16) -> [u16;N] { std::intrinsics::const_eval_select((value,),constant_array::<N>,runtime_array::<N>) }
const ARRAY:[u16;3]=array::<3>(17);
const IMPORTED:[u16;2]=const_select_routes::selected::<2>(21);
fn main() {
 assert_eq!(VALUE,37);
 assert_eq!(IMPORTED,[21;2]);
 assert_eq!(const_select_routes::selected::<3>(std::hint::black_box(23)),[23;3]);
 assert_eq!(EMPTY,13);
 assert_eq!(empty(),13);
 assert_eq!(ARRAY,[17,17,17]);
 assert_eq!(array::<4>(std::hint::black_box(19)),[19;4]);
 assert_eq!(select(std::hint::black_box(4),std::hint::black_box(9)),49);
}
