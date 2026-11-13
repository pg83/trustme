trait Read {
    type Value;
    fn read(self) -> Self::Value;
}

struct Number(u32);

impl Read for Number {
    type Value = u32;

    fn read(self) -> u32 {
        self.0
    }
}

fn opaque(value: u32) -> impl Read<Value = u32> {
    Number(value)
}

fn project<T: Read>(value: T) -> T::Value {
    value.read()
}

fn main() {
    let nested = Some((opaque(37), [opaque(5), opaque(11)]));
    match nested {
        Some((first, [second, third])) => {
            assert_eq!(project(first) + project(second) + project(third), 53);
        }
        None => unreachable!(),
    }
}
