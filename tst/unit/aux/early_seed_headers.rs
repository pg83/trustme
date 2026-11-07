pub trait Seed: Sized {
    const VALUE: Self;
}
pub struct Word(pub u8);
impl Seed for Word {
    const VALUE: Self = Word(43);
}
impl Seed for u64 {
    const VALUE: Self = 47;
}
pub trait View {
    fn get(&self) -> u8;
}
impl View for Word {
    fn get(&self) -> u8 { self.0 }
}
