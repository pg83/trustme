//@ edition: 2021
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    async move { 1668u16 };
}
