//@ compile-fail: generic

trait PointerPattern {
    const BYTES: usize = std::mem::size_of::<*const Self>();
    fn pointer_matches(&self, value: usize) -> bool {
        match value {
            Self::BYTES => true,
            _ => false,
        }
    }
}

impl PointerPattern for str {}

fn main() {
    let _ = <str as PointerPattern>::pointer_matches("wide", 2 * std::mem::size_of::<usize>());
}
