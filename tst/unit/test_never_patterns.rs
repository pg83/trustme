// `!` matches a value of an uninhabited type. No such value exists, so the
// alternative it belongs to never matches and the body it guards never runs.
#![feature(never_patterns)]
#![allow(incomplete_features)]
#![allow(dead_code)]

#[derive(Copy, Clone)]
enum Void {}

fn never_arg(!: Void) -> ! {}

fn never_arg_returns_anything<T>(!: Void) -> T {}

fn ref_never_arg(&!: &Void) -> ! {}

fn by_value(s: Void) {
    move || {
        let ! = s;
    };
}

fn main() {
    let res_void: Result<bool, Void> = Ok(true);

    let (Ok(x) | Err(!)) = res_void;
    assert!(x);
    let (Err(!) | Ok(y)) = res_void;
    assert!(y);

    let mut seen = 0;
    match res_void {
        Ok(v) | Err(!) => seen += v as i32,
    }
    match res_void {
        Err(!) | Ok(v) => seen += v as i32,
    }

    let nested: Result<Result<bool, Void>, Void> = Ok(Ok(true));
    match nested {
        Ok(Ok(v) | Err(!)) | Err(!) => seen += v as i32,
    }
    assert_eq!(seen, 3);
}
