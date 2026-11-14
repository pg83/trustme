trait WithType<A> {}

impl WithType<u8> for i32 {}

trait Selected {}

impl<T: WithType<u8>> Selected for T {}

fn require<T: Selected>(_: T) {}

fn main() {
    require(7);
}
