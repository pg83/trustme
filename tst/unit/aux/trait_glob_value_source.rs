pub trait Source: Sized {
    type Item;
    const EMPTY: Option<Self> = None;
    fn create() -> Self;
    fn read(self) -> u16;
}
pub use Source as SourceAlias;
