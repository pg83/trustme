pub struct RefTag<const N: usize>;
impl<const N: usize> RefTag<N> {
    const DATA: [u8; 2] = [N as u8, (N + 1) as u8];
    pub const REFERENCE: &'static [u8; 2] = &Self::DATA;
}
