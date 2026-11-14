trait Same {}

impl<T> Same for (T, T) {}

struct Vessel<T>(T);

impl<T> Vessel<T> {
    fn identity(value: T) -> T {
        value
    }
}

fn accept<T: Same>(_: T) {}

fn main() {
    accept((Vessel::<_>::identity, Vessel::<u8>::identity));
}
