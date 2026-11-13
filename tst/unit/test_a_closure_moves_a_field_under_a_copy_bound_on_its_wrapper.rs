#[derive(Clone, Copy)]
struct Wrapper<T>(T);

fn take<T>(value: Wrapper<T>) -> T
where
    Wrapper<T>: Copy,
{
    let closure = move || value.0;
    closure()
}

fn main() {
    assert_eq!(take(Wrapper(23u8)), 23);
    assert_eq!(take(Wrapper(Some(41u16))), Some(41));
}
