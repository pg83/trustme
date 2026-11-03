//@ compile-fail: Unable to find
fn attempt<ImportType>() {
    use ImportType as Saved;
    let _: Option<Saved> = None;
}
fn main() { attempt::<u16>(); }
