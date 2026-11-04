//@ input: aux/own_dynamic_link_driver.rs
//@ input: aux/own_dynamic_base.rs
//@ input: aux/own_dynamic_adapter.rs
//@ input: aux/own_dynamic_relative_client.rs
#[path = "aux/own_dynamic_link_driver.rs"]
mod driver;
fn main() {
    let fixture = driver::Fixture::new(file!(), "own_rust_dylib_dependencies_relative");
    fixture.compile("own_dynamic_base.rs", "libown_dynamic_base.rlib", "rlib", &["-Cpanic=abort"]);
    fixture.compile("own_dynamic_adapter.rs", "libown_dynamic_adapter.so", "dylib", &["-Cpanic=abort"]);
    let original_directory = std::env::current_dir().unwrap();
    std::env::set_current_dir(&fixture.directory).unwrap();
    fixture.compile("own_dynamic_relative_client.rs", "client", "bin", &[
        "-Cpanic=abort", "-Clink-arg=-lc",
        "--extern", "own_dynamic_base=libown_dynamic_base.rlib",
        "--extern", "own_dynamic_adapter=libown_dynamic_adapter.so",
    ]);
    fixture.execute("client");
    std::env::set_current_dir(original_directory).unwrap();
}
