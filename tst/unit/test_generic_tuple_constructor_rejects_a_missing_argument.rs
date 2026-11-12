//@ compile-fail: argument

struct Pair<T>(T, T);

fn main() {
    let _: Pair<u16> = Pair(167u16);
}
