trait Item {
    type Output;
}

trait Factory {
    fn make<T: Item>() -> impl Item<Output = T::Output>;
}

fn main() {}
