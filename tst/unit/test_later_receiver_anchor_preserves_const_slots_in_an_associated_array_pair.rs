trait Storage {
    type Bytes;
    fn bytes(&self) -> Self::Bytes;
}

impl<const N: usize> Storage for [u8; N] {
    type Bytes = [u8; N];

    fn bytes(&self) -> [u8; N] {
        *self
    }
}

struct Envelope<T>(T);

trait ReadPair {
    type Pair;
    fn read_pair(&self) -> Self::Pair;
}

impl<T, const N: usize> ReadPair for Envelope<T>
where
    T: Storage<Bytes = [u8; N]>,
{
    type Pair = ([u8; N], [u8; N]);

    fn read_pair(&self) -> Self::Pair {
        (self.0.bytes(), self.0.bytes())
    }
}

fn anchor(_: &Envelope<[u8; 3]>) {}

fn main() {
    let value = Envelope(Default::default());
    let pair = value.read_pair();
    anchor(&value);
    assert_eq!(pair, ([0u8; 3], [0; 3]));
    assert_eq!(value.read_pair(), pair);
}
