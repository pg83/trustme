pub trait Rows {
    type Row<const N: usize>;
    const BIAS: u16;
    fn row<const N: usize>(&self) -> Self::Row<N>;
}
