// { dg-output "hello world: gccrs\r*\n" }
// { dg-additional-options "-w" }

static TEST_1: &str = "gccrs";
static TEST_2: i32 = 123;

struct Foo(i32, bool);
static TEST_3: Foo = Foo(123, false);

extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    println!("hello world: {}", TEST_1);
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
