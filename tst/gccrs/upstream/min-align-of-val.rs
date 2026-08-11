// { dg-require-effective-target lp64 }

trait TraitA {}

struct StructA {
    _a: i32,
    _b: i16,
}

impl TraitA for StructA {}
impl TraitA for u8 {}

fn gccrs_main() -> i32 {
    let value = 32i32;
    assert_eq!(std::mem::align_of_val(&value), 4);

    let value = StructA { _a: 1, _b: 2 };
    assert_eq!(std::mem::align_of_val(&value), 4);
    assert_eq!(std::mem::align_of_val(&value as &dyn TraitA), 4);

    let array = [10i32, 20, 30];
    assert_eq!(std::mem::align_of_val(&array[..]), 4);
    assert_eq!(std::mem::align_of_val("gccrs"), 1);

    let byte = 7u8;
    assert_eq!(std::mem::align_of_val(&byte), 1);
    assert_eq!(std::mem::align_of_val(&byte as &dyn TraitA), 1);
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
