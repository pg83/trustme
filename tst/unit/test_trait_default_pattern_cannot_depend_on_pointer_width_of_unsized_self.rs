//@ compile-fail: Pattern for usize

trait PointerPattern {
    const BYTES: usize = std::mem::size_of::<*const Self>();
    fn matches(&self, value: usize) -> bool {
        match value {
            Self::BYTES => true,
            _ => false,
        }
    }
}

impl PointerPattern for str {}

fn main() {
    let _ = "wide".matches(2 * std::mem::size_of::<usize>());
}
