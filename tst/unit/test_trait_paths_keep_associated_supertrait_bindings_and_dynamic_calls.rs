mod definitions {
    pub trait Root {
        type Item;
        fn seed(&self) -> Self::Item;
    }
    pub trait Branch: Root<Item = u16> {
        fn scaled(&self) -> u16 { self.seed() * 3 }
    }
    pub trait Leaf: Branch {
        fn finish(&self) -> u16 { self.scaled() + 5 }
    }
    pub struct Value(pub u16);
    impl Root for Value {
        type Item = u16;
        fn seed(&self) -> u16 { self.0 }
    }
    impl Branch for Value {}
    impl Leaf for Value {}
}

fn read<T: definitions::Leaf>(value: &T) -> u16 { value.finish() }
fn read_dynamic(value: &dyn definitions::Leaf) -> u16 { value.finish() }

fn main() {
    let value = definitions::Value(7);
    assert_eq!(read(&value), 26);
    assert_eq!(read_dynamic(&value), 26);
}
