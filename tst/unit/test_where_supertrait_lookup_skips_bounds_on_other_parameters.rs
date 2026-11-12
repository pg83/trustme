trait Base {
    type Item;
    fn item() -> Self::Item;
}

trait Derived<T>
where
    T: Copy,
    Self: Base,
{
}

struct Value;

impl Base for Value {
    type Item = u16;

    fn item() -> u16 {
        83
    }
}

impl Derived<u32> for Value {}

fn read<T: Derived<u32, Item = u16>>() -> u16 {
    T::item()
}

fn main() {
    assert_eq!(read::<Value>(), 83);
}
