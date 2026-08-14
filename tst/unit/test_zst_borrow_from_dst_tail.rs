#[derive(Copy, Clone)]
struct Zst;

struct Tail<T: ?Sized> {
    value: T,
}

#[inline(never)]
fn borrow_first<T>(value: &Tail<[T]>) -> &T {
    &value.value[0]
}

fn main() {
    let value = Tail { value: [Zst] };
    let value: &Tail<[Zst]> = &value;
    let first = borrow_first(value);
    assert_eq!(first as *const Zst, value.value.as_ptr());
}
