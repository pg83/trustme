trait Grid { type Row<T, const N: usize>; }
struct Whole;
impl Grid for Whole { type Row<T, const N: usize> = [T; N]; }
fn through<G: Grid, T, const N: usize>(row: G::Row<T, N>) -> G::Row<T, N> { row }
fn main() {
    let values = through::<Whole, u32, _>([241, 251, 257]);
    assert_eq!(values, [241, 251, 257]);
    let inferred = through::<Whole, _, 2>([263u16, 269]);
    assert_eq!(inferred, [263, 269]);
}
