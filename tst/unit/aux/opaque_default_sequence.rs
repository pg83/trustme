pub trait Factory<T: Copy, const N: usize> {
    fn sequence(value: T) -> impl Iterator<Item = T> + Clone { [value; N].into_iter() }
}
pub struct Source;
impl<T: Copy, const N: usize> Factory<T, N> for Source {}
