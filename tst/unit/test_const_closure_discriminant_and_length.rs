#![feature(const_closures, const_trait_impl)]
#[repr(u8)]
enum Tag { Value = (const || 7)() }
fn main() {
    let bytes = [3u8; (const || 4)()];
    assert_eq!(bytes, [3, 3, 3, 3]);
    assert_eq!(Tag::Value as u8, 7);
}
