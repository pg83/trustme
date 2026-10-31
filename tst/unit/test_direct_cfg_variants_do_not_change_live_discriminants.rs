#[repr(u8)]
enum Tag {
    First = 3,
    #[cfg(any())]
    Removed = compile_error!("removed discriminant expanded"),
    Next,
}
fn main() { assert_eq!(Tag::First as u8, 3); assert_eq!(Tag::Next as u8, 4); }
