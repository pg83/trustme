#![feature(ptr_metadata)]

trait Rebuild {
    fn same_pointer(&self) -> bool {
        let original: *const Self = self;
        let metadata = std::ptr::metadata(original);
        let rebuilt = std::ptr::from_raw_parts::<Self>(original as *const (), metadata);
        std::ptr::eq(original, rebuilt)
    }
}
impl Rebuild for [u16] {}
impl Rebuild for str {}
impl Rebuild for () {}

fn main() {
    assert!([3u16, 5, 7][..].same_pointer());
    assert!("metadata".same_pointer());
    assert!(().same_pointer());
}
