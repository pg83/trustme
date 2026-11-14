pub trait Parent<const N: usize> {
    fn read(&self) -> u32;
}
