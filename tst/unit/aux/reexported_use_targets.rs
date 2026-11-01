pub trait Source: Sized {
    type Item;
    const EMPTY: Option<Self> = None;
    fn make() -> Self;
    fn read(&self) -> u16;
}

pub enum Mode { First = 3, Second = 11 }
