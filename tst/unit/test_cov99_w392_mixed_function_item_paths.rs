struct Pair<A, B>(A, B);

impl<T> Pair<T, T> {
    fn select(&self) -> u32 {
        1
    }
}

trait Fallback {
    fn select(&self) -> u32;
}

impl<A, B> Fallback for Pair<A, B> {
    fn select(&self) -> u32 {
        2
    }
}

fn free() {}

struct Host;

impl Host {
    fn associated() {}
}

fn main() {
    let pair = Pair(free, Host::associated);
    assert_eq!(pair.select(), 2);
}
