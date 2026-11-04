//@ input: aux/own_dynamic_link_driver.rs
//@ input: aux/own_dynamic_value.c
//@ input: aux/own_dynamic_c_client.rs
#[path = "aux/own_dynamic_link_driver.rs"]
mod driver;
fn main() {
    let fixture = driver::Fixture::new(file!(), "own_c_dylib_search");
    fixture.compile_c("own_dynamic_value.c", "libown_dynamic_value.so");
    fixture.compile("own_dynamic_c_client.rs", "client", "bin", &[]);
    fixture.execute("client");
}
