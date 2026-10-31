macro_rules! spelling { ($value:ty) => { stringify!($value) }; }
fn compact(text: &str) -> String { text.chars().filter(|c| !c.is_whitespace()).collect() }
fn main() {
    for (actual, expected) in [
        (spelling!(()), "()"),
        (spelling!(!), "!"),
        (spelling!(_), "_"),
        (spelling!(unsafe extern "C" fn(u8, ...) -> u32), "unsafe extern \"C\" fn(u8, ...) -> u32"),
        (spelling!(dyn Trait + Send), "dyn Trait + Send"),
        (spelling!(impl Trait<Output = u16>), "impl Trait<Output = u16>"),
        (spelling!(<Container<u8> as Trait<u16>>::Output), "<Container<u8> as Trait<u16>>::Output"),
        (spelling!(*const [u8; 7]), "*const [u8; 7]"),
        (spelling!(&mut [u8]), "&mut [u8]"),
    ] { assert_eq!(compact(actual), compact(expected)); }
}
