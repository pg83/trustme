//@ compile-fail: A slice pattern takes at most one

fn main() {
    let [.., ..] = [1u8, 2];
}
