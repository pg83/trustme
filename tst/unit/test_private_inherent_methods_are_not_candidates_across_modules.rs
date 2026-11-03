//@ compile-fail: No applicable methods
mod archive {
    pub struct Ledger;
    impl Ledger {
        fn read(&self) -> u32 { 53 }
    }
}
fn main() {
    let _ = archive::Ledger.read();
}
