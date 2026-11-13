trait Tagged {}
trait HasByte { type Byte; }

impl Tagged for u8 {}
impl HasByte for u8 { type Byte = u8; }

fn tagged<T: Tagged + HasByte<Byte = u8>>(value: T) -> T { value }

fn merge<A, B, C>(a: A, b: B, c: C) -> (A, B, C) { (a, b, c) }

fn verify<T>(value: T) {
    assert_eq!(core::mem::size_of_val(&value), 2187);
}

macro_rules! fan {
    () => { tagged(9u8) };
    ($head:tt $($tail:tt)*) => {
        merge(fan!($($tail)*), fan!($($tail)*), fan!($($tail)*))
    };
}

fn main() {
    verify(fan!(@ @ @ @ @ @ @));
}
