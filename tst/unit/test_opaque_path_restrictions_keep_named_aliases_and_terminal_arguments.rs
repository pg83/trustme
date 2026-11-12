#![feature(inherent_associated_types, type_alias_impl_trait)]

struct Wrapped<F>(F);

impl<F> Wrapped<F> {
    type Item = F;
}

type Hidden = impl Fn() -> u16;

#[define_opaque(Hidden)]
fn hidden() -> Hidden {
    || 223
}

struct Factory;

impl Factory {
    fn item() -> Wrapped<fn() -> u16>::Item {
        fn read() -> u16 {
            227
        }
        read
    }

    fn named() -> Wrapped<Hidden>::Item {
        hidden()
    }

    fn wrapped() -> Wrapped<impl Fn() -> u16> {
        Wrapped(|| 229)
    }
}

fn main() {
    assert_eq!(Factory::item()(), 227);
    assert_eq!(Factory::named()(), 223);
    let Wrapped(read) = Factory::wrapped();
    assert_eq!(read(), 229);
}
