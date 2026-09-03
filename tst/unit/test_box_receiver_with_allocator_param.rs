// `<[T]>::into_vec` declares its receiver as `Box<Self, A>` over its own
// allocator parameter, so the call site reads A off the actual receiver. A
// receiver classified as the conventional `Box<Self>` form drops everything
// after Self: A is then fixed by nothing, and typecheck ends asking for
// annotations on `A: Allocator` for a call whose receiver names the allocator.

fn main() {
    let b: Box<[i32]> = Box::new([1, 2, 3]);
    let n = b.into_vec().len();
    assert_eq!(n, 3);
}
