pub trait MakeSeed: Sized {
    const ITEM: Self;
}
pub struct Mark(pub usize);
impl MakeSeed for Mark {
    const ITEM: Self = Mark(43);
}
impl MakeSeed for u64 {
    const ITEM: Self = 47;
}
