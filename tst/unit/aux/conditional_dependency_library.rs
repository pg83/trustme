pub trait Family { type Item; }
impl Family for u8 { type Item = u32; }
#[inline(never)]
fn private_value() -> u32 { 37 }
#[inline(never)]
fn private_generic<T: Family<Item = u32>>() -> u32 { 41 }
#[inline(never)]
fn unsized_length<T: ?Sized>(value: &T) -> usize { std::mem::size_of_val(value) }
pub fn plain<T: Clone>(value: T) -> (T, u32) { (value.clone(), private_value()) }
pub fn associated<T: Copy>(value: T) -> (T, u32) { (value, private_generic::<u8>()) }
pub fn with_unsized<T: Copy>(value: T, text: &str) -> (T, usize) { (value, unsized_length(text)) }
