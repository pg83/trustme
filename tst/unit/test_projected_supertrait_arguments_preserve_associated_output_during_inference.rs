trait Keys {
    type Key;
}

struct ByteKeys;
impl Keys for ByteKeys {
    type Key = u8;
}

trait Parent<Key> {
    type Item;
    fn item() -> Self::Item;
}

trait Child<Mapping: Keys>: Parent<Mapping::Key> {}

struct Leaf;
impl Parent<u8> for Leaf {
    type Item = u16;
    fn item() -> u16 { 61 }
}
impl Child<ByteKeys> for Leaf {}

trait Source {
    type Mapping: Keys<Key = u8>;
    type Value: Child<Self::Mapping, Item = u16>;
}

struct Provider;
impl Source for Provider {
    type Mapping = ByteKeys;
    type Value = Leaf;
}

fn require<Value, Key>() -> u16
where
    Value: Parent<Key, Item = u16>,
{
    Value::item()
}

fn read<Provider: Source>() -> u16 {
    require::<Provider::Value, _>()
}

fn main() {
    assert_eq!(read::<Provider>(), 61);
    assert_eq!(require::<Leaf, u8>(), 61);
}
