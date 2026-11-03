#![feature(inherent_associated_types)]
struct Storage<T>(T);
impl<T> Storage<T> { type Row<const N: usize> = [T; N]; }
trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
fn through<F: Family>(value: <Storage<F::Word>>::Row<2>) -> <Storage<F::Word>>::Row<2> { value }
fn main() { assert_eq!(through::<Whole>([193, 197]), [193, 197]); }
