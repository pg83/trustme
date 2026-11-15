trait Factory {
    type Callable;
}

impl Factory for i32 {
    type Callable = fn() -> u8;
}

impl Factory for u32 {
    type Callable = fn(u8) -> u16;
}

struct Witness<T, F>(T, F);

impl<'a, T: Factory> Witness<T, &'a Option<T::Callable>> {
    fn touch(&self) {}
}

fn main() {
    let maybe = None;
    let witness = Witness(0, &maybe);
    if let Some(callable) = maybe {
        witness.touch();
        let _: u8 = callable();
    }
}
