trait Size { fn width(&self) -> usize; }
impl<T> Size for T { fn width(&self) -> usize { std::mem::size_of::<T>() } }
fn marker() -> u8 { 17 }
fn main() {
    let values: [Box<dyn Size>; 4] = [Box::new(17u8), Box::new(marker), Box::new(marker as fn() -> u8), Box::new(std::ptr::null::<u8>())];
    let expected = [1, 0, std::mem::size_of::<fn() -> u8>(), std::mem::size_of::<*const u8>()];
    for (value, width) in values.into_iter().zip(expected) { assert_eq!(value.as_ref().width(), width); drop(value); }
}
