#![feature(trait_alias)]

trait Value {
    fn read(&self) -> u16;
}

impl Value for u16 {
    fn read(&self) -> u16 {
        *self
    }
}

trait FirstFlags = Send + Sync;
trait SecondFlags = Sync + Send + Send;

type Repeated = dyn Value + FirstFlags + SecondFlags + Send + Sync;
type Canonical = dyn Value + Send + Sync;

fn canonical(value: Box<Repeated>) -> Box<Canonical> {
    value
}

fn main() {
    let value: Box<Repeated> = Box::new(151u16);
    assert_eq!(canonical(value).read(), 151);
    assert_eq!(
        std::any::TypeId::of::<Repeated>(),
        std::any::TypeId::of::<Canonical>(),
    );
}
