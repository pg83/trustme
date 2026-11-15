//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    loop {
        let _: [u8; 2] = [continue; 2];
    }
}
