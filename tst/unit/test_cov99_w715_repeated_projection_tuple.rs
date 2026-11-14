trait Element {
    type Item;
}

struct Seed;

impl Element for Seed {
    type Item = u8;
}

struct Pair<T>(T);

trait Output {
    type Value;
}

impl<T: Element> Output for Pair<T> {
    type Value = (T::Item, T::Item);
}

fn require_pair<P: Output<Value = (u8, u8)>>(_: P) {}

fn main() {
    require_pair(Pair(Seed));
}
