#![feature(type_alias_impl_trait)]

type Callback = impl Fn(u16) -> u32;
struct Factory;

impl Factory {
    fn keep(&self, callback: Callback) -> Callback { callback }

    #[define_opaque(Callback)]
    fn make(&self) -> Callback {
        self.keep(|amount| 65537 + u32::from(amount))
    }
}

fn main() {
    let callback = Factory.make();
    assert_eq!(callback(257), 65794);
    assert_eq!(callback(3), 65540);
}
