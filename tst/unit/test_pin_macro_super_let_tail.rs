//@ crate-type: lib

fn temp_lifetime() {
    match std::pin::pin!(foo(&mut 0)) {
        _ => {}
    }
    async fn foo(_: &mut usize) {}
}
