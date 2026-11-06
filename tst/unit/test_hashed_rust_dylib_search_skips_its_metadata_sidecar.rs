//@ input: aux/own_dynamic_link_driver.rs
//@ input: aux/own_hashed_dylib_value.rs
//@ input: aux/own_hashed_dylib_client.rs
#[path = "aux/own_dynamic_link_driver.rs"]
mod driver;
fn main() {
    let fixture = driver::Fixture::new(file!(), "own_rust_dylib_dependencies_hashed_search");
    fixture.compile("own_hashed_dylib_value.rs", "libown_hashed_dylib_value-a71c.so", "dylib", &["-Cpanic=abort"]);
    let source = std::path::Path::new(file!()).parent().unwrap().join("aux/own_hashed_dylib_client.rs");
    let output = std::process::Command::new(std::env::var_os("RUSTC").unwrap())
        .arg(source)
        .args(["--edition=2021", "--crate-type=bin", "-Cpanic=abort", "-Ctarget-feature=-crt-static", "-Clink-arg=-fPIC", "-Clink-arg=-lc"])
        .arg("-L").arg(fixture.directory.join("unavailable"))
        .arg("-L").arg(&fixture.directory)
        .arg("-o").arg(fixture.directory.join("client"))
        .output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    fixture.execute("client");
}
