pub fn values<T: Copy, const N: usize>(input: [T; N])
    -> impl ExactSizeIterator<Item = T> + Clone
{
    input.into_iter()
}
