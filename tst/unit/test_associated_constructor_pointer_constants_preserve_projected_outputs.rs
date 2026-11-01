struct Packet<T>(T);
trait Build {
    type Output;
    const MAKE: fn(u16) -> Self::Output;
}
struct Seed;
impl Build for Seed {
    type Output = Packet<u16>;
    const MAKE: fn(u16) -> Self::Output = Packet;
}
fn build<B: Build>(value: u16) -> B::Output { (B::MAKE)(value) }

fn main() {
    assert_eq!(build::<Seed>(31).0, 31);
    assert_eq!((Seed::MAKE)(37).0, 37);
}
