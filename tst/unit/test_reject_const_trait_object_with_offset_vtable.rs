//@ compile-fail: Invalid trait object vtable
trait Tag {}
impl Tag for u8 {}
static DATA: u8 = 5;
const ORIGINAL: &dyn Tag = &DATA;
const WORDS: (*const u8, *const usize) = unsafe { std::mem::transmute(ORIGINAL) };
const FORGED: &dyn Tag = unsafe { std::mem::transmute((WORDS.0, WORDS.1.wrapping_add(1))) };
const RESULT: usize = std::mem::size_of_val(FORGED);
fn main() { let _ = RESULT; }
