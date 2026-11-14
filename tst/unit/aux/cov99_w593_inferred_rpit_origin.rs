pub struct Factory<T, const N: usize>(pub T);

impl<T: Copy, const N: usize> Factory<T, N> {
    #[inline]
    pub fn generate(self) -> impl Iterator<Item = T> {
        core::iter::once(self.0)
    }
}

#[inline]
pub fn relay<T: Copy, const N: usize>(value: T) -> impl Iterator<Item = T> {
    Factory::<_, N>(value).generate()
}
