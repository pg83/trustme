pub trait Route<T = ()> {
    fn route(&self) -> u8;
}
