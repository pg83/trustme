//@ input: aux/own_dynamic_link_driver.rs
//@ input: aux/own_dynamic_base.rs
//@ input: aux/own_dynamic_adapter.rs
//@ input: aux/own_dynamic_rust_client.rs
#[path = "aux/own_dynamic_link_driver.rs"]
mod driver;
fn main() {
    let fixture = driver::Fixture::new(file!(), "own_rust_dylib_dependencies_abort");
    fixture.compile("own_dynamic_base.rs", "libown_dynamic_base.rlib", "rlib", &["-Cpanic=abort"]);
    fixture.compile("own_dynamic_adapter.rs", "libown_dynamic_adapter.so", "dylib", &["-Cpanic=abort"]);
    fixture.compile("own_dynamic_rust_client.rs", "client", "bin", &["-Cpanic=abort", "-Clink-arg=-lc"]);
    fixture.execute("client");
}
