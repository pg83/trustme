#![feature(inherent_associated_types)]
struct Storage<T, const N: usize>([T; N]);
impl<T, const N: usize> Storage<T, N> { type Row = [T; N]; }
trait Family { type Word: Copy; }
struct Whole;
impl Family for Whole { type Word = u32; }
fn through<F: Family>(value: <Storage<F::Word, 2>>::Row) -> <Storage<F::Word, 2>>::Row { value }
fn main() { assert_eq!(through::<Whole>([167, 173]), [167, 173]); }
