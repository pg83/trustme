trait Base {
    type Item;
    fn value(&self) -> Self::Item;
}

trait Derived: Base {
    fn bias(&self) -> u8;
}

struct Number(u8);

impl Base for Number {
    type Item = u8;
    fn value(&self) -> u8 { self.0 }
}

impl Derived for Number {
    fn bias(&self) -> u8 { 3 }
}

fn upcast<T: Copy>(value: &dyn Derived<Item = T>) -> &dyn Base<Item = T> {
    value
}

fn main() {
    let number = Number(211);
    let marked: &(dyn Derived<Item = u8> + Send) = &number;
    let derived: &dyn Derived<Item = u8> = marked;
    let base = upcast(derived);
    assert_eq!(base.value(), 211);
    assert_eq!(derived.bias(), 3);
}
