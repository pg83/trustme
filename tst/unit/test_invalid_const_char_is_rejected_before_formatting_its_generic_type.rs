//@ compile-fail: invalid

const BAD: char = unsafe { std::mem::transmute::<u32, char>(0x20_0000) };

struct Marker<const VALUE: char>;

fn require_letter(_: Marker<'A'>) {}

fn main() {
    require_letter(Marker::<BAD>);
}
