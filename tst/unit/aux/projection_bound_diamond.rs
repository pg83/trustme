pub trait Source { type Cell<T>; }
pub trait Left: Source<Cell<u8>: Copy> {}
pub trait Right: Source<Cell<String>: Eq> {}
pub trait Both: Left + Right {}
pub trait Further: Both {}
pub struct Value;
impl Source for Value { type Cell<T> = T; }
impl Left for Value {}
impl Right for Value {}
impl Both for Value {}
impl Further for Value {}
