/* { dg-output "Err: 15\r*\n" } */

fn baz() -> Result<i32, i32> {
    Err(15)
}

fn foo() -> Result<i32, i32> {
    Ok(15 + baz()?)
}

fn gccrs_main() -> i32 {
    match foo() {
        Ok(value) => println!("Ok: {value}"),
        Err(err) => println!("Err: {err}"),
    }
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
