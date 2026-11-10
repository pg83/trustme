struct Record<T: ?Sized> {
    tag: u8,
    payload: T,
}

impl<T: ?Sized> Record<T> {
    fn payload(&self) -> &T {
        &self.payload
    }
}

fn inspect<T: ?Sized>(record: &Record<T>) -> (u8, &T) {
    (record.tag, record.payload())
}

trait Payload {
    type Data: ?Sized;
    fn data(&self) -> &Self::Data;
}

trait Tagged: Payload {
    fn tag(&self) -> u8;
}

impl<const N: usize> Payload for Record<[u16; N]> {
    type Data = [u16];

    fn data(&self) -> &[u16] {
        &self.payload
    }
}

impl<const N: usize> Tagged for Record<[u16; N]> {
    fn tag(&self) -> u8 {
        self.tag
    }
}

fn main() {
    let record = Record { tag: 47, payload: [11u16, 23, 37] };
    let view: &Record<[u16]> = &record;
    assert_eq!(inspect(view), (47, &[11, 23, 37][..]));
    let tagged: &dyn Tagged<Data = [u16]> = &record;
    assert_eq!(tagged.tag(), 47);
    assert_eq!(tagged.data(), &[11, 23, 37]);
}
