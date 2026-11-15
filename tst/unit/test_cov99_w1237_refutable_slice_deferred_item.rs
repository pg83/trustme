//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata

fn main() {
    let mut stream = std::iter::empty();
    if let Some([element]) = stream.next() {
        let _ = element;
    }
}
