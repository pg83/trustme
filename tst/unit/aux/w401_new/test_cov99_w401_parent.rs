pub trait Parent<const N: usize, const M: usize = 0> {
    fn read(&self) -> u32;
}
