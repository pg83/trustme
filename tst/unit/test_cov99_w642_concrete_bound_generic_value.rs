trait Reading {
    type Value;
    fn read(&self) -> u8;
}

struct Fixed;

impl Reading for Fixed {
    type Value = u8;

    fn read(&self) -> u8 {
        42
    }
}

fn via_value<T>(fixed: Fixed) -> u8
where
    Fixed: Reading<Value = T>,
{
    fixed.read()
}

fn main() {
    assert_eq!(via_value::<u8>(Fixed), 42);
}
