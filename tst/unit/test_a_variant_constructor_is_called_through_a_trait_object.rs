// term-transcript's `ShellOptions::default()` stores `Box::new(Some)` as a
// `Box<dyn FnMut(String) -> Option<String>>`. The vtable's `call_mut` for the
// constructor's fn item calls `Option::Some` itself, and codegen looked the
// variant up as a function ("Node 1 of path Option::Some wasn't a module").
// A tuple variant or tuple struct constructor called as a function builds
// the value.
struct Pair(u8, u8);

fn main() {
    let mut some: Box<dyn FnMut(String) -> Option<String>> = Box::new(Some);
    assert_eq!(some("a".to_string()), Some("a".to_string()));
    let ok: &dyn Fn(u8) -> Result<u8, ()> = &Ok;
    assert_eq!(ok(3), Ok(3));
    let pair: Box<dyn FnOnce(u8, u8) -> Pair> = Box::new(Pair);
    let Pair(a, b) = pair(1, 2);
    assert_eq!((a, b), (1, 2));
}
