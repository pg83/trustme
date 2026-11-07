#![feature(type_alias_impl_trait)]

trait Value {
    fn read(&self) -> u16;
}

impl Value for u16 {
    fn read(&self) -> u16 {
        *self
    }
}

type Hidden = impl Copy + Value;

#[define_opaque(Hidden)]
const fn make() -> Hidden {
    83u16
}

const DATA: &(Hidden,) = &(make(),);

fn main() {
    assert_eq!(DATA.0.read(), 83);
    assert_eq!(std::mem::size_of_val(DATA), 2);
}
