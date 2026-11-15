#![feature(type_alias_impl_trait)]

trait Marker<T> {}
impl<T> Marker<T> for () {}

fn require<S, T>() -> u8
where
    S: Marker<T>,
{
    42
}

fn run<T: Copy>(value: T) -> u8 {
    type Identity<U: Copy> = impl Copy;

    #[define_opaque()]
    fn probe() -> u8 {
        type Receiver = ();
        require::<Receiver, Identity<Identity<u8>>>()
    }

    #[define_opaque(Identity)]
    fn define<U: Copy>(value: U) -> Identity<U> {
        value
    }

    let _: Identity<T> = value;
    probe()
}

fn main() {
    assert_eq!(run(0u8), 42);
}
