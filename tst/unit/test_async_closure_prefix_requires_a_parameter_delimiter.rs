//@ compile-fail: Unexpected token

fn main() {
    let _ = &async static move 7;
}
