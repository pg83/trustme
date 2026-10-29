#![feature(trait_alias)]

pub trait Read {
    type Item;
    fn read(&self) -> Self::Item;
}
impl Read for u32 {
    type Item = u32;
    fn read(&self) -> u32 { *self + 1 }
}
impl Read for u64 {
    type Item = u64;
    fn read(&self) -> u64 { *self + 2 }
}
pub trait Flags = Send + Sync;
pub trait Reader<T = u32> = Read<Item = T> + Flags;
pub trait View = Reader;
pub type Borrowed<'a> = &'a dyn View;

pub fn read(value: Borrowed<'_>) -> u32 { value.read() }
pub fn read_wide(value: &dyn Reader<u64>) -> u64 { value.read() }
pub fn marker_size(value: &dyn Flags) -> usize { std::mem::size_of_val(value) }

fn main() {
    let narrow = 17u32;
    let wide = 23u64;
    assert_eq!(read(&narrow), 18);
    assert_eq!(read_wide(&wide), 25);
    assert_eq!(marker_size(&narrow), std::mem::size_of::<u32>());
    assert_eq!(marker_size(&wide), std::mem::size_of::<u64>());
}
