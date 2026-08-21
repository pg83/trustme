trait Tail {
    type Type: ?Sized;
}

impl Tail for (i32, bool) {
    type Type = [(); 1];
}

impl Tail for (u32, ()) {
    type Type = [()];
}

struct Wrapper<T, U>
where
    (T, U): Tail,
{
    value: <(T, U) as Tail>::Type,
}

fn coerce(value: &Wrapper<i32, bool>) -> &Wrapper<u32, ()> {
    value
}

fn main() {
    let value = Wrapper::<i32, bool> { value: [()] };
    let slice = coerce(&value);

    assert_eq!(slice.value.len(), 1);
}
