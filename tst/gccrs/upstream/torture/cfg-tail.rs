
fn foo() -> i32 {
    {54}
    #[cfg(all(A, not(A)))]
    {45}
}

fn gccrs_main() -> i32 {
    return foo() - 54;
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
