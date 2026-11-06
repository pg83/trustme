macro_rules! expression_text { ($value:expr) => { stringify!($value) }; }
macro_rules! type_text { ($value:ty) => { stringify!($value) }; }
type Local = u16;
fn answer() -> Local { 73 }
fn main() {
    assert_eq!(expression_text!(self::answer()), "self::answer()");
    assert_eq!(type_text!(self::Local), "self::Local");
    assert_eq!(self::answer(), 73);
}
