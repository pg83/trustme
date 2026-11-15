//@ compile-only
//@ compile-fail: Look up pattern value
//@ compile-flags: --emit=metadata -Cpanic=abort

enum Endpoint {
    Low,
    High,
}

fn inferred<T>() -> T {
    loop {}
}

fn main() {
    match inferred() {
        <Endpoint>::Low..=<Endpoint>::High => {},
        _ => {},
    }
}
