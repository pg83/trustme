
#![feature(lang_items)]
fn gccrs_main() -> i32 {
  let foo = |&&d: &&i32| -> i32 { d };

  let x = &&5i32;
  foo(x) - 5
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
