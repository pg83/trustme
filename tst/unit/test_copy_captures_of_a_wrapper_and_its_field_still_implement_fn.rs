#[derive(Clone, Copy)]
struct Wrapper<T>(T);

fn twice<F: Fn() -> R, R>(function: F) -> (R, R) {
    (function(), function())
}

fn whole<T>(value: Wrapper<T>) -> (Wrapper<T>, Wrapper<T>)
where
    Wrapper<T>: Copy,
{
    twice(move || value)
}

fn field<T: Copy>(value: Wrapper<T>) -> (T, T) {
    twice(move || value.0)
}

fn main() {
    let (first, second) = whole(Wrapper(17u8));
    assert_eq!((first.0, second.0), (17, 17));
    assert_eq!(field(Wrapper(Some(29u16))), (Some(29), Some(29)));
}
