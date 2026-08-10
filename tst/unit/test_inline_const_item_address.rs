fn answer() -> usize {
    37
}

fn identity<T>(value: T) -> T {
    value
}

fn call_generic<T>(value: T) -> T {
    let function = const { identity::<T> };
    function(value)
}

struct Wrapper(usize);

enum Number {
    Value(usize),
}

fn main() {
    let function = const { answer };
    assert_eq!(function(), 37);

    let address = function as fn() -> usize as usize;
    assert_ne!(address, 0);

    assert_eq!(call_generic(41usize), 41);

    let make_wrapper = const { Wrapper };
    assert_eq!(make_wrapper(43).0, 43);

    let make_number = const { Number::Value };
    match make_number(47) {
        Number::Value(value) => assert_eq!(value, 47),
    }
}
