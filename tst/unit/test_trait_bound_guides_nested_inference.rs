#[derive(Clone, Default)]
struct MaybeCopy<T>(T);

impl Copy for MaybeCopy<u8> {}

fn require_copy<T: Copy>(_: T) {}

fn main() {
    require_copy(MaybeCopy::default());
    [MaybeCopy::default(); 13];
    [String::new(); 0];
    [String::new(); 1];
}
