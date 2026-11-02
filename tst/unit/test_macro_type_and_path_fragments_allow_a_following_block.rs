//@ compile-variant: -O

macro_rules! path_body {
    ($result:path $body:block) => { fn path_result() -> $result $body };
}
macro_rules! type_body {
    ($result:ty $body:block) => { fn tuple_result() -> $result $body };
}
path_body!(u32 { 41 });
type_body!((u8, u8) { (3, 7) });
fn main() {
    assert_eq!(path_result(), 41);
    assert_eq!(tuple_result(), (3, 7));
}
