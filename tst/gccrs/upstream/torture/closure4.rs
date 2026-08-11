
#![feature(lang_items)]
struct Foo(i32, i32);

fn gccrs_main() -> i32 {
  let foo = |&x: &i32, y: i32| -> i32 {
    x + y
  };
  
  let bar = |Foo(x, y): Foo| -> i32 {
    x + y
  };

  let a = 4;
  foo(&a, 2) + bar(Foo(100, 200)) - 306
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
