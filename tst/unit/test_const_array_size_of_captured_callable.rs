#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
fn zeros<T>(_: &T) -> [u8; std::mem::size_of::<T>()]
where [(); std::mem::size_of::<T>()]: {
    [0; std::mem::size_of::<T>()]
}
fn aligned<T>(_: &T) -> [u8; std::mem::align_of::<T>()]
where [(); std::mem::align_of::<T>()]: {
    [1; std::mem::align_of::<T>()]
}
trait True<const YES: bool> {}
impl True<true> for () {}
trait PointerSized {}
impl<T> PointerSized for T
where (): True<{ std::mem::size_of::<&T>() == std::mem::size_of::<usize>() }> {}
fn pointer_sized_capture() -> impl PointerSized + Fn() -> u8 {
    let input = [29u8; 5];
    move || input[2]
}
fn main() {
    let input = [7u8; 5];
    let word = 19u64;
    let closure = move || input[3] as u64 + word;
    let bytes = zeros(&closure);
    assert_eq!(bytes.len(), std::mem::size_of_val(&closure));
    assert!(bytes.iter().all(|&byte| byte == 0));
    assert_eq!(closure(), 26);
    let alignment = aligned(&closure);
    assert_eq!(alignment.len(), std::mem::align_of_val(&closure));
    assert!(alignment.iter().all(|&byte| byte == 1));
    assert_eq!(pointer_sized_capture()(), 29);
}
