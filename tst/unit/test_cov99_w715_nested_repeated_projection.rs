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

trait Ready {}

impl<T: Output<Value = (u8, u8)>> Ready for T {}

fn require_ready<T: Ready>(_: T) {}

fn main() {
    require_ready(Pair(Seed));
}
