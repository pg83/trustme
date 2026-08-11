
fn foo (&a: &i32, b: i32) -> i32 {
  a + b
}

fn gccrs_main() -> i32 {
  let a = 4;
  foo(&a, 2) - 6
}
fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
