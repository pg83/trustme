trait Factory {
    type Callable;
    fn into_callable(self) -> Self::Callable;
}

fn answer() -> u8 {
    73
}

fn alternative(value: u8) -> u16 {
    value as u16 + 1
}

impl Factory for i32 {
    type Callable = fn() -> u8;
    fn into_callable(self) -> Self::Callable {
        answer
    }
}

impl Factory for u32 {
    type Callable = fn(u8) -> u16;
    fn into_callable(self) -> Self::Callable {
        alternative
    }
}

fn make<T: Factory>(value: T) -> T::Callable {
    value.into_callable()
}

fn main() {
    assert_eq!(make(0)(), 73u8);
}
