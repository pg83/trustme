trait Family { type Word: Copy; type Row<T, const N: usize>; }
struct Whole;
impl Family for Whole { type Word = u32; type Row<T, const N: usize> = [T; N]; }
struct Matrix<T, const N: usize>([T; N]);
fn through<F: Family>(value: F::Row<F::Word, 2>) -> F::Row<F::Word, 2> { value }
fn wrapped<F: Family>(value: [F::Word; 2]) -> Matrix<F::Word, 2> { Matrix(value) }
fn main() {
    let row = through::<Whole>([139, 149]);
    assert_eq!(wrapped::<Whole>(row).0, [139, 149]);
}
