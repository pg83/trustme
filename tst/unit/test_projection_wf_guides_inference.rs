trait Identity {
    type Type;
}

impl<T> Identity for T {
    type Type = T;
}

trait Project<U: Identity<Type = Self>> {
    type Output;
}

impl<T, U: Identity<Type = T>> Project<U> for T {
    type Output = ();
}

fn main() {
    let _: <_ as Project<u8>>::Output;
}
